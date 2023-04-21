/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include <efi.h>
#include <efilib.h>
#include <stdbool.h>
#if defined(__i386__) || defined(__x86_64__)
#  include <cpuid.h>
#endif

#include "drivers.h"
#include "efi-string.h"
#include "string-util-fundamental.h"
#include "util.h"

#define QEMU_KERNEL_LOADER_FS_MEDIA_GUID \
        { 0x1428f772, 0xb64a, 0x441e, { 0xb8, 0xc3, 0x9e, 0xbd, 0xd7, 0xf8, 0x93, 0xc7 } }

#define VMM_BOOT_ORDER_GUID \
        { 0x668f4529, 0x63d0, 0x4bb5, { 0xb6, 0x5d, 0x6f, 0xbb, 0x9d, 0x36, 0xa4, 0x4a } }

/* detect direct boot */
bool is_direct_boot(EFI_HANDLE device) {
        EFI_STATUS err;
        VENDOR_DEVICE_PATH *dp; /* NB: Alignment of this structure might be quirky! */

        err = BS->HandleProtocol(device, MAKE_GUID_PTR(EFI_DEVICE_PATH_PROTOCOL), (void **) &dp);
        if (err != EFI_SUCCESS)
                return false;

        /* 'qemu -kernel systemd-bootx64.efi' */
        if (dp->Header.Type == MEDIA_DEVICE_PATH &&
            dp->Header.SubType == MEDIA_VENDOR_DP &&
            memcmp(&dp->Guid, MAKE_GUID_PTR(QEMU_KERNEL_LOADER_FS_MEDIA), sizeof(EFI_GUID)) == 0) /* Don't change to efi_guid_equal() because EFI device path objects are not necessarily aligned! */
                return true;

        /* loaded from firmware volume (sd-boot added to ovmf) */
        if (dp->Header.Type == MEDIA_DEVICE_PATH &&
            dp->Header.SubType == MEDIA_PIWG_FW_VOL_DP)
                return true;

        return false;
}

static bool device_path_startswith(const EFI_DEVICE_PATH *dp, const EFI_DEVICE_PATH *start) {
        if (!start)
                return true;
        if (!dp)
                return false;
        for (;;) {
                if (IsDevicePathEnd(start))
                        return true;
                if (IsDevicePathEnd(dp))
                        return false;
                size_t l1 = DevicePathNodeLength(start);
                size_t l2 = DevicePathNodeLength(dp);
                if (l1 != l2)
                        return false;
                if (memcmp(dp, start, l1) != 0)
                        return false;
                start = NextDevicePathNode(start);
                dp    = NextDevicePathNode(dp);
        }
}

/*
 * Try find ESP when not loaded from ESP
 *
 * Inspect all filesystems known to the firmware, try find the ESP.  In case VMMBootOrderNNNN variables are
 * present they are used to inspect the filesystems in the specified order.  When nothing was found or the
 * variables are not present the function will do one final search pass over all filesystems.
 *
 * Recent OVMF builds store the qemu boot order (as specified using the bootindex property on the qemu
 * command line) in VMMBootOrderNNNN.  The variables contain a device path.
 *
 * Example qemu command line:
 *     qemu -virtio-scsi-pci,addr=14.0 -device scsi-cd,scsi-id=4,bootindex=1
 *
 * Resulting variable:
 *     VMMBootOrder0000 = PciRoot(0x0)/Pci(0x14,0x0)/Scsi(0x4,0x0)
 */
EFI_STATUS vmm_open(EFI_HANDLE *ret_vmm_dev, EFI_FILE **ret_vmm_dir) {
        _cleanup_free_ EFI_HANDLE *handles = NULL;
        size_t n_handles;
        EFI_STATUS err, dp_err;

        assert(ret_vmm_dev);
        assert(ret_vmm_dir);

        /* Make sure all file systems have been initialized. Only do this in VMs as this is slow
         * on some real firmwares. */
        (void) reconnect_all_drivers();

        /* find all file system handles */
        err = BS->LocateHandleBuffer(
                        ByProtocol, MAKE_GUID_PTR(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL), NULL, &n_handles, &handles);
        if (err != EFI_SUCCESS)
                return err;

        for (size_t order = 0;; order++) {
                _cleanup_free_ EFI_DEVICE_PATH *dp = NULL;

                _cleanup_free_ char16_t *order_str = xasprintf("VMMBootOrder%04zx", order);
                dp_err = efivar_get_raw(MAKE_GUID_PTR(VMM_BOOT_ORDER), order_str, (char **) &dp, NULL);

                for (size_t i = 0; i < n_handles; i++) {
                        _cleanup_(file_closep) EFI_FILE *root_dir = NULL, *efi_dir = NULL;
                        EFI_DEVICE_PATH *fs;

                        err = BS->HandleProtocol(
                                        handles[i], MAKE_GUID_PTR(EFI_DEVICE_PATH_PROTOCOL), (void **) &fs);
                        if (err != EFI_SUCCESS)
                                return err;

                        /* check against VMMBootOrderNNNN (if set) */
                        if (dp_err == EFI_SUCCESS && !device_path_startswith(fs, dp))
                                continue;

                        err = open_volume(handles[i], &root_dir);
                        if (err != EFI_SUCCESS)
                                continue;

                        /* simple ESP check */
                        err = root_dir->Open(root_dir, &efi_dir, (char16_t*) u"\\EFI",
                                             EFI_FILE_MODE_READ,
                                             EFI_FILE_READ_ONLY | EFI_FILE_DIRECTORY);
                        if (err != EFI_SUCCESS)
                                continue;

                        *ret_vmm_dev = handles[i];
                        *ret_vmm_dir = TAKE_PTR(root_dir);
                        return EFI_SUCCESS;
                }

                if (dp_err != EFI_SUCCESS)
                        return EFI_NOT_FOUND;
        }
        assert_not_reached();
}

