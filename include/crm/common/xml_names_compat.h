/*
 * Copyright 2004-2026 the Pacemaker project contributors
 *
 * The version control history for this file may have further details.
 *
 * This source code is licensed under the GNU Lesser General Public License
 * version 2.1 or later (LGPLv2.1+) WITHOUT ANY WARRANTY.
 */

#ifndef PCMK__CRM_COMMON_XML_NAMES_COMPAT__H
#define PCMK__CRM_COMMON_XML_NAMES_COMPAT__H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \file
 * \brief Deprecated defined constants for Pacemaker XML element/attribute names
 * \ingroup core
 * \deprecated Do not include this header directly. The nvpair APIs in this
 *             header, and the header itself, will be removed in a future
 *             release.
 */

//! \deprecated Do not use
#define PCMK_XA_CRM_TIMESTAMP "crm-timestamp"

//! \deprecated Use \c PCMK_XA_POSITION instead
#define PCMK_XE_POSITION PCMK_XA_POSITION

#ifdef __cplusplus
}
#endif

#endif // PCMK__CRM_COMMON_XML_NAMES_COMPAT__H
