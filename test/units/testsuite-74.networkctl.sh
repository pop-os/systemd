#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# shellcheck disable=SC2016
set -eux
set -o pipefail

# shellcheck source=test/units/util.sh
. "$(dirname "$0")"/util.sh

at_exit() {
    systemctl stop systemd-networkd

    if [[ -v NETWORK_NAME && -v NETDEV_NAME && -v LINK_NAME ]]; then
        rm -fvr {/usr/lib,/etc}/systemd/network/"$NETWORK_NAME" "/usr/lib/systemd/network/$NETDEV_NAME" \
            {/usr/lib,/etc}/systemd/network/"$LINK_NAME" "/etc/systemd/network/${NETWORK_NAME}.d" \
            "new" "+4"
    fi
}

trap at_exit EXIT

export NETWORK_NAME="10-networkctl-test-$RANDOM.network"
export NETDEV_NAME="10-networkctl-test-$RANDOM.netdev"
export LINK_NAME="10-networkctl-test-$RANDOM.link"
cat >"/usr/lib/systemd/network/$NETWORK_NAME" <<EOF
[Match]
Name=test
EOF

# Test files
networkctl cat "$NETWORK_NAME" | tail -n +2 | cmp - "/usr/lib/systemd/network/$NETWORK_NAME"

cat >new <<EOF
[Match]
Name=test2
EOF

EDITOR='mv new' script -ec 'networkctl edit "$NETWORK_NAME"' /dev/null
printf '%s\n' '[Match]' 'Name=test2' | cmp - "/etc/systemd/network/$NETWORK_NAME"

cat >"+4" <<EOF
[Network]
IPv6AcceptRA=no
EOF

EDITOR='cp' script -ec 'networkctl edit "$NETWORK_NAME" --drop-in test' /dev/null
cmp "+4" "/etc/systemd/network/${NETWORK_NAME}.d/test.conf"

networkctl cat "$NETWORK_NAME" | grep '^# ' |
    cmp - <(printf '%s\n' "# /etc/systemd/network/$NETWORK_NAME" "# /etc/systemd/network/${NETWORK_NAME}.d/test.conf")

cat >"/usr/lib/systemd/network/$NETDEV_NAME" <<EOF
[NetDev]
Name=test2
Kind=dummy
EOF

networkctl cat "$NETDEV_NAME"

cat >"/usr/lib/systemd/network/$LINK_NAME" <<EOF
[Match]
OriginalName=test2

[Link]
Alias=test_alias
EOF

SYSTEMD_LOG_LEVEL=debug EDITOR='true' script -ec 'networkctl edit "$LINK_NAME"' /dev/null
cmp "/usr/lib/systemd/network/$LINK_NAME" "/etc/systemd/network/$LINK_NAME"

# Test links
systemctl unmask systemd-networkd
systemctl stop systemd-networkd
(! networkctl cat @test2)

systemctl start systemd-networkd
SYSTEMD_LOG_LEVEL=debug /usr/lib/systemd/systemd-networkd-wait-online -i test2:carrier --timeout 20
networkctl cat @test2:network | cmp - <(networkctl cat "$NETWORK_NAME")

EDITOR='cp' script -ec 'networkctl edit @test2 --drop-in test2.conf' /dev/null
cmp "+4" "/etc/systemd/network/${NETWORK_NAME}.d/test2.conf"

ip_link="$(ip link show test2)"
if systemctl --quiet is-active systemd-udevd; then
    assert_in 'alias test_alias' "$ip_link"
fi
