/*
 * Copyright 2008-2023 the Pacemaker project contributors
 *
 * The version control history for this file may have further details.
 *
 * This source code is licensed under the GNU General Public License version 2
 * or later (GPLv2+) WITHOUT ANY WARRANTY.
 */

#include <crm_internal.h>

#include <sys/param.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>

#include <crm/crm.h>
#include <crm/cib.h>
#include <crm/msg_xml.h>
#include <crm/common/ipc.h>
#include <crm/cluster.h>

#include <crm/common/xml.h>

#include <pacemaker-based.h>

gboolean stand_alone = FALSE;

extern int cib_perform_command(xmlNode * request, xmlNode ** reply, xmlNode ** cib_diff,
                               gboolean privileged);

static xmlNode *
cib_prepare_common(xmlNode * root, const char *section)
{
    xmlNode *data = NULL;

    /* extract the CIB from the fragment */
    if (root == NULL) {
        return NULL;

    } else if (pcmk__strcase_any_of(crm_element_name(root), XML_TAG_FRAGMENT,
                                    F_CRM_DATA, F_CIB_CALLDATA, NULL)) {
        data = first_named_child(root, XML_TAG_CIB);

    } else {
        data = root;
    }

    /* grab the section specified for the command */
    if (section != NULL && data != NULL && pcmk__str_eq(crm_element_name(data), XML_TAG_CIB, pcmk__str_none)) {
        data = pcmk_find_cib_element(data, section);
    }

    /* crm_log_xml_trace(root, "cib:input"); */
    return data;
}

static int
cib_prepare_none(xmlNode * request, xmlNode ** data, const char **section)
{
    *data = NULL;
    *section = crm_element_value(request, F_CIB_SECTION);
    return pcmk_ok;
}

static int
cib_prepare_data(xmlNode * request, xmlNode ** data, const char **section)
{
    xmlNode *input_fragment = get_message_xml(request, F_CIB_CALLDATA);

    *section = crm_element_value(request, F_CIB_SECTION);
    *data = cib_prepare_common(input_fragment, *section);
    /* crm_log_xml_debug(*data, "data"); */
    return pcmk_ok;
}

static int
cib_prepare_sync(xmlNode * request, xmlNode ** data, const char **section)
{
    *data = NULL;
    *section = crm_element_value(request, F_CIB_SECTION);
    return pcmk_ok;
}

static int
cib_prepare_diff(xmlNode * request, xmlNode ** data, const char **section)
{
    xmlNode *input_fragment = NULL;

    *data = NULL;
    *section = NULL;

    if (pcmk__xe_attr_is_true(request, F_CIB_GLOBAL_UPDATE)) {
        input_fragment = get_message_xml(request, F_CIB_UPDATE_DIFF);
    } else {
        input_fragment = get_message_xml(request, F_CIB_CALLDATA);
    }

    CRM_CHECK(input_fragment != NULL, crm_log_xml_warn(request, "no input"));
    *data = cib_prepare_common(input_fragment, NULL);
    return pcmk_ok;
}

static int
cib_cleanup_query(int options, xmlNode ** data, xmlNode ** output)
{
    CRM_LOG_ASSERT(*data == NULL);
    if ((options & cib_no_children)
        || pcmk__str_eq(crm_element_name(*output), "xpath-query", pcmk__str_casei)) {
        free_xml(*output);
    }
    return pcmk_ok;
}

static int
cib_cleanup_data(int options, xmlNode ** data, xmlNode ** output)
{
    free_xml(*output);
    *data = NULL;
    return pcmk_ok;
}

static int
cib_cleanup_output(int options, xmlNode ** data, xmlNode ** output)
{
    free_xml(*output);
    return pcmk_ok;
}

static int
cib_cleanup_none(int options, xmlNode ** data, xmlNode ** output)
{
    CRM_LOG_ASSERT(*data == NULL);
    CRM_LOG_ASSERT(*output == NULL);
    return pcmk_ok;
}

