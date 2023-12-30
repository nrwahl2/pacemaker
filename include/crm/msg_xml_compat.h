/*
 * Copyright 2004-2023 the Pacemaker project contributors
 *
 * The version control history for this file may have further details.
 *
 * This source code is licensed under the GNU Lesser General Public License
 * version 2.1 or later (LGPLv2.1+) WITHOUT ANY WARRANTY.
 */

#ifndef PCMK__CRM_MSG_XML_COMPAT__H
#  define PCMK__CRM_MSG_XML_COMPAT__H

#include <crm/common/agents.h>      // PCMK_STONITH_PROVIDES

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \file
 * \brief Deprecated Pacemaker XML constants API
 * \ingroup core
 * \deprecated Do not include this header directly. The XML constants in this
 *             header, and the header itself, will be removed in a future
 *             release.
 */

//! \deprecated Do not use
#define PCMK_META_CLONE_MAX "clone-max"

//! \deprecated Do not use
#define XML_RSC_ATTR_INCARNATION_MAX PCMK_META_CLONE_MAX

//! \deprecated Do not use
#define PCMK_META_CLONE_MIN "clone-min"

//! \deprecated Do not use
#define XML_RSC_ATTR_INCARNATION_MIN PCMK_META_CLONE_MIN

//! \deprecated Do not use
#define PCMK_META_CLONE_NODE_MAX "clone-node-max"

//! \deprecated Do not use
#define XML_RSC_ATTR_INCARNATION_NODEMAX PCMK_META_CLONE_NODE_MAX

//! \deprecated Do not use
#define PCMK_META_PROMOTED_MAX "promoted-max"

//! \deprecated Do not use
#define XML_RSC_ATTR_PROMOTED_MAX PCMK_META_PROMOTED_MAX

//! \deprecated Do not use
#define PCMK_META_PROMOTED_NODE_MAX "promoted-node-max"

//! \deprecated Do not use
#define XML_RSC_ATTR_PROMOTED_NODEMAX PCMK_META_PROMOTED_NODE_MAX

//! \deprecated Use PCMK_STONITH_PROVIDES instead
#define XML_RSC_ATTR_PROVIDES PCMK_STONITH_PROVIDES

//! \deprecated Use PCMK_XE_PROMOTABLE_LEGACY instead
#define XML_CIB_TAG_MASTER PCMK_XE_PROMOTABLE_LEGACY

//! \deprecated Do not use
#define PCMK_XA_PROMOTED_MAX_LEGACY "master-max"

//! \deprecated Do not use
#define PCMK_XE_PROMOTED_MAX_LEGACY PCMK_XA_PROMOTED_MAX_LEGACY

//! \deprecated Do not use
#define XML_RSC_ATTR_MASTER_MAX PCMK_XA_PROMOTED_MAX_LEGACY

//! \deprecated Do not use
#define PCMK_XA_PROMOTED_NODE_MAX_LEGACY "master-node-max"

//! \deprecated Do not use
#define PCMK_XE_PROMOTED_NODE_MAX_LEGACY PCMK_XA_PROMOTED_NODE_MAX_LEGACY

//! \deprecated Do not use
#define XML_RSC_ATTR_MASTER_NODEMAX PCMK_XA_PROMOTED_NODE_MAX_LEGACY

//! \deprecated Do not use
#define PCMK_META_MIGRATION_THRESHOLD "migration-threshold"

//! \deprecated Do not use
#define XML_RSC_ATTR_FAIL_STICKINESS PCMK_META_MIGRATION_THRESHOLD

//! \deprecated Do not use
#define PCMK_META_FAILURE_TIMEOUT "failure-timeout"

//! \deprecated Do not use
#define XML_RSC_ATTR_FAIL_TIMEOUT PCMK_META_FAILURE_TIMEOUT

//! \deprecated Do not use (will be removed in a future release)
#define XML_ATTR_RA_VERSION "ra-version"

//! \deprecated Do not use (will be removed in a future release)
#define XML_TAG_FRAGMENT "cib_fragment"