static bool cpuid_in_hypervisor(void) {
#if defined(__i386__) || defined(__x86_64__)
        unsigned eax, ebx, ecx, edx;

        /* This is a dumbed down version of src/basic/virt.c's detect_vm() that safely works in the UEFI
         * environment. */

        if (__get_cpuid(1, &eax, &ebx, &ecx, &edx) == 0)
                return false;

        if (FLAGS_SET(ecx, 0x80000000U))
                return true;
#endif

        return false;
}

typedef struct {
        uint8_t anchor_string[4];
        uint8_t entry_point_structure_checksum;
        uint8_t entry_point_length;
        uint8_t major_version;
        uint8_t minor_version;
        uint16_t max_structure_size;
        uint8_t entry_point_revision;
        uint8_t formatted_area[5];
        uint8_t intermediate_anchor_string[5];
        uint8_t intermediate_checksum;
        uint16_t table_length;
        uint32_t table_address;
        uint16_t number_of_smbios_structures;
        uint8_t smbios_bcd_revision;
} _packed_ SmbiosEntryPoint;

typedef struct {
        uint8_t anchor_string[5];
        uint8_t entry_point_structure_checksum;
        uint8_t entry_point_length;
        uint8_t major_version;
        uint8_t minor_version;
        uint8_t docrev;
        uint8_t entry_point_revision;
        uint8_t reserved;
        uint32_t table_maximum_size;
        uint64_t table_address;
} _packed_ Smbios3EntryPoint;

typedef struct {
        uint8_t type;
        uint8_t length;
        uint8_t handle[2];
} _packed_ SmbiosHeader;

typedef struct {
        SmbiosHeader header;
        uint8_t vendor;
        uint8_t bios_version;
        uint16_t bios_segment;
        uint8_t bios_release_date;
        uint8_t bios_size;
        uint64_t bios_characteristics;
        uint8_t bios_characteristics_ext[2];
} _packed_ SmbiosTableType0;

static void *find_smbios_configuration_table(uint64_t *ret_size) {
        assert(ret_size);

        Smbios3EntryPoint *entry3 = find_configuration_table(MAKE_GUID_PTR(SMBIOS3_TABLE));
        if (entry3 && memcmp(entry3->anchor_string, "_SM3_", 5) == 0 &&
            entry3->entry_point_length <= sizeof(*entry3)) {
                *ret_size = entry3->table_maximum_size;
                return PHYSICAL_ADDRESS_TO_POINTER(entry3->table_address);
        }

        SmbiosEntryPoint *entry = find_configuration_table(MAKE_GUID_PTR(SMBIOS_TABLE));
        if (entry && memcmp(entry->anchor_string, "_SM_", 4) == 0 &&
            entry->entry_point_length <= sizeof(*entry)) {
                *ret_size = entry->table_length;
                return PHYSICAL_ADDRESS_TO_POINTER(entry->table_address);
        }

        return NULL;
}

static SmbiosHeader *get_smbios_table(uint8_t type) {
        uint64_t size = 0;
        uint8_t *p = find_smbios_configuration_table(&size);
        if (!p)
                return false;

        for (;;) {
                if (size < sizeof(SmbiosHeader))
                        return NULL;

                SmbiosHeader *header = (SmbiosHeader *) p;

                /* End of table. */
                if (header->type == 127)
                        return NULL;

                if (size < header->length)
                        return NULL;

                if (header->type == type)
                        return header; /* Yay! */

                /* Skip over formatted area. */
                size -= header->length;
                p += header->length;

                /* Skip over string table. */
                for (;;) {
                        while (size > 0 && *p != '\0') {
                                p++;
                                size--;
                        }
                        if (size == 0)
                                return NULL;
                        p++;
                        size--;

                        /* Double NUL terminates string table. */
                        if (*p == '\0') {
                                if (size == 0)
                                        return NULL;
                                p++;
                                break;
                        }
                }
        }

        return NULL;
}

static bool smbios_in_hypervisor(void) {
        /* Look up BIOS Information (Type 0). */
        SmbiosTableType0 *type0 = (SmbiosTableType0 *) get_smbios_table(0);
        if (!type0 || type0->header.length < sizeof(SmbiosTableType0))
                return false;

        /* Bit 4 of 2nd BIOS characteristics extension bytes indicates virtualization. */
        return FLAGS_SET(type0->bios_characteristics_ext[1], 1 << 4);
}

bool in_hypervisor(void) {
        static int cache = -1;
        if (cache >= 0)
                return cache;

        cache = cpuid_in_hypervisor() || smbios_in_hypervisor();
        return cache;
}
