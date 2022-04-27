/*
 * Copyright 2022 the Pacemaker project contributors
 *
 * The version control history for this file may have further details.
 *
 * This source code is licensed under the GNU Lesser General Public License
 * version 2.1 or later (LGPLv2.1+) WITHOUT ANY WARRANTY.
 */

#include <crm_internal.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>

static void
valid(void **state) {
    assert_int_equal(pcmk__parse_health_strategy(NULL),
                     pcmk__health_strategy_none);

    assert_int_equal(pcmk__parse_health_strategy("none"),
                     pcmk__health_strategy_none);

    assert_int_equal(pcmk__parse_health_strategy("NONE"),
                     pcmk__health_strategy_none);

    assert_int_equal(pcmk__parse_health_strategy("None"),
                     pcmk__health_strategy_none);

    assert_int_equal(pcmk__parse_health_strategy("nOnE"),
                     pcmk__health_strategy_none);

    assert_int_equal(pcmk__parse_health_strategy("migrate-on-red"),
                     pcmk__health_strategy_no_red);

    assert_int_equal(pcmk__parse_health_strategy("only-green"),
                     pcmk__health_strategy_only_green);

    assert_int_equal(pcmk__parse_health_strategy("progressive"),
                     pcmk__health_strategy_progressive);

    assert_int_equal(pcmk__parse_health_strategy("custom"),
                     pcmk__health_strategy_custom);
}

static void
invalid(void **state) {
    assert_int_equal(pcmk__parse_health_strategy("foo"),
                     pcmk__health_strategy_none);
    assert_int_equal(pcmk__parse_health_strategy("custom1"),
                     pcmk__health_strategy_none);
    assert_int_equal(pcmk__parse_health_strategy("not-only-green-here"),
                     pcmk__health_strategy_none);
}

int
main(int argc, char **argv)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(valid),
        cmocka_unit_test(invalid),
    };

    cmocka_set_message_output(CM_OUTPUT_TAP);
    return cmocka_run_group_tests(tests, NULL, NULL);
}