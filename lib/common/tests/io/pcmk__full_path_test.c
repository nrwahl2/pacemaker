/*
 * Copyright 2020-2026 the Pacemaker project contributors
 *
 * The version control history for this file may have further details.
 *
 * This source code is licensed under the GNU General Public License version 2
 * or later (GPLv2+) WITHOUT ANY WARRANTY.
 */

#include <crm_internal.h>

#include <glib.h>                           // g_free

#include <crm/common/unittest_internal.h>

#include "mock_private.h"

static void
function_asserts(void **state)
{
    pcmk__assert_asserts(pcmk__full_path(NULL, "/dir"));
    pcmk__assert_asserts(pcmk__full_path("file", NULL));
}

static void
full_path(void **state)
{
    char *path = NULL;

    path = pcmk__full_path("file", "/dir");
    assert_string_equal(path, "/dir/file");
    g_free(path);

    path = pcmk__full_path("/full/path", "/dir");
    assert_string_equal(path, "/full/path");
    g_free(path);

    path = pcmk__full_path("../relative/path", "/dir");
    assert_string_equal(path, "/relative/path");
    g_free(path);

    path = pcmk__full_path("../relative/path", "/dir1/dir2");
    assert_string_equal(path, "/dir1/relative/path");
    g_free(path);
}

PCMK__UNIT_TEST(NULL, NULL,
                cmocka_unit_test(function_asserts),
                cmocka_unit_test(full_path))
