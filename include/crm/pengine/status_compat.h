/*
 * Copyright 2004-2024 the Pacemaker project contributors
 *
 * The version control history for this file may have further details.
 *
 * This source code is licensed under the GNU Lesser General Public License
 * version 2.1 or later (LGPLv2.1+) WITHOUT ANY WARRANTY.
 */

#ifndef PCMK__CRM_PENGINE_STATUS_COMPAT__H
#define PCMK__CRM_PENGINE_STATUS_COMPAT__H

#include <stdbool.h>                // bool
#include <crm/common/util.h>        // pcmk_is_set()
#include <crm/common/scheduler.h>   // pcmk_resource_t, pcmk_rsc_unique

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \file
 * \brief Deprecated Pacemaker scheduler utilities
 * \ingroup pengine
 * \deprecated Do not include this header directly. The utilities in this
 *             header, and the header itself, will be removed in a future
 *             release.
 */

//! \deprecated Compare variant and flags directly
static inline bool
pe_rsc_is_unique_clone(const pcmk_resource_t *rsc)
{
    return pe_rsc_is_clone(rsc) && pcmk_is_set(rsc->flags, pcmk_rsc_unique);
}

#ifdef __cplusplus
}
#endif

#endif // PCMK__CRM_PENGINE_STATUS_COMPAT__H
