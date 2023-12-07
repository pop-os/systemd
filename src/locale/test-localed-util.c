/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "alloc-util.h"
#include "localed-util.h"
#include "log.h"
#include "string-util.h"
#include "tests.h"

TEST(find_language_fallback) {
        _cleanup_free_ char *ans = NULL, *ans2 = NULL;

        assert_se(find_language_fallback("foobar", &ans) == 0);
        assert_se(ans == NULL);

        assert_se(find_language_fallback("csb", &ans) == 0);
        assert_se(ans == NULL);

        assert_se(find_language_fallback("csb_PL", &ans) == 1);
        assert_se(streq(ans, "csb:pl"));

        assert_se(find_language_fallback("szl_PL", &ans2) == 1);
        assert_se(streq(ans2, "szl:pl"));
}

TEST(find_converted_keymap) {
        _cleanup_free_ char *ans = NULL, *ans2 = NULL;
        int r;

        assert_se(find_converted_keymap(
                        &(X11Context) {
                                .layout  = (char*) "pl",
                                .variant = (char*) "foobar",
                        }, &ans) == 0);
        assert_se(ans == NULL);

        r = find_converted_keymap(
                        &(X11Context) {
                                .layout  = (char*) "pl",
                        }, &ans);
        if (r == 0) {
                log_info("Skipping rest of %s: keymaps are not installed", __func__);
                return;
        }

        assert_se(r == 1);
        assert_se(streq(ans, "pl"));
        ans = mfree(ans);

        assert_se(find_converted_keymap(
                        &(X11Context) {
                                .layout  = (char*) "pl",
                                .variant = (char*) "dvorak",
                        }, &ans2) == 1);
        assert_se(streq(ans2, "pl-dvorak"));
}

TEST(find_legacy_keymap) {
        X11Context xc = {};
        _cleanup_free_ char *ans = NULL, *ans2 = NULL;

        xc.layout = (char*) "foobar";
        assert_se(find_legacy_keymap(&xc, &ans) == 0);
        assert_se(ans == NULL);

        xc.layout = (char*) "pl";
        assert_se(find_legacy_keymap(&xc, &ans) == 1);
        assert_se(streq(ans, "pl2"));

        xc.layout = (char*) "pl,ru";
        assert_se(find_legacy_keymap(&xc, &ans2) == 1);
        assert_se(streq(ans, "pl2"));
}

TEST(vconsole_convert_to_x11) {
        _cleanup_(x11_context_clear) X11Context xc = {};
        _cleanup_(vc_context_clear) VCContext vc = {};
        int r;

        log_info("/* test empty keymap */");
        assert_se(vconsole_convert_to_x11(&vc, &xc) >= 0);
        assert_se(x11_context_isempty(&xc));

        log_info("/* test without variant, new mapping (es:) */");
        assert_se(free_and_strdup(&vc.keymap, "es") >= 0);
        assert_se(vconsole_convert_to_x11(&vc, &xc) >= 0);
        assert_se(streq(xc.layout, "es"));
        assert_se(xc.variant == NULL);
        x11_context_clear(&xc);

        log_info("/* test with known variant, new mapping (es:dvorak) */");
        assert_se(free_and_strdup(&vc.keymap, "es-dvorak") >= 0);
        assert_se(vconsole_convert_to_x11(&vc, &xc) >= 0);
        assert_se(streq(xc.layout, "es"));
        assert_se(streq(xc.variant, "dvorak"));
        x11_context_clear(&xc);

        log_info("/* test with old mapping (fr:latin9) */");
        assert_se(free_and_strdup(&vc.keymap, "fr-latin9") >= 0);
        assert_se(vconsole_convert_to_x11(&vc, &xc) >= 0);
        assert_se(streq(xc.layout, "fr"));
        assert_se(streq(xc.variant, "latin9"));
        x11_context_clear(&xc);

        log_info("/* test with a compound mapping (ru,us) */");
        assert_se(free_and_strdup(&vc.keymap, "ru") >= 0);
        assert_se(vconsole_convert_to_x11(&vc, &xc) >= 0);
        assert_se(streq(xc.layout, "ru,us"));
        assert_se(xc.variant == NULL);
        x11_context_clear(&xc);

        log_info("/* test with a simple mapping (us) */");
        assert_se(free_and_strdup(&vc.keymap, "us") >= 0);
        assert_se(vconsole_convert_to_x11(&vc, &xc) >= 0);
        assert_se(streq(xc.layout, "us"));
        assert_se(xc.variant == NULL);
        x11_context_clear(&xc);

        /* "gh" has no mapping in kbd-model-map and kbd provides a converted keymap for this layout. */
        log_info("/* test with a converted keymap (gh:) */");
        assert_se(free_and_strdup(&vc.keymap, "gh") >= 0);
        r = vconsole_convert_to_x11(&vc, &xc);
        if (r == 0) {
                log_info("Skipping rest of %s: keymaps are not installed", __func__);
                return;
        }
        assert_se(r > 0);
        assert_se(streq(xc.layout, "gh"));
        assert_se(xc.variant == NULL);
        x11_context_clear(&xc);

        log_info("/* test with converted keymap and with a known variant (gh:ewe) */");
        assert_se(free_and_strdup(&vc.keymap, "gh-ewe") >= 0);
        assert_se(vconsole_convert_to_x11(&vc, &xc) > 0);
        assert_se(streq(xc.layout, "gh"));
        assert_se(streq(xc.variant, "ewe"));
        x11_context_clear(&xc);

        log_info("/* test with converted keymap and with an unknown variant (gh:ewe) */");
        assert_se(free_and_strdup(&vc.keymap, "gh-foobar") > 0);
        assert_se(vconsole_convert_to_x11(&vc, &xc) > 0);
        assert_se(streq(xc.layout, "gh"));
        assert_se(xc.variant == NULL);
        x11_context_clear(&xc);
}

