/*
 * Copyright 2004-2023 the Pacemaker project contributors
 *
 * The version control history for this file may have further details.
 *
 * This source code is licensed under the GNU Lesser General Public License
 * version 2.1 or later (LGPLv2.1+) WITHOUT ANY WARRANTY.
 */

#include <crm_internal.h>
#include <crm/crm.h>
#include <crm/msg_xml.h>
#include <crm/common/xml.h>
#include <crm/common/util.h>

#include <glib.h>

#include <crm/common/scheduler_internal.h>
#include <crm/pengine/internal.h>

static bool
check_placement_strategy(const char *value)
{
    return pcmk__strcase_any_of(value, "default", "utilization", "minimal",
                           "balanced", NULL);
}

void
pe_metadata(pcmk__output_t *out)
{
    const char *desc_short = "Pacemaker scheduler options";
    const char *desc_long = "Cluster options used by Pacemaker's scheduler";

    gchar *s = pcmk__format_option_metadata("pacemaker-schedulerd", desc_short,
                                            desc_long,
                                            pcmk__opt_context_schedulerd);
    out->output_xml(out, "metadata", s);
    g_free(s);
}

void
verify_pe_options(GHashTable * options)
{
    pcmk__validate_cluster_options(options);
}

const char *
pe_pref(GHashTable * options, const char *name)
{
    return pcmk__cluster_option(options, name);
}

const char *
fail2text(enum action_fail_response fail)
{
    const char *result = "<unknown>";

    switch (fail) {
        case pcmk_on_fail_ignore:
            result = "ignore";
            break;
        case pcmk_on_fail_demote:
            result = "demote";
            break;
        case pcmk_on_fail_block:
            result = "block";
            break;
        case pcmk_on_fail_restart:
            result = "recover";
            break;
        case pcmk_on_fail_ban:
            result = "migrate";
            break;
        case pcmk_on_fail_stop:
            result = "stop";
            break;
        case pcmk_on_fail_fence_node:
            result = "fence";
            break;
        case pcmk_on_fail_standby_node:
            result = "standby";
            break;
        case pcmk_on_fail_restart_container:
            result = "restart-container";
            break;
        case pcmk_on_fail_reset_remote:
            result = "reset-remote";
            break;
    }
    return result;
}

enum action_tasks
text2task(const char *task)
{
    if (pcmk__str_eq(task, PCMK_ACTION_STOP, pcmk__str_casei)) {
        return pcmk_action_stop;

    } else if (pcmk__str_eq(task, PCMK_ACTION_STOPPED, pcmk__str_casei)) {
        return pcmk_action_stopped;

    } else if (pcmk__str_eq(task, PCMK_ACTION_START, pcmk__str_casei)) {
        return pcmk_action_start;

    } else if (pcmk__str_eq(task, PCMK_ACTION_RUNNING, pcmk__str_casei)) {
        return pcmk_action_started;

    } else if (pcmk__str_eq(task, PCMK_ACTION_DO_SHUTDOWN, pcmk__str_casei)) {
        return pcmk_action_shutdown;

    } else if (pcmk__str_eq(task, PCMK_ACTION_STONITH, pcmk__str_casei)) {
        return pcmk_action_fence;

    } else if (pcmk__str_eq(task, PCMK_ACTION_MONITOR, pcmk__str_casei)) {
        return pcmk_action_monitor;

    } else if (pcmk__str_eq(task, PCMK_ACTION_NOTIFY, pcmk__str_casei)) {
        return pcmk_action_notify;

    } else if (pcmk__str_eq(task, PCMK_ACTION_NOTIFIED, pcmk__str_casei)) {
        return pcmk_action_notified;

    } else if (pcmk__str_eq(task, PCMK_ACTION_PROMOTE, pcmk__str_casei)) {
        return pcmk_action_promote;

    } else if (pcmk__str_eq(task, PCMK_ACTION_DEMOTE, pcmk__str_casei)) {
        return pcmk_action_demote;

    } else if (pcmk__str_eq(task, PCMK_ACTION_PROMOTED, pcmk__str_casei)) {
        return pcmk_action_promoted;

    } else if (pcmk__str_eq(task, PCMK_ACTION_DEMOTED, pcmk__str_casei)) {
        return pcmk_action_demoted;
    }
    return pcmk_action_unspecified;
}

