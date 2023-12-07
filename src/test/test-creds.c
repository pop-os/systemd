/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include "creds-util.h"
#include "fileio.h"
#include "path-util.h"
#include "rm-rf.h"
#include "tests.h"
#include "tmpfile-util.h"

TEST(read_credential_strings) {
        _cleanup_free_ char *x = NULL, *y = NULL, *saved = NULL, *p = NULL;
        _cleanup_(rm_rf_physical_and_freep) char *tmp = NULL;
        _cleanup_fclose_ FILE *f = NULL;

        const char *e = getenv("CREDENTIALS_DIRECTORY");
        if (e)
                assert_se(saved = strdup(e));

        assert_se(read_credential_strings_many("foo", &x, "bar", &y) == 0);
        assert_se(x == NULL);
        assert_se(y == NULL);

        assert_se(mkdtemp_malloc(NULL, &tmp) >= 0);

        assert_se(setenv("CREDENTIALS_DIRECTORY", tmp, /* override= */ true) >= 0);

        assert_se(read_credential_strings_many("foo", &x, "bar", &y) == 0);
        assert_se(x == NULL);
        assert_se(y == NULL);

        assert_se(p = path_join(tmp, "bar"));
        assert_se(write_string_file(p, "piff", WRITE_STRING_FILE_CREATE|WRITE_STRING_FILE_AVOID_NEWLINE) >= 0);

        assert_se(read_credential_strings_many("foo", &x, "bar", &y) == 0);
        assert_se(x == NULL);
        assert_se(streq(y, "piff"));

        assert_se(write_string_file(p, "paff", WRITE_STRING_FILE_TRUNCATE|WRITE_STRING_FILE_AVOID_NEWLINE) >= 0);

        assert_se(read_credential_strings_many("foo", &x, "bar", &y) == 0);
        assert_se(x == NULL);
        assert_se(streq(y, "piff"));

        p = mfree(p);
        assert_se(p = path_join(tmp, "foo"));
        assert_se(write_string_file(p, "knurz", WRITE_STRING_FILE_CREATE|WRITE_STRING_FILE_AVOID_NEWLINE) >= 0);

        assert_se(read_credential_strings_many("foo", &x, "bar", &y) >= 0);
        assert_se(streq(x, "knurz"));
        assert_se(streq(y, "piff"));

        y = mfree(y);

        assert_se(read_credential_strings_many("foo", &x, "bar", &y) >= 0);
        assert_se(streq(x, "knurz"));
        assert_se(streq(y, "paff"));

        p = mfree(p);
        assert_se(p = path_join(tmp, "bazz"));
        assert_se(f = fopen(p, "w"));
        assert_se(fwrite("x\0y", 1, 3, f) == 3); /* embedded NUL byte should result in EBADMSG when reading back with read_credential_strings_many() */
        f = safe_fclose(f);

        assert_se(read_credential_strings_many("bazz", &x, "foo", &y) == -EBADMSG);
        assert_se(streq(x, "knurz"));
        assert_se(streq(y, "paff"));

        if (saved)
                assert_se(setenv("CREDENTIALS_DIRECTORY", saved, /* override= */ 1) >= 0);
        else
                assert_se(unsetenv("CREDENTIALS_DIRECTORY") >= 0);
}

TEST(credential_name_valid) {
        char buf[NAME_MAX+2];

        assert_se(!credential_name_valid(NULL));
        assert_se(!credential_name_valid(""));
        assert_se(!credential_name_valid("."));
        assert_se(!credential_name_valid(".."));
        assert_se(!credential_name_valid("foo/bar"));
        assert_se(credential_name_valid("foo"));

        memset(buf, 'x', sizeof(buf)-1);
        buf[sizeof(buf)-1] = 0;
        assert_se(!credential_name_valid(buf));

        buf[sizeof(buf)-2] = 0;
        assert_se(credential_name_valid(buf));
}

TEST(credential_glob_valid) {
        char buf[NAME_MAX+2];

        assert_se(!credential_glob_valid(NULL));
        assert_se(!credential_glob_valid(""));
        assert_se(!credential_glob_valid("."));
        assert_se(!credential_glob_valid(".."));
        assert_se(!credential_glob_valid("foo/bar"));
        assert_se(credential_glob_valid("foo"));
        assert_se(credential_glob_valid("foo*"));
        assert_se(credential_glob_valid("x*"));
        assert_se(credential_glob_valid("*"));
        assert_se(!credential_glob_valid("?"));
        assert_se(!credential_glob_valid("*a"));
        assert_se(!credential_glob_valid("a?"));
        assert_se(!credential_glob_valid("a[abc]"));
        assert_se(!credential_glob_valid("a[abc]"));

        memset(buf, 'x', sizeof(buf)-1);
        buf[sizeof(buf)-1] = 0;
        assert_se(!credential_glob_valid(buf));

        buf[sizeof(buf)-2] = 0;
        assert_se(credential_glob_valid(buf));

        buf[sizeof(buf)-2] = '*';
        assert_se(credential_glob_valid(buf));
}

DEFINE_TEST_MAIN(LOG_INFO);