TEST(x11_convert_to_vconsole) {
        _cleanup_(x11_context_clear) X11Context xc = {};
        _cleanup_(vc_context_clear) VCContext vc = {};

        log_info("/* test empty layout (:) */");
        assert_se(x11_convert_to_vconsole(&xc, &vc) >= 0);
        assert_se(vc_context_isempty(&vc));

        log_info("/* test without variant, new mapping (es:) */");
        assert_se(free_and_strdup(&xc.layout, "es") >= 0);
        assert_se(x11_convert_to_vconsole(&xc, &vc) >= 0);
        assert_se(streq(vc.keymap, "es"));
        vc_context_clear(&vc);

        log_info("/* test with unknown variant, new mapping (es:foobar) */");
        assert_se(free_and_strdup(&xc.variant, "foobar") >= 0);
        assert_se(x11_convert_to_vconsole(&xc, &vc) >= 0);
        assert_se(streq(vc.keymap, "es"));
        vc_context_clear(&vc);

        log_info("/* test with known variant, new mapping (es:dvorak) */");
        assert_se(free_and_strdup(&xc.variant, "dvorak") >= 0);
        assert_se(x11_convert_to_vconsole(&xc, &vc) >= 0);
        if (vc_context_isempty(&vc)) {
                log_info("Skipping rest of %s: keymaps are not installed", __func__);
                return;
        }
        assert_se(streq(vc.keymap, "es-dvorak"));
        vc_context_clear(&vc);

        /* es no-variant test is not very good as the desired match
        comes first in the list so will win if both candidates score
        the same. in this case the desired match comes second so will
        not win unless we correctly give the no-variant match a bonus
        */
        log_info("/* test without variant, desired match second (bg,us:) */");
        assert_se(free_and_strdup(&xc.layout, "bg,us") >= 0);
        assert_se(free_and_strdup(&xc.variant, NULL) >= 0);
        assert_se(x11_convert_to_vconsole(&xc, &vc) >= 0);
        assert_se(streq(vc.keymap, "bg_bds-utf8"));
        vc_context_clear(&vc);

        /* same, but with variant specified as "," */
        log_info("/* test with variant as ',', desired match second (bg,us:) */");
        assert_se(free_and_strdup(&xc.variant, ",") >= 0);
        assert_se(x11_convert_to_vconsole(&xc, &vc) >= 0);
        assert_se(streq(vc.keymap, "bg_bds-utf8"));
        vc_context_clear(&vc);

        log_info("/* test with old mapping (fr:latin9) */");
        assert_se(free_and_strdup(&xc.layout, "fr") >= 0);
        assert_se(free_and_strdup(&xc.variant, "latin9") >= 0);
        assert_se(x11_convert_to_vconsole(&xc, &vc) >= 0);
        assert_se(streq(vc.keymap, "fr-latin9"));
        vc_context_clear(&vc);

        /* https://bugzilla.redhat.com/show_bug.cgi?id=1039185 */
        /* us,ru is the x config users want, but they still want ru
        as the console layout in this case */
        log_info("/* test with a compound mapping (us,ru:) */");
        assert_se(free_and_strdup(&xc.layout, "us,ru") >= 0);
        assert_se(free_and_strdup(&xc.variant, NULL) >= 0);
        assert_se(x11_convert_to_vconsole(&xc, &vc) >= 0);
        assert_se(streq(vc.keymap, "ru"));
        vc_context_clear(&vc);

        log_info("/* test with a compound mapping (ru,us:) */");
        assert_se(free_and_strdup(&xc.layout, "ru,us") >= 0);
        assert_se(free_and_strdup(&xc.variant, NULL) >= 0);
        assert_se(x11_convert_to_vconsole(&xc, &vc) >= 0);
        assert_se(streq(vc.keymap, "ru"));
        vc_context_clear(&vc);

        /* https://bugzilla.redhat.com/show_bug.cgi?id=1333998 */
        log_info("/* test with a simple new mapping (ru:) */");
        assert_se(free_and_strdup(&xc.layout, "ru") >= 0);
        assert_se(free_and_strdup(&xc.variant, NULL) >= 0);
        assert_se(x11_convert_to_vconsole(&xc, &vc) >= 0);
        assert_se(streq(vc.keymap, "ru"));
}

static int intro(void) {
        _cleanup_free_ char *map = NULL;

        assert_se(get_testdata_dir("test-keymap-util/kbd-model-map", &map) >= 0);
        assert_se(setenv("SYSTEMD_KBD_MODEL_MAP", map, 1) == 0);

        return EXIT_SUCCESS;
}

DEFINE_TEST_MAIN_WITH_INTRO(LOG_DEBUG, intro);