static cib_operation_t cib_server_ops[] = {
    // Booleans are modifies_cib, needs_privileges, needs_quorum
    {
        NULL, FALSE, FALSE, FALSE,
        cib_prepare_none, cib_cleanup_none, cib_process_default
    },
    {
        PCMK__CIB_REQUEST_QUERY, FALSE, FALSE, FALSE,
        cib_prepare_none, cib_cleanup_query, cib_process_query
    },
    {
        PCMK__CIB_REQUEST_MODIFY, TRUE, TRUE, TRUE,
        cib_prepare_data, cib_cleanup_data, cib_process_modify
    },
    {
        PCMK__CIB_REQUEST_APPLY_PATCH, TRUE, TRUE, TRUE,
        cib_prepare_diff, cib_cleanup_data, cib_server_process_diff
    },
    {
        PCMK__CIB_REQUEST_REPLACE, TRUE, TRUE, TRUE,
        cib_prepare_data, cib_cleanup_data, cib_process_replace_svr
    },
    {
        PCMK__CIB_REQUEST_CREATE, TRUE, TRUE, TRUE,
        cib_prepare_data, cib_cleanup_data, cib_process_create
    },
    {
        PCMK__CIB_REQUEST_DELETE, TRUE, TRUE, TRUE,
        cib_prepare_data, cib_cleanup_data, cib_process_delete
    },
    {
        PCMK__CIB_REQUEST_SYNC_TO_ALL, FALSE, TRUE, FALSE,
        cib_prepare_sync, cib_cleanup_none, cib_process_sync
    },
    {
        PCMK__CIB_REQUEST_BUMP, TRUE, TRUE, TRUE,
        cib_prepare_none, cib_cleanup_output, cib_process_bump
    },
    {
        PCMK__CIB_REQUEST_ERASE, TRUE, TRUE, TRUE,
        cib_prepare_none, cib_cleanup_output, cib_process_erase
    },
    {
        PCMK__CIB_REQUEST_NOOP, FALSE, FALSE, FALSE,
        cib_prepare_none, cib_cleanup_none, cib_process_default
    },
    {
        PCMK__CIB_REQUEST_ABS_DELETE, TRUE, TRUE, TRUE,
        cib_prepare_data, cib_cleanup_data, cib_process_delete_absolute
    },
    {
        PCMK__CIB_REQUEST_UPGRADE, TRUE, TRUE, TRUE,
        cib_prepare_none, cib_cleanup_output, cib_process_upgrade_server
    },
    {
        PCMK__CIB_REQUEST_SECONDARY, FALSE, TRUE, FALSE,
        cib_prepare_none, cib_cleanup_none, cib_process_readwrite
    },
    {
        PCMK__CIB_REQUEST_ALL_SECONDARY, FALSE, TRUE, FALSE,
        cib_prepare_none, cib_cleanup_none, cib_process_readwrite
    },
    {
        PCMK__CIB_REQUEST_SYNC_TO_ONE, FALSE, TRUE, FALSE,
        cib_prepare_sync, cib_cleanup_none, cib_process_sync_one
    },
    {
        PCMK__CIB_REQUEST_PRIMARY, TRUE, TRUE, FALSE,
        cib_prepare_data, cib_cleanup_data, cib_process_readwrite
    },
    {
        PCMK__CIB_REQUEST_IS_PRIMARY, FALSE, TRUE, FALSE,
        cib_prepare_none, cib_cleanup_none, cib_process_readwrite
    },
    {
        PCMK__CIB_REQUEST_SHUTDOWN, FALSE, TRUE, FALSE,
        cib_prepare_sync, cib_cleanup_none, cib_process_shutdown_req
    },
    {
        CRM_OP_PING, FALSE, FALSE, FALSE,
        cib_prepare_none, cib_cleanup_output, cib_process_ping
    },
    {
        PCMK__CIB_REQUEST_CLUSTER_LOCK, FALSE, TRUE, FALSE,
        cib_prepare_none, cib_cleanup_none, cib_process_lock
    },
    {
        PCMK__CIB_REQUEST_CLUSTER_UNLOCK, TRUE, TRUE, FALSE,
        cib_prepare_none, cib_cleanup_none, cib_process_lock
    },
};