const char *
task2text(enum action_tasks task)
{
    const char *result = "<unknown>";

    switch (task) {
        case pcmk_action_unspecified:
            result = "no_action";
            break;
        case pcmk_action_stop:
            result = PCMK_ACTION_STOP;
            break;
        case pcmk_action_stopped:
            result = PCMK_ACTION_STOPPED;
            break;
        case pcmk_action_start:
            result = PCMK_ACTION_START;
            break;
        case pcmk_action_started:
            result = PCMK_ACTION_RUNNING;
            break;
        case pcmk_action_shutdown:
            result = PCMK_ACTION_DO_SHUTDOWN;
            break;
        case pcmk_action_fence:
            result = PCMK_ACTION_STONITH;
            break;
        case pcmk_action_monitor:
            result = PCMK_ACTION_MONITOR;
            break;
        case pcmk_action_notify:
            result = PCMK_ACTION_NOTIFY;
            break;
        case pcmk_action_notified:
            result = PCMK_ACTION_NOTIFIED;
            break;
        case pcmk_action_promote:
            result = PCMK_ACTION_PROMOTE;
            break;
        case pcmk_action_promoted:
            result = PCMK_ACTION_PROMOTED;
            break;
        case pcmk_action_demote:
            result = PCMK_ACTION_DEMOTE;
            break;
        case pcmk_action_demoted:
            result = PCMK_ACTION_DEMOTED;
            break;
    }

    return result;
}

const char *
role2text(enum rsc_role_e role)
{
    switch (role) {
        case pcmk_role_stopped:
            return PCMK__ROLE_STOPPED;

        case pcmk_role_started:
            return PCMK__ROLE_STARTED;

        case pcmk_role_unpromoted:
#ifdef PCMK__COMPAT_2_0
            return PCMK__ROLE_UNPROMOTED_LEGACY;
#else
            return PCMK__ROLE_UNPROMOTED;
#endif

        case pcmk_role_promoted:
#ifdef PCMK__COMPAT_2_0
            return PCMK__ROLE_PROMOTED_LEGACY;
#else
            return PCMK__ROLE_PROMOTED;
#endif

        default: // pcmk_role_unknown
            return PCMK__ROLE_UNKNOWN;
    }
}

enum rsc_role_e
text2role(const char *role)
{
    if (pcmk__str_eq(role, PCMK__ROLE_UNKNOWN,
                     pcmk__str_casei|pcmk__str_null_matches)) {
        return pcmk_role_unknown;
    } else if (pcmk__str_eq(role, PCMK__ROLE_STOPPED, pcmk__str_casei)) {
        return pcmk_role_stopped;
    } else if (pcmk__str_eq(role, PCMK__ROLE_STARTED, pcmk__str_casei)) {
        return pcmk_role_started;
    } else if (pcmk__strcase_any_of(role, PCMK__ROLE_UNPROMOTED,
                                    PCMK__ROLE_UNPROMOTED_LEGACY, NULL)) {
        return pcmk_role_unpromoted;
    } else if (pcmk__strcase_any_of(role, PCMK__ROLE_PROMOTED,
                                    PCMK__ROLE_PROMOTED_LEGACY, NULL)) {
        return pcmk_role_promoted;
    }
    return pcmk_role_unknown; // Invalid role given
}

