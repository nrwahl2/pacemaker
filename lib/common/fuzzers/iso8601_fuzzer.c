/*
 * Copyright 2024-2026 the Pacemaker project contributors
 *
 * The version control history for this file may have further details.
 *
 * This source code is licensed under the GNU Lesser General Public License
 * version 2.1 or later (LGPLv2.1+) WITHOUT ANY WARRANTY.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include <crm/common/util.h>
#include <crm/common/internal.h>

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char *ns = NULL;
    GDateTime *now = NULL;
    char *result = NULL;

    // Ensure we have enough data.
    if (size < 10) {
        return -1; // Do not add input to testing corpus
    }
    ns = pcmk__assert_alloc(size + 1, sizeof(char));
    memcpy(ns, data, size);

    now = g_date_time_new_now_local();
    result = pcmk__time_format_hr(ns, now, g_date_time_get_microsecond(now));

    free(ns);
    g_date_time_unref(now);
    free(result);
    return 0;
}