int
cib_get_operation_id(const char *op, int *operation)
{
    static GHashTable *operation_hash = NULL;

    if (operation_hash == NULL) {
        int lpc = 0;
        int max_msg_types = PCMK__NELEM(cib_server_ops);

        operation_hash = pcmk__strkey_table(NULL, free);
        for (lpc = 1; lpc < max_msg_types; lpc++) {
            int *value = malloc(sizeof(int));

            if(value) {
                *value = lpc;
                g_hash_table_insert(operation_hash, (gpointer) cib_server_ops[lpc].operation, value);
            }
        }
    }

    if (op != NULL) {
        int *value = g_hash_table_lookup(operation_hash, op);

        if (value) {
            *operation = *value;
            return pcmk_ok;
        }
    }
    crm_err("Operation %s is not valid", op);
    *operation = -1;
    return -EINVAL;
}

xmlNode *
cib_msg_copy(xmlNode * msg, gboolean with_data)
{
    int lpc = 0;
    const char *field = NULL;
    const char *value = NULL;
    xmlNode *value_struct = NULL;

    static const char *field_list[] = {
        F_XML_TAGNAME,
        F_TYPE,
        F_CIB_CLIENTID,
        F_CIB_CALLOPTS,
        F_CIB_CALLID,
        F_CIB_OPERATION,
        F_CIB_ISREPLY,
        F_CIB_SECTION,
        F_CIB_HOST,
        F_CIB_RC,
        F_CIB_DELEGATED,
        F_CIB_OBJID,
        F_CIB_OBJTYPE,
        F_CIB_EXISTING,
        F_CIB_SEENCOUNT,
        F_CIB_TIMEOUT,
        F_CIB_CALLBACK_TOKEN,
        F_CIB_GLOBAL_UPDATE,
        F_CIB_CLIENTNAME,
        F_CIB_USER,
        F_CIB_NOTIFY_TYPE,
        F_CIB_NOTIFY_ACTIVATE
    };

    static const char *data_list[] = {
        F_CIB_CALLDATA,
        F_CIB_UPDATE,
        F_CIB_UPDATE_RESULT
    };

    xmlNode *copy = create_xml_node(NULL, "copy");

    CRM_ASSERT(copy != NULL);

    for (lpc = 0; lpc < PCMK__NELEM(field_list); lpc++) {
        field = field_list[lpc];
        value = crm_element_value(msg, field);
        if (value != NULL) {
            crm_xml_add(copy, field, value);
        }
    }
    for (lpc = 0; with_data && lpc < PCMK__NELEM(data_list); lpc++) {
        field = data_list[lpc];
        value_struct = get_message_xml(msg, field);
        if (value_struct != NULL) {
            add_message_xml(copy, field, value_struct);
        }
    }

    return copy;
}

cib_op_t *
cib_op_func(int call_type)
{
    return &(cib_server_ops[call_type].fn);
}

gboolean
cib_op_modifies(int call_type)
{
    return cib_server_ops[call_type].modifies_cib;
}

int
cib_op_can_run(int call_type, int call_options, gboolean privileged, gboolean global_update)
{
    if (privileged == FALSE && cib_server_ops[call_type].needs_privileges) {
        /* abort */
        return -EACCES;
    }
#if 0
    if (rc == pcmk_ok
        && stand_alone == FALSE
        && global_update == FALSE
        && (call_options & cib_quorum_override) == 0 && cib_server_ops[call_type].needs_quorum) {
        return -pcmk_err_no_quorum;
    }
#endif
    return pcmk_ok;
}

int
cib_op_prepare(int call_type, xmlNode * request, xmlNode ** input, const char **section)
{
    crm_trace("Prepare %d", call_type);
    return cib_server_ops[call_type].prepare(request, input, section);
}

int
cib_op_cleanup(int call_type, int options, xmlNode ** input, xmlNode ** output)
{
    crm_trace("Cleanup %d", call_type);
    return cib_server_ops[call_type].cleanup(options, input, output);
}