void
add_hash_param(GHashTable * hash, const char *name, const char *value)
{
    CRM_CHECK(hash != NULL, return);

    /* @TODO Either overwrite existing value or update trace message to reflect
     * that we don't
     */
    crm_trace("Adding name='%s' value='%s' to hash table",
              pcmk__s(name, "<null>"), pcmk__s(value, "<null>"));

    if (name == NULL || value == NULL) {
        return;
    }

    if (g_hash_table_lookup(hash, name) == NULL) {
        g_hash_table_insert(hash, strdup(name), strdup(value));
    }
}

/*!
 * \internal
 * \brief Look up an attribute value on the appropriate node
 *
 * If \p node is a guest node and either the \c PCMK__META_CONTAINER_ATTR_TARGET
 * meta attribute is set to "host" for \p rsc or \p force_host is \c true, query
 * the attribute on the node's host. Otherwise, query the attribute on \p node
 * itself.
 *
 * \param[in] node        Node to query attribute value on by default
 * \param[in] name        Name of attribute to query
 * \param[in] rsc         Resource on whose behalf we're querying
 * \param[in] node_type   Type of resource location lookup
 * \param[in] force_host  Force a lookup on the guest node's host, regardless of
 *                        the \c PCMK__META_CONTAINER_ATTR_TARGET value
 *
 * \return Value of the attribute on \p node or on the host of \p node
 *
 * \note If \p force_host is \c true, \p node \e must be a guest node.
 */
const char *
pe__node_attribute_calculated(const pcmk_node_t *node, const char *name,
                              const pcmk_resource_t *rsc,
                              enum pcmk__rsc_node node_type,
                              bool force_host)
{
    // @TODO: Use pe__is_guest_node() after merging libpe_{rules,status}
    bool is_guest = (node != NULL)
                    && (node->details->type == pcmk_node_variant_remote)
                    && (node->details->remote_rsc != NULL)
                    && (node->details->remote_rsc->container != NULL);
    const char *source = NULL;
    const char *node_type_s = NULL;
    const char *reason = NULL;

    const pcmk_resource_t *container = NULL;
    const pcmk_node_t *host = NULL;

    CRM_ASSERT((node != NULL) && (name != NULL) && (rsc != NULL)
               && (!force_host || is_guest));

    /* Ignore PCMK__META_CONTAINER_ATTR_TARGET if node is not a guest node. This
     * represents a user configuration error.
     */
    source = g_hash_table_lookup(rsc->meta, PCMK__META_CONTAINER_ATTR_TARGET);
    if (!force_host
        && (!is_guest || !pcmk__str_eq(source, "host", pcmk__str_casei))) {

        return g_hash_table_lookup(node->details->attrs, name);
    }

    container = node->details->remote_rsc->container;

    switch (node_type) {
        case pcmk__rsc_node_assigned:
            node_type_s = "assigned";
            host = container->allocated_to;
            if (host == NULL) {
                reason = "not assigned";
            }
            break;

        case pcmk__rsc_node_current:
            node_type_s = "current";

            if (container->running_on != NULL) {
                host = container->running_on->data;
            }
            if (host == NULL) {
                reason = "inactive";
            }
            break;

        default:
            // Add support for other enum pcmk__rsc_node values if needed
            CRM_ASSERT(false);
            break;
    }

    if (host != NULL) {
        const char *value = g_hash_table_lookup(host->details->attrs, name);

        pcmk__rsc_trace(rsc,
                        "%s: Value lookup for %s on %s container host %s %s%s",
                        rsc->id, name, node_type_s, pe__node_name(host),
                        ((value != NULL)? "succeeded: " : "failed"),
                        pcmk__s(value, ""));
        return value;
    }
    pcmk__rsc_trace(rsc,
                    "%s: Not looking for %s on %s container host: %s is %s",
                    rsc->id, name, node_type_s, container->id, reason);
    return NULL;
}

const char *
pe_node_attribute_raw(const pcmk_node_t *node, const char *name)
{
    if(node == NULL) {
        return NULL;
    }
    return g_hash_table_lookup(node->details->attrs, name);
}
