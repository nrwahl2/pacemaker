/*
 * Copyright 2022-2026 the Pacemaker project contributors
 *
 * The version control history for this file may have further details.
 *
 * This source code is licensed under the GNU General Public License version 2
 * or later (GPLv2+) WITHOUT ANY WARRANTY.
 */

#include <crm_internal.h>

#include <crm/common/unittest_internal.h>

static void
empty_input_string(void **state)
{
    int result = 0;

    assert_int_equal(pcmk__scan_min_int("", &result, 1), pcmk_rc_bad_input);
    assert_int_equal(result, 1);

    assert_int_equal(pcmk__scan_min_int(NULL, &result, 1), pcmk_rc_ok);
    assert_int_equal(result, 1);
}

static void
input_below_minimum(void **state)
{
    int result = 0;
    char *below_s = NULL;

    // Within int and long long range
    assert_int_equal(pcmk__scan_min_int("100", &result, 1024), pcmk_rc_ok);
    assert_int_equal(result, 1024);

    // Below long long range
    below_s = pcmk__assert_asprintf("%lld0", LLONG_MIN);
    assert_int_equal(pcmk__scan_min_int(below_s, &result, 1024), pcmk_rc_ok);
    assert_int_equal(result, 1024);
    free(below_s);
}

static void
input_above_maximum(void **state)
{
    int result = 0;
    char *above_s = NULL;

    // Above int range and likely within long long range
    above_s = pcmk__assert_asprintf("%d0", INT_MAX);
    assert_int_equal(pcmk__scan_min_int(above_s, &result, 100), EOVERFLOW);
    assert_int_equal(result, INT_MAX);
    free(above_s);

    // Above long long range
    above_s = pcmk__assert_asprintf("%lld0", LLONG_MAX);
    assert_int_equal(pcmk__scan_min_int(above_s, &result, 100), EOVERFLOW);
    assert_int_equal(result, INT_MAX);
    free(above_s);
}

static void
input_just_right(void **state)
{
    int result = 0;

    assert_int_equal(pcmk__scan_min_int("1024", &result, 1024), pcmk_rc_ok);
    assert_int_equal(result, 1024);

    assert_int_equal(pcmk__scan_min_int("2048", &result, 1024), pcmk_rc_ok);
    assert_int_equal(result, 2048);
}

PCMK__UNIT_TEST(NULL, NULL,
                cmocka_unit_test(empty_input_string),
                cmocka_unit_test(input_below_minimum),
                cmocka_unit_test(input_above_maximum),
                cmocka_unit_test(input_just_right))