//! \deprecated Do not use (will be removed in a future release)
#define XML_TAG_RSC_VER_ATTRS "rsc_versioned_attrs"

//! \deprecated Do not use (will be removed in a future release)
#define XML_TAG_OP_VER_ATTRS "op_versioned_attrs"

//! \deprecated Do not use (will be removed in a future release)
#define XML_TAG_OP_VER_META "op_versioned_meta"

//! \deprecated Use \p XML_ATTR_ID instead
#define XML_ATTR_UUID "id"

//! \deprecated Do not use (will be removed in a future release)
#define XML_ATTR_VERBOSE "verbose"

//! \deprecated Do not use (will be removed in a future release)
#define XML_CIB_TAG_DOMAINS "domains"

//! \deprecated Do not use (will be removed in a future release)
#define XML_CIB_ATTR_SOURCE "source"

//! \deprecated Do not use
#define XML_NODE_EXPECTED "expected"

//! \deprecated Do not use
#define XML_NODE_IN_CLUSTER "in_ccm"

//! \deprecated Do not use
#define XML_NODE_IS_PEER "crmd"

//! \deprecated Do not use
#define XML_NODE_JOIN_STATE "join"

//! \deprecated Do not use (will be removed in a future release)
#define XML_RSC_OP_LAST_RUN "last-run"

//! \deprecated Use name member directly
#define TYPE(x) (((x) == NULL)? NULL : (const char *) ((x)->name))

//! \deprecated Do not use
#define PCMK_META_ENABLED "enabled"

//! \deprecated Do not use
#define XML_RSC_ATTR_TARGET "container-attribute-target"

//! \deprecated Do not use
#define XML_RSC_ATTR_RESTART "restart-type"

//! \deprecated Do not use
#define XML_RSC_ATTR_ORDERED "ordered"

//! \deprecated Do not use
#define XML_RSC_ATTR_INTERLEAVE "interleave"

//! \deprecated Do not use
#define XML_RSC_ATTR_INCARNATION "clone"

//! \deprecated Do not use
#define XML_RSC_ATTR_PROMOTABLE "promotable"

//! \deprecated Do not use
#define XML_RSC_ATTR_MANAGED "is-managed"

//! \deprecated Do not use
#define XML_RSC_ATTR_TARGET_ROLE "target-role"

//! \deprecated Do not use
#define XML_RSC_ATTR_UNIQUE "globally-unique"

//! \deprecated Do not use
#define XML_RSC_ATTR_NOTIFY "notify"

//! \deprecated Do not use
#define XML_RSC_ATTR_STICKINESS "resource-stickiness"

//! \deprecated Do not use
#define XML_RSC_ATTR_MULTIPLE "multiple-active"

//! \deprecated Do not use
#define XML_RSC_ATTR_REQUIRES "requires"

//! \deprecated Do not use
#define XML_RSC_ATTR_CONTAINER "container"

//! \deprecated Do not use
#define XML_RSC_ATTR_INTERNAL_RSC "internal_rsc"

//! \deprecated Do not use
#define XML_RSC_ATTR_MAINTENANCE "maintenance"

//! \deprecated Do not use
#define XML_RSC_ATTR_REMOTE_NODE "remote-node"

//! \deprecated Do not use
#define XML_RSC_ATTR_CRITICAL "critical"

//! \deprecated Do not use
#define XML_OP_ATTR_ALLOW_MIGRATE "allow-migrate"

//! \deprecated Use \c XML_BOOLEAN_TRUE instead
#define XML_BOOLEAN_YES XML_BOOLEAN_TRUE

//! \deprecated Use \c XML_BOOLEAN_FALSE instead
#define XML_BOOLEAN_NO XML_BOOLEAN_FALSE

//! \deprecated Do not use
#define XML_RSC_ATTR_REMOTE_RA_ADDR "addr"

//! \deprecated Do not use
#define XML_RSC_ATTR_REMOTE_RA_SERVER "server"

#ifdef __cplusplus
}
#endif

#endif // PCMK__CRM_MSG_XML_COMPAT__H
