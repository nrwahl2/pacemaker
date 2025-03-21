/*
 * Copyright 2004-2025 the Pacemaker project contributors
 *
 * The version control history for this file may have further details.
 *
 * This source code is licensed under the GNU General Public License version 2
 * or later (GPLv2+) WITHOUT ANY WARRANTY.
 */

#include <crm_internal.h>

#include <crm_resource.h>
#include <crm/lrmd_internal.h>
#include <crm/common/cmdline_internal.h>
#include <crm/common/ipc_attrd_internal.h>
#include <crm/common/lists_internal.h>
#include <crm/common/output.h>
#include <pacemaker-internal.h>

#include <sys/param.h>
#include <stdbool.h>                        // bool, true, false
#include <stdint.h>                         // uint32_t
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <time.h>

#include <crm/crm.h>
#include <crm/stonith-ng.h>
#include <crm/common/ipc_controld.h>
#include <crm/cib/internal.h>

#define SUMMARY "crm_resource - perform tasks related to Pacemaker cluster resources"

enum rsc_command {
    cmd_none = 0,           // No command option given (yet)
    cmd_ban,
    cmd_cleanup,
    cmd_clear,
    cmd_colocations,
    cmd_cts,
    cmd_delete,
    cmd_delete_param,
    cmd_digests,
    cmd_execute_agent,
    cmd_fail,
    cmd_get_param,
    cmd_list_active_ops,
    cmd_list_agents,
    cmd_list_all_ops,
    cmd_list_alternatives,
    cmd_list_instances,
    cmd_list_options,
    cmd_list_providers,
    cmd_list_resources,
    cmd_list_standards,
    cmd_locate,
    cmd_metadata,
    cmd_move,
    cmd_query_xml,
    cmd_query_xml_raw,
    cmd_refresh,
    cmd_restart,
    cmd_set_param,
    cmd_wait,
    cmd_why,
};

struct {
    enum rsc_command rsc_cmd;     // crm_resource command to perform

    // Command-line option values
    gchar *rsc_id;                // Value of --resource
    gchar *rsc_type;              // Value of --resource-type
    gboolean all;                 // --all was given
    gboolean force;               // --force was given
    gboolean clear_expired;       // --expired was given
    gboolean recursive;           // --recursive was given
    gboolean promoted_role_only;  // --promoted was given
    gchar *host_uname;            // Value of --node
    gchar *interval_spec;         // Value of --interval
    gchar *move_lifetime;         // Value of --lifetime
    gchar *operation;             // Value of --operation
    enum pcmk__opt_flags opt_list;  // Parsed from --list-options
    const char *attr_set_type;    // Instance, meta, utilization, or element attribute
    gchar *prop_id;               // --nvpair (attribute XML ID)
    char *prop_name;              // Attribute name
    gchar *prop_set;              // --set-name (attribute block XML ID)
    gchar *prop_value;            // --parameter-value (attribute value)
    guint timeout_ms;             // Parsed from --timeout value
    char *agent_spec;             // Standard and/or provider and/or agent
    int check_level;              // Optional value of --validate or --force-check

    // Resource configuration specified via command-line arguments
    bool cmdline_config;          // Resource configuration was via arguments
    char *v_agent;                // Value of --agent
    char *v_class;                // Value of --class
    char *v_provider;             // Value of --provider
    GHashTable *cmdline_params;   // Resource parameters specified

    // Positional command-line arguments
    gchar **remainder;            // Positional arguments as given
    GHashTable *override_params;  // Resource parameter values that override config
} options = {
    .attr_set_type = PCMK_XE_INSTANCE_ATTRIBUTES,
    .check_level = -1,
    .rsc_cmd = cmd_list_resources,  // List all resources if no command given
};

static crm_exit_t exit_code = CRM_EX_OK;
static pcmk__output_t *out = NULL;
static pcmk__common_args_t *args = NULL;

// Things that should be cleaned up on exit
static GError *error = NULL;
static GMainLoop *mainloop = NULL;
static cib_t *cib_conn = NULL;
static pcmk_ipc_api_t *controld_api = NULL;
static pcmk_scheduler_t *scheduler = NULL;

#define MESSAGE_TIMEOUT_S 60

#define INDENT "                                    "

static pcmk__supported_format_t formats[] = {
    PCMK__SUPPORTED_FORMAT_NONE,
    PCMK__SUPPORTED_FORMAT_TEXT,
    PCMK__SUPPORTED_FORMAT_XML,
    { NULL, NULL, NULL }
};

static void
quit_main_loop(crm_exit_t ec)
{
    exit_code = ec;
    if (mainloop != NULL) {
        GMainLoop *mloop = mainloop;

        mainloop = NULL; // Don't re-enter this block
        pcmk_quit_main_loop(mloop, 10);
        g_main_loop_unref(mloop);
    }
}

static gboolean
resource_ipc_timeout(gpointer data)
{
    // Start with newline because "Waiting for ..." message doesn't have one
    if (error != NULL) {
        g_clear_error(&error);
    }

    g_set_error(&error, PCMK__EXITC_ERROR, CRM_EX_TIMEOUT,
                _("Aborting because no messages received in %d seconds"), MESSAGE_TIMEOUT_S);

    quit_main_loop(CRM_EX_TIMEOUT);
    return FALSE;
}

static void
controller_event_callback(pcmk_ipc_api_t *api, enum pcmk_ipc_event event_type,
                          crm_exit_t status, void *event_data, void *user_data)
{
    switch (event_type) {
        case pcmk_ipc_event_disconnect:
            if (exit_code == CRM_EX_DISCONNECT) { // Unexpected
                crm_info("Connection to controller was terminated");
            }
            quit_main_loop(exit_code);
            break;

        case pcmk_ipc_event_reply:
            if (status != CRM_EX_OK) {
                out->err(out, "Error: bad reply from controller: %s",
                         crm_exit_str(status));
                pcmk_disconnect_ipc(api);
                quit_main_loop(status);
            } else {
                if ((pcmk_controld_api_replies_expected(api) == 0)
                    && mainloop && g_main_loop_is_running(mainloop)) {
                    out->info(out, "... got reply (done)");
                    crm_debug("Got all the replies we expected");
                    pcmk_disconnect_ipc(api);
                    quit_main_loop(CRM_EX_OK);
                } else {
                    out->info(out, "... got reply");
                }
            }
            break;

        default:
            break;
    }
}

static void
start_mainloop(pcmk_ipc_api_t *capi)
{
    unsigned int count = pcmk_controld_api_replies_expected(capi);

    if (count > 0) {
        out->info(out, "Waiting for %u %s from the controller",
                  count, pcmk__plural_alt(count, "reply", "replies"));
        exit_code = CRM_EX_DISCONNECT; // For unexpected disconnects
        mainloop = g_main_loop_new(NULL, FALSE);
        pcmk__create_timer(MESSAGE_TIMEOUT_S * 1000, resource_ipc_timeout, NULL);
        g_main_loop_run(mainloop);
    }
}

static int
compare_id(gconstpointer a, gconstpointer b)
{
    return strcmp((const char *)a, (const char *)b);
}

static GList *
build_constraint_list(xmlNode *root)
{
    GList *retval = NULL;
    xmlNode *cib_constraints = NULL;
    xmlXPathObjectPtr xpathObj = NULL;
    int ndx = 0;

    cib_constraints = pcmk_find_cib_element(root, PCMK_XE_CONSTRAINTS);
    xpathObj = xpath_search(cib_constraints, "//" PCMK_XE_RSC_LOCATION);

    for (ndx = 0; ndx < numXpathResults(xpathObj); ndx++) {
        xmlNode *match = getXpathResult(xpathObj, ndx);
        retval = g_list_insert_sorted(retval, (gpointer) pcmk__xe_id(match),
                                      compare_id);
    }

    freeXpathObject(xpathObj);
    return retval;
}

static gboolean
validate_opt_list(const gchar *optarg)
{
    if (pcmk__str_eq(optarg, PCMK_VALUE_FENCING, pcmk__str_none)) {
        options.opt_list = pcmk__opt_fencing;

    } else if (pcmk__str_eq(optarg, PCMK__VALUE_PRIMITIVE, pcmk__str_none)) {
        options.opt_list = pcmk__opt_primitive;

    } else {
        return FALSE;
    }

    return TRUE;
}

// GOptionArgFunc callback functions

static gboolean
attr_set_type_cb(const gchar *option_name, const gchar *optarg, gpointer data,
                 GError **error) {
    if (pcmk__str_any_of(option_name, "-m", "--meta", NULL)) {
        options.attr_set_type = PCMK_XE_META_ATTRIBUTES;
    } else if (pcmk__str_any_of(option_name, "-z", "--utilization", NULL)) {
        options.attr_set_type = PCMK_XE_UTILIZATION;
    } else if (pcmk__str_eq(option_name, "--element", pcmk__str_none)) {
        options.attr_set_type = ATTR_SET_ELEMENT;
    }
    return TRUE;
}

static gboolean
cmdline_config_cb(const gchar *option_name, const gchar *optarg, gpointer data,
                  GError **error)
{
    options.cmdline_config = true;

    if (pcmk__str_eq(option_name, "--class", pcmk__str_none)) {
        pcmk__str_update(&options.v_class, optarg);

    } else if (pcmk__str_eq(option_name, "--provider", pcmk__str_none)) {
        pcmk__str_update(&options.v_provider, optarg);

    } else {    // --agent
        pcmk__str_update(&options.v_agent, optarg);
    }
    return TRUE;
}

/*!
 * \internal
 * \brief Process options that set the command
 *
 * Nothing else should set \c options.rsc_cmd.
 *
 * \param[in]  option_name  Name of the option being parsed
 * \param[in]  optarg       Value to be parsed
 * \param[in]  data         Ignored
 * \param[out] error        Where to store recoverable error, if any
 *
 * \return \c TRUE if the option was successfully parsed, or \c FALSE if an
 *         error occurred, in which case \p *error is set
 */
static gboolean
command_cb(const gchar *option_name, const gchar *optarg, gpointer data,
           GError **error)
{
    // Sorted by enum rsc_command name
    if (pcmk__str_any_of(option_name, "-B", "--ban", NULL)) {
        options.rsc_cmd = cmd_ban;

    } else if (pcmk__str_any_of(option_name, "-C", "--cleanup", NULL)) {
        options.rsc_cmd = cmd_cleanup;

    } else if (pcmk__str_any_of(option_name, "-U", "--clear", NULL)) {
        options.rsc_cmd = cmd_clear;

    } else if (pcmk__str_any_of(option_name, "-a", "--constraints", NULL)) {
        options.rsc_cmd = cmd_colocations;

    } else if (pcmk__str_any_of(option_name, "-A", "--stack", NULL)) {
        options.rsc_cmd = cmd_colocations;
        options.recursive = TRUE;

    } else if (pcmk__str_any_of(option_name, "-c", "--list-cts", NULL)) {
        options.rsc_cmd = cmd_cts;

    } else if (pcmk__str_any_of(option_name, "-D", "--delete", NULL)) {
        options.rsc_cmd = cmd_delete;

    } else if (pcmk__str_any_of(option_name, "-d", "--delete-parameter",
                                NULL)) {
        options.rsc_cmd = cmd_delete_param;
        pcmk__str_update(&options.prop_name, optarg);

    } else if (pcmk__str_eq(option_name, "--digests", pcmk__str_none)) {
        options.rsc_cmd = cmd_digests;

        if (options.override_params == NULL) {
            options.override_params = pcmk__strkey_table(g_free, g_free);
        }

    } else if (pcmk__str_any_of(option_name,
                                "--force-demote", "--force-promote",
                                "--force-start", "--force-stop",
                                "--force-check", "--validate", NULL)) {
        options.rsc_cmd = cmd_execute_agent;

        g_free(options.operation);
        options.operation = g_strdup(option_name + 2);  // skip "--"

        if (options.override_params == NULL) {
            options.override_params = pcmk__strkey_table(g_free, g_free);
        }

        if (optarg != NULL) {
            if (pcmk__scan_min_int(optarg, &options.check_level,
                                   0) != pcmk_rc_ok) {
                g_set_error(error, G_OPTION_ERROR, CRM_EX_INVALID_PARAM,
                            _("Invalid check level setting: %s"), optarg);
                return FALSE;
            }
        }

    } else if (pcmk__str_any_of(option_name, "-F", "--fail", NULL)) {
        options.rsc_cmd = cmd_fail;

    } else if (pcmk__str_any_of(option_name, "-g", "--get-parameter", NULL)) {
        options.rsc_cmd = cmd_get_param;
        pcmk__str_update(&options.prop_name, optarg);

    } else if (pcmk__str_any_of(option_name, "-O", "--list-operations", NULL)) {
        options.rsc_cmd = cmd_list_active_ops;

    } else if (pcmk__str_eq(option_name, "--list-agents", pcmk__str_none)) {
        options.rsc_cmd = cmd_list_agents;
        pcmk__str_update(&options.agent_spec, optarg);

    } else if (pcmk__str_any_of(option_name, "-o", "--list-all-operations",
                                NULL)) {
        options.rsc_cmd = cmd_list_all_ops;

    } else if (pcmk__str_eq(option_name, "--list-ocf-alternatives",
                            pcmk__str_none)) {
        options.rsc_cmd = cmd_list_alternatives;
        pcmk__str_update(&options.agent_spec, optarg);

    } else if (pcmk__str_eq(option_name, "--list-options", pcmk__str_none)) {
        options.rsc_cmd = cmd_list_options;
        return validate_opt_list(optarg);

    } else if (pcmk__str_any_of(option_name, "-l", "--list-raw", NULL)) {
        options.rsc_cmd = cmd_list_instances;

    } else if (pcmk__str_eq(option_name, "--list-ocf-providers",
                            pcmk__str_none)) {
        options.rsc_cmd = cmd_list_providers;
        pcmk__str_update(&options.agent_spec, optarg);

    } else if (pcmk__str_any_of(option_name, "-L", "--list", NULL)) {
        options.rsc_cmd = cmd_list_resources;

    } else if (pcmk__str_eq(option_name, "--list-standards", pcmk__str_none)) {
        options.rsc_cmd = cmd_list_standards;

    } else if (pcmk__str_any_of(option_name, "-W", "--locate", NULL)) {
        options.rsc_cmd = cmd_locate;

    } else if (pcmk__str_eq(option_name, "--show-metadata", pcmk__str_none)) {
        options.rsc_cmd = cmd_metadata;
        pcmk__str_update(&options.agent_spec, optarg);

    } else if (pcmk__str_any_of(option_name, "-M", "--move", NULL)) {
        options.rsc_cmd = cmd_move;

    } else if (pcmk__str_any_of(option_name, "-q", "--query-xml", NULL)) {
        options.rsc_cmd = cmd_query_xml;

    } else if (pcmk__str_any_of(option_name, "-w", "--query-xml-raw", NULL)) {
        options.rsc_cmd = cmd_query_xml_raw;

    } else if (pcmk__str_any_of(option_name, "-R", "--refresh", NULL)) {
        options.rsc_cmd = cmd_refresh;

    } else if (pcmk__str_eq(option_name, "--restart", pcmk__str_none)) {
        options.rsc_cmd = cmd_restart;

    } else if (pcmk__str_any_of(option_name, "-p", "--set-parameter", NULL)) {
        options.rsc_cmd = cmd_set_param;
        pcmk__str_update(&options.prop_name, optarg);

    } else if (pcmk__str_eq(option_name, "--wait", pcmk__str_none)) {
        options.rsc_cmd = cmd_wait;

    } else if (pcmk__str_any_of(option_name, "-Y", "--why", NULL)) {
        options.rsc_cmd = cmd_why;
    }

    return TRUE;
}

static gboolean
option_cb(const gchar *option_name, const gchar *optarg, gpointer data,
          GError **error)
{
    gchar *name = NULL;
    gchar *value = NULL;

    if (pcmk__scan_nvpair(optarg, &name, &value) != pcmk_rc_ok) {
        return FALSE;
    }

    /* services__create_resource_action() ultimately takes ownership of
     * options.cmdline_params. It's not worth trying to ensure that the entire
     * call path uses (gchar *) strings and g_free(). So create the table for
     * (char *) strings, and duplicate the (gchar *) strings when inserting.
     */
    if (options.cmdline_params == NULL) {
        options.cmdline_params = pcmk__strkey_table(free, free);
    }
    pcmk__insert_dup(options.cmdline_params, name, value);
    g_free(name);
    g_free(value);
    return TRUE;
}

static gboolean
timeout_cb(const gchar *option_name, const gchar *optarg, gpointer data,
           GError **error)
{
    long long timeout_ms = crm_get_msec(optarg);

    if (timeout_ms < 0) {
        return FALSE;
    }
    options.timeout_ms = (guint) QB_MIN(timeout_ms, UINT_MAX);
    return TRUE;
}

// Command line option specification

/* short option letters still available: eEJkKXyYZ */

static GOptionEntry query_entries[] = {
    { "list", 'L', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, command_cb,
      "List all cluster resources with status",
      NULL },
    { "list-raw", 'l', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, command_cb,
      "List IDs of all instantiated resources (individual members\n"
      INDENT "rather than groups etc.)",
      NULL },
    { "list-cts", 'c', G_OPTION_FLAG_HIDDEN|G_OPTION_FLAG_NO_ARG,
          G_OPTION_ARG_CALLBACK, command_cb,
      NULL,
      NULL },
    { "list-operations", 'O', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
          command_cb,
      "List active resource operations, optionally filtered by\n"
      INDENT "--resource and/or --node",
      NULL },
    { "list-all-operations", 'o', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
          command_cb,
      "List all resource operations, optionally filtered by\n"
      INDENT "--resource and/or --node",
      NULL },
    { "list-options", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, command_cb,
      "List all available options of the given type.\n"
      INDENT "Allowed values:\n"
      INDENT PCMK__VALUE_PRIMITIVE " (primitive resource meta-attributes),\n"
      INDENT PCMK_VALUE_FENCING " (parameters common to all fencing resources)",
      "TYPE" },
    { "list-standards", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
          command_cb,
      "List supported standards",
      NULL },
    { "list-ocf-providers", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
          command_cb,
      "List all available OCF providers",
      NULL },
    { "list-agents", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK,
          command_cb,
      "List all agents available for the named standard and/or provider",
      "STD:PROV" },
    { "list-ocf-alternatives", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK,
          command_cb,
      "List all available providers for the named OCF agent",
      "AGENT" },
    { "show-metadata", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, command_cb,
      "Show the metadata for the named class:provider:agent",
      "SPEC" },
    { "query-xml", 'q', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, command_cb,
      "Show XML configuration of resource (after any template expansion)",
      NULL },
    { "query-xml-raw", 'w', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
          command_cb,
      "Show XML configuration of resource (before any template expansion)",
      NULL },
    { "get-parameter", 'g', G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK,
          command_cb,
      "Display named parameter for resource (use instance attribute\n"
      INDENT "unless --element, --meta, or --utilization is specified)",
      "PARAM" },
    { "locate", 'W', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, command_cb,
      "Show node(s) currently running resource",
      NULL },
    { "constraints", 'a', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
          command_cb,
      "Display the location and colocation constraints that apply to a\n"
      INDENT "resource, and if --recursive is specified, to the resources\n"
      INDENT "directly or indirectly involved in those colocations.\n"
      INDENT "If the named resource is part of a group, or a clone or\n"
      INDENT "bundle instance, constraints for the collective resource\n"
      INDENT "will be shown unless --force is given.",
      NULL },
    { "stack", 'A', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, command_cb,
      "Equivalent to --constraints --recursive",
      NULL },
    { "why", 'Y', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, command_cb,
      "Show why resources are not running, optionally filtered by\n"
      INDENT "--resource and/or --node",
      NULL },

    { NULL }
};

static GOptionEntry command_entries[] = {
    { "validate", 0, G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_CALLBACK,
          command_cb,
      "Validate resource configuration by calling agent's validate-all\n"
      INDENT "action. The configuration may be specified either by giving an\n"
      INDENT "existing resource name with -r, or by specifying --class,\n"
      INDENT "--agent, and --provider arguments, along with any number of\n"
      INDENT "--option arguments. An optional LEVEL argument can be given\n"
      INDENT "to control the level of checking performed.",
      "LEVEL" },
    { "cleanup", 'C', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, command_cb,
      "If resource has any past failures, clear its history and fail\n"
      INDENT "count. Optionally filtered by --resource, --node, --operation\n"
      INDENT "and --interval (otherwise all). --operation and --interval\n"
      INDENT "apply to fail counts, but entire history is always clear, to\n"
      INDENT "allow current state to be rechecked. If the named resource is\n"
      INDENT "part of a group, or one numbered instance of a clone or bundled\n"
      INDENT "resource, the clean-up applies to the whole collective resource\n"
      INDENT "unless --force is given.",
      NULL },
    { "refresh", 'R', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, command_cb,
      "Delete resource's history (including failures) so its current state\n"
      INDENT "is rechecked. Optionally filtered by --resource and --node\n"
      INDENT "(otherwise all). If the named resource is part of a group, or one\n"
      INDENT "numbered instance of a clone or bundled resource, the refresh\n"
      INDENT "applies to the whole collective resource unless --force is given.",
      NULL },
    { "set-parameter", 'p', G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK,
          command_cb,
      "Set named parameter for resource (requires -v). Use instance\n"
      INDENT "attribute unless --element, --meta, or --utilization is "
      "specified.",
      "PARAM" },
    { "delete-parameter", 'd', G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK,
          command_cb,
      "Delete named parameter for resource. Use instance attribute\n"
      INDENT "unless --element, --meta or, --utilization is specified.",
      "PARAM" },

    { NULL }
};

static GOptionEntry location_entries[] = {
    { "move", 'M', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, command_cb,
      "Create a constraint to move resource. If --node is specified,\n"
      INDENT "the constraint will be to move to that node, otherwise it\n"
      INDENT "will be to ban the current node. Unless --force is specified\n"
      INDENT "this will return an error if the resource is already running\n"
      INDENT "on the specified node. If --force is specified, this will\n"
      INDENT "always ban the current node.\n"
      INDENT "Optional: --lifetime, --promoted. NOTE: This may prevent the\n"
      INDENT "resource from running on its previous location until the\n"
      INDENT "implicit constraint expires or is removed with --clear.",
      NULL },
    { "ban", 'B', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, command_cb,
      "Create a constraint to keep resource off a node.\n"
      INDENT "Optional: --node, --lifetime, --promoted.\n"
      INDENT "NOTE: This will prevent the resource from running on the\n"
      INDENT "affected node until the implicit constraint expires or is\n"
      INDENT "removed with --clear. If --node is not specified, it defaults\n"
      INDENT "to the node currently running the resource for primitives\n"
      INDENT "and groups, or the promoted instance of promotable clones with\n"
      INDENT PCMK_META_PROMOTED_MAX "=1 (all other situations result in an\n"
      INDENT "error as there is no sane default).",
      NULL },
    { "clear", 'U', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, command_cb,
      "Remove all constraints created by the --ban and/or --move\n"
      INDENT "commands. Requires: --resource. Optional: --node, --promoted,\n"
      INDENT "--expired. If --node is not specified, all constraints created\n"
      INDENT "by --ban and --move will be removed for the named resource. If\n"
      INDENT "--node and --force are specified, any constraint created by\n"
      INDENT "--move will be cleared, even if it is not for the specified\n"
      INDENT "node. If --expired is specified, only those constraints whose\n"
      INDENT "lifetimes have expired will be removed.",
      NULL },
    { "expired", 'e', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
          &options.clear_expired,
      "Modifies the --clear argument to remove constraints with\n"
      INDENT "expired lifetimes.",
      NULL },
    { "lifetime", 'u', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &options.move_lifetime,
      "Lifespan (as ISO 8601 duration) of created constraints (with\n"
      INDENT "-B, -M) see https://en.wikipedia.org/wiki/ISO_8601#Durations)",
      "TIMESPEC" },
    { "promoted", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
      &options.promoted_role_only,
      "Limit scope of command to promoted role (with -B, -M, -U). For\n"
      INDENT "-B and -M, previously promoted instances may remain\n"
      INDENT "active in the unpromoted role.",
      NULL },

    // Deprecated since 2.1.0
    { "master", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
      &options.promoted_role_only,
      "Deprecated: Use --promoted instead", NULL },

    { NULL }
};

static GOptionEntry advanced_entries[] = {
    { "delete", 'D', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, command_cb,
      "(Advanced) Delete a resource from the CIB. Required: -t",
      NULL },
    { "fail", 'F', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, command_cb,
      "(Advanced) Tell the cluster this resource has failed",
      NULL },
    { "restart", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, command_cb,
      "(Advanced) Tell the cluster to restart this resource and\n"
      INDENT "anything that depends on it. This temporarily modifies\n"
      INDENT "the CIB, and other CIB modifications should be avoided\n"
      INDENT "while this is in progress. If a node is fenced because\n"
      INDENT "the stop portion of the restart fails, CIB modifications\n"
      INDENT "such as target-role may remain.",
      NULL },
    { "wait", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, command_cb,
      "(Advanced) Wait until the cluster settles into a stable state",
      NULL },
    { "digests", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, command_cb,
      "(Advanced) Show parameter hashes that Pacemaker uses to detect\n"
      INDENT "configuration changes (only accurate if there is resource\n"
      INDENT "history on the specified node). Required: --resource, --node.\n"
      INDENT "Optional: any NAME=VALUE parameters will be used to override\n"
      INDENT "the configuration (to see what the hash would be with those\n"
      INDENT "changes).",
      NULL },
    { "force-demote", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
          command_cb,
      "(Advanced) Bypass the cluster and demote a resource on the local\n"
      INDENT "node. Unless --force is specified, this will refuse to do so if\n"
      INDENT "the cluster believes the resource is a clone instance already\n"
      INDENT "running on the local node.",
      NULL },
    { "force-stop", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, command_cb,
      "(Advanced) Bypass the cluster and stop a resource on the local node",
      NULL },
    { "force-start", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, command_cb,
      "(Advanced) Bypass the cluster and start a resource on the local\n"
      INDENT "node. Unless --force is specified, this will refuse to do so if\n"
      INDENT "the cluster believes the resource is a clone instance already\n"
      INDENT "running on the local node.",
      NULL },
    { "force-promote", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
          command_cb,
      "(Advanced) Bypass the cluster and promote a resource on the local\n"
      INDENT "node. Unless --force is specified, this will refuse to do so if\n"
      INDENT "the cluster believes the resource is a clone instance already\n"
      INDENT "running on the local node.",
      NULL },
    { "force-check", 0, G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_CALLBACK,
          command_cb,
      "(Advanced) Bypass the cluster and check the state of a resource on\n"
      INDENT "the local node. An optional LEVEL argument can be given\n"
      INDENT "to control the level of checking performed.",
      "LEVEL" },

    { NULL }
};

static GOptionEntry addl_entries[] = {
    { "node", 'N', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &options.host_uname,
      "Node name",
      "NAME" },
    { "recursive", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &options.recursive,
      "Follow colocation chains when using --set-parameter or --constraints",
      NULL },
    { "resource-type", 't', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &options.rsc_type,
      "Resource XML element (primitive, group, etc.) (with -D)",
      "ELEMENT" },
    { "parameter-value", 'v', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &options.prop_value,
      "Value to use with -p",
      "PARAM" },
    { "meta", 'm', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, attr_set_type_cb,
      "Use resource meta-attribute instead of instance attribute\n"
      INDENT "(with -p, -g, -d)",
      NULL },
    { "utilization", 'z', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, attr_set_type_cb,
      "Use resource utilization attribute instead of instance attribute\n"
      INDENT "(with -p, -g, -d)",
      NULL },
    { "element", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, attr_set_type_cb,
      "Use resource element attribute instead of instance attribute\n"
      INDENT "(with -p, -g, -d)",
      NULL },
    { "operation", 'n', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &options.operation,
      "Operation to clear instead of all (with -C -r)",
      "OPERATION" },
    { "interval", 'I', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &options.interval_spec,
      "Interval of operation to clear (default 0s) (with -C -r -n)",
      "N" },
    { "class", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, cmdline_config_cb,
      "The standard the resource agent conforms to (for example, ocf).\n"
      INDENT "Use with --agent, --provider, --option, and --validate.",
      "CLASS" },
    { "agent", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, cmdline_config_cb,
      "The agent to use (for example, IPaddr). Use with --class,\n"
      INDENT "--provider, --option, and --validate.",
      "AGENT" },
    { "provider", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK,
          cmdline_config_cb,
      "The vendor that supplies the resource agent (for example,\n"
      INDENT "heartbeat). Use with --class, --agent, --option, and --validate.",
      "PROVIDER" },
    { "option", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, option_cb,
      "Specify a device configuration parameter as NAME=VALUE (may be\n"
      INDENT "specified multiple times). Use with --validate and without the\n"
      INDENT "-r option.",
      "PARAM" },
    { "set-name", 's', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &options.prop_set,
      "(Advanced) XML ID of attributes element to use (with -p, -d)",
      "ID" },
    { "nvpair", 'i', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &options.prop_id,
      "(Advanced) XML ID of nvpair element to use (with -p, -d)",
      "ID" },
    { "timeout", 'T', G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, timeout_cb,
      "(Advanced) Abort if command does not finish in this time (with\n"
      INDENT "--restart, --wait, --force-*)",
      "N" },
    { "all", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &options.all,
      "List all options, including advanced and deprecated (with\n"
      INDENT "--list-options)",
      NULL },
    { "force", 'f', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &options.force,
      "Force the action to be performed. See help for individual commands for\n"
      INDENT "additional behavior.",
      NULL },

    // @COMPAT Used in resource-agents prior to v4.2.0
    { "host-uname", 'H', G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &options.host_uname,
      NULL,
      "HOST" },

    { NULL }
};

static int
ban_or_move(pcmk__output_t *out, pcmk_resource_t *rsc,
            const char *move_lifetime)
{
    int rc = pcmk_rc_ok;
    pcmk_node_t *current = NULL;
    unsigned int nactive = 0;

    CRM_CHECK(rsc != NULL, return EINVAL);

    current = pe__find_active_requires(rsc, &nactive);

    if (nactive == 1) {
        rc = cli_resource_ban(out, options.rsc_id, current->priv->name,
                              move_lifetime, cib_conn,
                              options.promoted_role_only, PCMK_ROLE_PROMOTED);

    } else if (pcmk_is_set(rsc->flags, pcmk__rsc_promotable)) {
        int count = 0;
        GList *iter = NULL;

        current = NULL;
        for (iter = rsc->priv->children; iter != NULL; iter = iter->next) {
            pcmk_resource_t *child = (pcmk_resource_t *)iter->data;
            enum rsc_role_e child_role = child->priv->fns->state(child, true);

            if (child_role == pcmk_role_promoted) {
                count++;
                current = pcmk__current_node(child);
            }
        }

        if(count == 1 && current) {
            rc = cli_resource_ban(out, options.rsc_id, current->priv->name,
                                  move_lifetime, cib_conn,
                                  options.promoted_role_only,
                                  PCMK_ROLE_PROMOTED);

        } else {
            rc = EINVAL;
            g_set_error(&error, PCMK__EXITC_ERROR, CRM_EX_USAGE,
                        _("Resource '%s' not moved: active in %d locations (promoted in %d).\n"
                        "To prevent '%s' from running on a specific location, "
                        "specify a node."
                        "To prevent '%s' from being promoted at a specific "
                        "location, specify a node and the --promoted option."),
                        options.rsc_id, nactive, count, options.rsc_id, options.rsc_id);
        }

    } else {
        rc = EINVAL;
        g_set_error(&error, PCMK__EXITC_ERROR, CRM_EX_USAGE,
                    _("Resource '%s' not moved: active in %d locations.\n"
                    "To prevent '%s' from running on a specific location, "
                    "specify a node."),
                    options.rsc_id, nactive, options.rsc_id);
    }

    return rc;
}

static void
cleanup(pcmk__output_t *out, pcmk_resource_t *rsc, pcmk_node_t *node)
{
    int rc = pcmk_rc_ok;

    if (options.force == FALSE) {
        rsc = uber_parent(rsc);
    }

    crm_debug("Erasing failures of %s (%s requested) on %s",
              rsc->id, options.rsc_id,
              ((node != NULL)? pcmk__node_name(node) : "all nodes"));
    rc = cli_resource_delete(controld_api, node, rsc, options.operation,
                             options.interval_spec, true, scheduler,
                             options.force);

    if ((rc == pcmk_rc_ok) && !out->is_quiet(out)) {
        // Show any reasons why resource might stay stopped
        cli_resource_check(out, rsc, node);
    }

    if (rc == pcmk_rc_ok) {
        start_mainloop(controld_api);
    }
}

static int
initialize_scheduler_data(xmlNode **cib_xml_orig)
{
    int rc = pcmk_rc_ok;

    pcmk__assert(cib_conn != NULL);

    scheduler = pcmk_new_scheduler();
    if (scheduler == NULL) {
        return ENOMEM;
    }

    pcmk__set_scheduler_flags(scheduler, pcmk__sched_no_counts);
    scheduler->priv->out = out;
    rc = update_scheduler_input(out, scheduler, cib_conn, cib_xml_orig);
    if (rc != pcmk_rc_ok) {
        return rc;
    }

    cluster_status(scheduler);
    return pcmk_rc_ok;
}

static int
refresh(pcmk__output_t *out, const pcmk_node_t *node)
{
    const char *node_name = NULL;
    const char *log_node_name = "all nodes";
    const char *router_node = NULL;
    int attr_options = pcmk__node_attr_none;
    int rc = pcmk_rc_ok;

    if (node != NULL) {
        node_name = node->priv->name;
        log_node_name = pcmk__node_name(node);
        router_node = node->priv->name;
    }

    if (pcmk__is_pacemaker_remote_node(node)) {
        const pcmk_node_t *conn_host = pcmk__current_node(node->priv->remote);

        if (conn_host == NULL) {
            rc = ENXIO;
            g_set_error(&error, PCMK__RC_ERROR, rc,
                        _("No cluster connection to Pacemaker Remote node %s "
                          "detected"),
                        log_node_name);
            return rc;
        }
        router_node = conn_host->priv->name;
        pcmk__set_node_attr_flags(attr_options, pcmk__node_attr_remote);
    }

    if (controld_api == NULL) {
        out->info(out, "Dry run: skipping clean-up of %s due to CIB_file",
                  log_node_name);
        return pcmk_rc_ok;
    }

    crm_debug("Re-checking the state of all resources on %s", log_node_name);

    rc = pcmk__attrd_api_clear_failures(NULL, node_name, NULL, NULL, NULL, NULL,
                                        attr_options);

    if (pcmk_controld_api_reprobe(controld_api, node_name,
                                  router_node) == pcmk_rc_ok) {
        start_mainloop(controld_api);
    }

    return rc;
}

static void
refresh_resource(pcmk__output_t *out, pcmk_resource_t *rsc, pcmk_node_t *node)
{
    int rc = pcmk_rc_ok;

    if (options.force == FALSE) {
        rsc = uber_parent(rsc);
    }

    crm_debug("Re-checking the state of %s (%s requested) on %s",
              rsc->id, options.rsc_id,
              ((node != NULL)? pcmk__node_name(node) : "all nodes"));
    rc = cli_resource_delete(controld_api, node, rsc, NULL, 0, false, scheduler,
                             options.force);

    if ((rc == pcmk_rc_ok) && !out->is_quiet(out)) {
        // Show any reasons why resource might stay stopped
        cli_resource_check(out, rsc, node);
    }

    if (rc == pcmk_rc_ok) {
        start_mainloop(controld_api);
    }
}

static void
validate_cmdline_config(void)
{
    // Cannot use both --resource and command-line resource configuration
    if (options.rsc_id != NULL) {
        g_set_error(&error, PCMK__EXITC_ERROR, CRM_EX_USAGE,
                    _("--resource cannot be used with --class, --agent, and --provider"));

    // Not all commands support command-line resource configuration
    } else if (options.rsc_cmd != cmd_execute_agent) {
        g_set_error(&error, PCMK__EXITC_ERROR, CRM_EX_USAGE,
                    _("--class, --agent, and --provider can only be used with "
                    "--validate and --force-*"));

    // Not all of --class, --agent, and --provider need to be given.  Not all
    // classes support the concept of a provider.  Check that what we were given
    // is valid.
    } else if (pcmk__str_eq(options.v_class, "stonith", pcmk__str_none)) {
        if (options.v_provider != NULL) {
            g_set_error(&error, PCMK__EXITC_ERROR, CRM_EX_USAGE,
                        _("stonith does not support providers"));

        } else if (stonith_agent_exists(options.v_agent, 0) == FALSE) {
            g_set_error(&error, PCMK__EXITC_ERROR, CRM_EX_USAGE,
                        _("%s is not a known stonith agent"), options.v_agent ? options.v_agent : "");
        }

    } else if (resources_agent_exists(options.v_class, options.v_provider, options.v_agent) == FALSE) {
        g_set_error(&error, PCMK__EXITC_ERROR, CRM_EX_USAGE,
                    _("%s:%s:%s is not a known resource"),
                    options.v_class ? options.v_class : "",
                    options.v_provider ? options.v_provider : "",
                    options.v_agent ? options.v_agent : "");
    }

    if ((error == NULL) && (options.cmdline_params == NULL)) {
        options.cmdline_params = pcmk__strkey_table(free, free);
    }
}

/*!
 * \internal
 * \brief Get the <tt>enum pe_find</tt> flags for a given command
 *
 * \return <tt>enum pe_find</tt> flag group appropriate for \c options.rsc_cmd.
 */
static uint32_t
get_find_flags(void)
{
    switch (options.rsc_cmd) {
        case cmd_ban:
        case cmd_cleanup:
        case cmd_clear:
        case cmd_colocations:
        case cmd_digests:
        case cmd_execute_agent:
        case cmd_locate:
        case cmd_move:
        case cmd_refresh:
        case cmd_restart:
        case cmd_why:
            return pcmk_rsc_match_history|pcmk_rsc_match_anon_basename;

        case cmd_delete_param:
        case cmd_get_param:
        case cmd_query_xml_raw:
        case cmd_query_xml:
        case cmd_set_param:
            return pcmk_rsc_match_history|pcmk_rsc_match_basename;

        default:
            return 0;
    }
}

/*!
 * \internal
 * \brief Check whether a node argument is required
 *
 * \return \c true if a \c --node argument is required, or \c false otherwise
 */
static bool
is_node_required(void)
{
    switch (options.rsc_cmd) {
        case cmd_digests:
        case cmd_fail:
            return true;
        default:
            return false;
    }
}

/*!
 * \internal
 * \brief Check whether a resource argument is required
 *
 * \return \c true if a \c --resource argument is required, or \c false
 *         otherwise
 */
static bool
is_resource_required(void)
{
    if (options.cmdline_config) {
        return false;
    }

    switch (options.rsc_cmd) {
        case cmd_clear:
            return !options.clear_expired;

        case cmd_cleanup:
        case cmd_cts:
        case cmd_list_active_ops:
        case cmd_list_agents:
        case cmd_list_all_ops:
        case cmd_list_alternatives:
        case cmd_list_instances:
        case cmd_list_options:
        case cmd_list_providers:
        case cmd_list_resources:
        case cmd_list_standards:
        case cmd_metadata:
        case cmd_refresh:
        case cmd_wait:
        case cmd_why:
            return false;

        default:
            return true;
    }
}

/*!
 * \internal
 * \brief Check whether a scheduler data object is required
 *
 * If true, the caller will populate the scheduler data from the CIB connection.
 *
 * \return \c true if scheduler data is required, or \c false otherwise
 */
static bool
is_scheduler_required(void)
{
    if (options.cmdline_config) {
        // cmd_execute_agent using CLI parameters instead of CIB connection
        return false;
    }

    switch (options.rsc_cmd) {
        case cmd_delete:
        case cmd_list_agents:
        case cmd_list_alternatives:
        case cmd_list_options:
        case cmd_list_providers:
        case cmd_list_standards:
        case cmd_metadata:
        case cmd_wait:
            return false;
        default:
            return true;
    }
}

/*!
 * \internal
 * \brief Check whether a CIB connection is required
 *
 * \return \c true if a CIB connection is required, or \c false otherwise
 */
static bool
is_cib_required(void)
{
    if (options.cmdline_config) {
        // cmd_execute_agent using CLI parameters instead of CIB connection
        return false;
    }

    if (is_scheduler_required()) {
        return true;
    }

    // Commands that requires CIB connection but not scheduler data
    switch (options.rsc_cmd) {
        case cmd_delete:
        case cmd_wait:
            return true;
        default:
            return false;
    }
}

/*!
 * \internal
 * \brief Check whether a controller IPC connection is required
 *
 * \return \c true if a controller connection is required, or \c false otherwise
 */
static bool
is_controller_required(void)
{
    switch (options.rsc_cmd) {
        case cmd_cleanup:
        case cmd_refresh:
            return getenv("CIB_file") == NULL;

        case cmd_fail:
            return true;

        default:
            return false;
    }
}

/*!
 * \internal
 * \brief Check whether the chosen command accepts clone instances
 *
 * \return \c true if \p options.rsc_cmd accepts or ignores clone instances, or
 *         \c false otherwise
 */
static bool
accept_clone_instance(void)
{
    switch (options.rsc_cmd) {
        case cmd_ban:
        case cmd_clear:
        case cmd_delete:
        case cmd_move:
        case cmd_restart:
            return false;
        default:
            return true;
    }
}

static int
handle_ban(pcmk_resource_t *rsc, const pcmk_node_t *node)
{
    int rc = pcmk_rc_ok;

    if (node == NULL) {
        rc = ban_or_move(out, rsc, options.move_lifetime);
    } else {
        rc = cli_resource_ban(out, options.rsc_id, node->priv->name,
                              options.move_lifetime, cib_conn,
                              options.promoted_role_only, PCMK_ROLE_PROMOTED);
    }

    if (rc == EINVAL) {
        exit_code = CRM_EX_USAGE;
        return pcmk_rc_ok;
    }
    return rc;
}

static int
handle_cleanup(pcmk_resource_t *rsc, pcmk_node_t *node)
{
    if (rsc == NULL) {
        int rc = cli_cleanup_all(controld_api, node, options.operation,
                                 options.interval_spec, scheduler);

        if (rc == pcmk_rc_ok) {
            start_mainloop(controld_api);
        }

    } else {
        cleanup(out, rsc, node);
    }

    return pcmk_rc_ok;
}

static int
handle_clear(const pcmk_node_t *node)
{
    const char *node_name = (node != NULL)? node->priv->name : NULL;
    GList *before = NULL;
    GList *after = NULL;
    GList *remaining = NULL;
    int rc = pcmk_rc_ok;

    if (!out->is_quiet(out)) {
        before = build_constraint_list(scheduler->input);
    }

    if (options.clear_expired) {
        rc = cli_resource_clear_all_expired(scheduler->input, cib_conn,
                                            options.rsc_id, node_name,
                                            options.promoted_role_only);

    } else if (node != NULL) {
        rc = cli_resource_clear(options.rsc_id, node_name, NULL, cib_conn, true,
                                options.force);

    } else {
        rc = cli_resource_clear(options.rsc_id, NULL, scheduler->nodes,
                                cib_conn, true, options.force);
    }

    if (!out->is_quiet(out)) {
        xmlNode *cib_xml = NULL;

        rc = cib_conn->cmds->query(cib_conn, NULL, &cib_xml, cib_sync_call);
        rc = pcmk_legacy2rc(rc);

        if (rc != pcmk_rc_ok) {
            g_set_error(&error, PCMK__RC_ERROR, rc,
                        _("Could not get modified CIB: %s"), pcmk_rc_str(rc));
            g_list_free(before);
            pcmk__xml_free(cib_xml);
            return rc;
        }

        scheduler->input = cib_xml;
        cluster_status(scheduler);

        after = build_constraint_list(scheduler->input);
        remaining = pcmk__subtract_lists(before, after, (GCompareFunc) strcmp);

        for (const GList *iter = remaining; iter != NULL; iter = iter->next) {
            const char *constraint = iter->data;

            out->info(out, "Removing constraint: %s", constraint);
        }

        g_list_free(before);
        g_list_free(after);
        g_list_free(remaining);
    }

    return rc;
}

static int
handle_colocations(pcmk_resource_t *rsc)
{
    return out->message(out, "locations-and-colocations", rsc,
                        options.recursive, options.force);
}

static int
handle_cts(void)
{
    // coverity[var_deref_op] False positive
    g_list_foreach(scheduler->priv->resources, (GFunc) cli_resource_print_cts,
                   out);
    cli_resource_print_cts_constraints(scheduler);
    return pcmk_rc_ok;
}

static int
handle_delete(void)
{
    /* rsc_id was already checked for NULL much earlier when validating command
     * line arguments
     */
    int rc = pcmk_rc_ok;

    if (options.rsc_type == NULL) {
        exit_code = CRM_EX_USAGE;
        g_set_error(&error, PCMK__EXITC_ERROR, CRM_EX_USAGE,
                    _("You need to specify a resource type with -t"));

    } else {
        rc = pcmk__resource_delete(cib_conn, cib_sync_call, options.rsc_id,
                                   options.rsc_type);

        if (rc != pcmk_rc_ok) {
            g_set_error(&error, PCMK__RC_ERROR, rc,
                        _("Could not delete resource %s: %s"),
                        options.rsc_id, pcmk_rc_str(rc));
        }
    }
    return rc;
}

static int
handle_delete_param(pcmk_resource_t *rsc, xmlNode *cib_xml_orig)
{
    /* coverity[var_deref_model] False positive */
    return cli_resource_delete_attribute(rsc, options.rsc_id, options.prop_set,
                                         options.attr_set_type, options.prop_id,
                                         options.prop_name, cib_conn,
                                         cib_xml_orig, options.force);
}

static int
handle_digests(pcmk_resource_t *rsc, const pcmk_node_t *node)
{
    return pcmk__resource_digests(out, rsc, node, options.override_params);
}

static int
handle_execute_agent(pcmk_resource_t *rsc)
{
    if (options.cmdline_config) {
        exit_code = cli_resource_execute_from_params(out, NULL, options.v_class,
                                                     options.v_provider,
                                                     options.v_agent,
                                                     options.operation,
                                                     options.cmdline_params,
                                                     options.override_params,
                                                     options.timeout_ms,
                                                     args->verbosity,
                                                     options.force,
                                                     options.check_level);
    } else {
        exit_code = cli_resource_execute(rsc, options.rsc_id, options.operation,
                                         options.override_params,
                                         options.timeout_ms, cib_conn,
                                         scheduler, args->verbosity,
                                         options.force, options.check_level);
    }
    return pcmk_rc_ok;
}

static int
handle_fail(const pcmk_node_t *node)
{
    int rc = cli_resource_fail(controld_api, node, options.rsc_id,
                               scheduler);

    if (rc == pcmk_rc_ok) {
        start_mainloop(controld_api);
    }
    return rc;
}

static int
handle_get_param(pcmk_resource_t *rsc)
{
    unsigned int count = 0;
    GHashTable *params = NULL;
    pcmk_node_t *current = rsc->priv->fns->active_node(rsc, &count, NULL);
    bool free_params = true;
    const char *value = NULL;
    int rc = pcmk_rc_ok;

    if (count > 1) {
        out->err(out,
                 "%s is active on more than one node, returning the default "
                 "value for %s",
                 rsc->id, pcmk__s(options.prop_name, "unspecified property"));
        current = NULL;
    }

    crm_debug("Looking up %s in %s", options.prop_name, rsc->id);

    if (pcmk__str_eq(options.attr_set_type, PCMK_XE_INSTANCE_ATTRIBUTES,
                     pcmk__str_none)) {
        params = pe_rsc_params(rsc, current, scheduler);
        free_params = false;

        value = g_hash_table_lookup(params, options.prop_name);

    } else if (pcmk__str_eq(options.attr_set_type, PCMK_XE_META_ATTRIBUTES,
                            pcmk__str_none)) {
        params = pcmk__strkey_table(free, free);
        get_meta_attributes(params, rsc, NULL, scheduler);

        value = g_hash_table_lookup(params, options.prop_name);

    } else if (pcmk__str_eq(options.attr_set_type, ATTR_SET_ELEMENT,
                            pcmk__str_none)) {
        value = crm_element_value(rsc->priv->xml, options.prop_name);
        free_params = false;

    } else {
        const pcmk_rule_input_t rule_input = {
            .now = scheduler->priv->now,
        };

        params = pcmk__strkey_table(free, free);
        pe__unpack_dataset_nvpairs(rsc->priv->xml, PCMK_XE_UTILIZATION,
                                   &rule_input, params, NULL, scheduler);

        value = g_hash_table_lookup(params, options.prop_name);
    }

    rc = out->message(out, "attribute-list", rsc, options.prop_name, value);
    if (free_params) {
        g_hash_table_destroy(params);
    }

    return rc;
}

static int
handle_list_active_ops(const pcmk_node_t *node)
{
    const char *node_name = (node != NULL)? node->priv->name : NULL;

    return cli_resource_print_operations(options.rsc_id, node_name, true,
                                         scheduler);
}

static int
handle_list_agents(void)
{
    return pcmk__list_agents(out, options.agent_spec);
}

static int
handle_list_all_ops(const pcmk_node_t *node)
{
    const char *node_name = (node != NULL)? node->priv->name : NULL;

    return cli_resource_print_operations(options.rsc_id, node_name, false,
                                         scheduler);
}

static int
handle_list_alternatives(void)
{
    return pcmk__list_alternatives(out, options.agent_spec);
}

static int
handle_list_instances(void)
{
    // coverity[var_deref_op] False positive
    if (out->message(out, "resource-names-list",
                     scheduler->priv->resources) != pcmk_rc_ok) {
        return ENXIO;
    }
    return pcmk_rc_ok;
}

static int
handle_list_options(void)
{
    switch (options.opt_list) {
        case pcmk__opt_fencing:
            return pcmk__list_fencing_params(out, options.all);
        case pcmk__opt_primitive:
            return pcmk__list_primitive_meta(out, options.all);
        default:
            exit_code = CRM_EX_SOFTWARE;
            g_set_error(&error, PCMK__EXITC_ERROR, exit_code,
                        "BUG: Invalid option list type");
            return pcmk_rc_ok;
    }
}

static int
handle_list_providers(void)
{
    return pcmk__list_providers(out, options.agent_spec);
}

static int
handle_list_resources(void)
{
    GList *all = g_list_prepend(NULL, (gpointer) "*");
    int rc = out->message(out, "resource-list", scheduler,
                          pcmk_show_inactive_rscs
                          |pcmk_show_rsc_only
                          |pcmk_show_pending,
                          true, all, all, false);

    g_list_free(all);

    if (rc == pcmk_rc_no_output) {
        rc = ENXIO;
    }
    return rc;
}

static int
handle_list_standards(void)
{
    return pcmk__list_standards(out);
}

static int
handle_locate(pcmk_resource_t *rsc)
{
    GList *nodes = cli_resource_search(rsc, options.rsc_id, scheduler);
    int rc = out->message(out, "resource-search-list", nodes, options.rsc_id);

    g_list_free_full(nodes, free);
    return rc;
}

static int
handle_metadata(void)
{
    int rc = pcmk_rc_ok;
    char *standard = NULL;
    char *provider = NULL;
    char *type = NULL;
    char *metadata = NULL;
    lrmd_t *lrmd_conn = NULL;

    rc = lrmd__new(&lrmd_conn, NULL, NULL, 0);
    if (rc != pcmk_rc_ok) {
        g_set_error(&error, PCMK__RC_ERROR, rc,
                    _("Could not create executor connection"));
        lrmd_api_delete(lrmd_conn);
        return rc;
    }

    rc = crm_parse_agent_spec(options.agent_spec, &standard, &provider, &type);
    rc = pcmk_legacy2rc(rc);

    if (rc == pcmk_rc_ok) {
        rc = lrmd_conn->cmds->get_metadata(lrmd_conn, standard,
                                           provider, type,
                                           &metadata, 0);
        rc = pcmk_legacy2rc(rc);

        if (metadata != NULL) {
            out->output_xml(out, PCMK_XE_METADATA, metadata);
            free(metadata);
        } else {
            /* We were given a validly formatted spec, but it doesn't necessarily
             * match up with anything that exists.  Use ENXIO as the return code
             * here because that maps to an exit code of CRM_EX_NOSUCH, which
             * probably is the most common reason to get here.
             */
            rc = ENXIO;
            g_set_error(&error, PCMK__RC_ERROR, rc,
                        _("Metadata query for %s failed: %s"),
                        options.agent_spec, pcmk_rc_str(rc));
        }
    } else {
        rc = ENXIO;
        g_set_error(&error, PCMK__RC_ERROR, rc,
                    _("'%s' is not a valid agent specification"),
                    options.agent_spec);
    }

    lrmd_api_delete(lrmd_conn);
    return rc;
}

static int
handle_move(pcmk_resource_t *rsc, const pcmk_node_t *node)
{
    int rc = pcmk_rc_ok;

    if (node == NULL) {
        rc = ban_or_move(out, rsc, options.move_lifetime);
    } else {
        rc = cli_resource_move(rsc, options.rsc_id, node, options.move_lifetime,
                               cib_conn, scheduler, options.promoted_role_only,
                               options.force);
    }

    if (rc == EINVAL) {
        exit_code = CRM_EX_USAGE;
        return pcmk_rc_ok;
    }
    return rc;
}

static int
handle_query_xml(pcmk_resource_t *rsc)
{
    return cli_resource_print(rsc, scheduler, true);
}

static int
handle_query_xml_raw(pcmk_resource_t *rsc)
{
    return cli_resource_print(rsc, scheduler, false);
}

static int
handle_refresh(pcmk_resource_t *rsc, pcmk_node_t *node)
{
    if (rsc == NULL) {
        return refresh(out, node);
    }
    refresh_resource(out, rsc, node);
    return pcmk_rc_ok;
}

static int
handle_restart(pcmk_resource_t *rsc, const pcmk_node_t *node)
{
    /* We don't pass scheduler because rsc needs to stay valid for the entire
     * lifetime of cli_resource_restart(), but it will reset and update the
     * scheduler data multiple times, so it needs to use its own copy.
     */
    return cli_resource_restart(out, rsc, node, options.move_lifetime,
                                options.timeout_ms, cib_conn,
                                options.promoted_role_only, options.force);
}

static int
handle_set_param(pcmk_resource_t *rsc, xmlNode *cib_xml_orig)
{
    if (pcmk__str_empty(options.prop_value)) {
        exit_code = CRM_EX_USAGE;
        g_set_error(&error, PCMK__EXITC_ERROR, exit_code,
                    _("You need to supply a value with the -v option"));
        return pcmk_rc_ok;
    }

    // coverity[var_deref_model] False positive
    return cli_resource_update_attribute(rsc, options.rsc_id, options.prop_set,
                                         options.attr_set_type, options.prop_id,
                                         options.prop_name, options.prop_value,
                                         options.recursive, cib_conn,
                                         cib_xml_orig, options.force);
}

static int
handle_wait(void)
{
    return wait_till_stable(out, options.timeout_ms, cib_conn);
}

static int
handle_why(pcmk_resource_t *rsc, pcmk_node_t *node)
{
    return out->message(out, "resource-reasons-list",
                        scheduler->priv->resources, rsc, node);
}

static GOptionContext *
build_arg_context(pcmk__common_args_t *args, GOptionGroup **group) {
    GOptionContext *context = NULL;

    GOptionEntry extra_prog_entries[] = {
        { "quiet", 'Q', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &(args->quiet),
          "Be less descriptive in output.",
          NULL },
        { "resource", 'r', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &options.rsc_id,
          "Resource ID",
          "ID" },
        { G_OPTION_REMAINING, 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING_ARRAY, &options.remainder,
          NULL,
          NULL },

        { NULL }
    };

    const char *description = "Examples:\n\n"
                              "List the available OCF agents:\n\n"
                              "\t# crm_resource --list-agents ocf\n\n"
                              "List the available OCF agents from the linux-ha project:\n\n"
                              "\t# crm_resource --list-agents ocf:heartbeat\n\n"
                              "Move 'myResource' to a specific node:\n\n"
                              "\t# crm_resource --resource myResource --move --node altNode\n\n"
                              "Allow (but not force) 'myResource' to move back to its original "
                              "location:\n\n"
                              "\t# crm_resource --resource myResource --clear\n\n"
                              "Stop 'myResource' (and anything that depends on it):\n\n"
                              "\t# crm_resource --resource myResource --set-parameter "
                              PCMK_META_TARGET_ROLE "--meta --parameter-value Stopped\n\n"
                              "Tell the cluster not to manage 'myResource' (the cluster will not "
                              "attempt to start or stop the\n"
                              "resource under any circumstances; useful when performing maintenance "
                              "tasks on a resource):\n\n"
                              "\t# crm_resource --resource myResource --set-parameter "
                              PCMK_META_IS_MANAGED "--meta --parameter-value false\n\n"
                              "Erase the operation history of 'myResource' on 'aNode' (the cluster "
                              "will 'forget' the existing\n"
                              "resource state, including any errors, and attempt to recover the"
                              "resource; useful when a resource\n"
                              "had failed permanently and has been repaired by an administrator):\n\n"
                              "\t# crm_resource --resource myResource --cleanup --node aNode\n\n";

    context = pcmk__build_arg_context(args, "text (default), xml", group, NULL);
    g_option_context_set_description(context, description);

    /* Add the -Q option, which cannot be part of the globally supported options
     * because some tools use that flag for something else.
     */
    pcmk__add_main_args(context, extra_prog_entries);

    pcmk__add_arg_group(context, "queries", "Queries:",
                        "Show query help", query_entries);
    pcmk__add_arg_group(context, "commands", "Commands:",
                        "Show command help", command_entries);
    pcmk__add_arg_group(context, "locations", "Locations:",
                        "Show location help", location_entries);
    pcmk__add_arg_group(context, "advanced", "Advanced:",
                        "Show advanced option help", advanced_entries);
    pcmk__add_arg_group(context, "additional", "Additional Options:",
                        "Show additional options", addl_entries);
    return context;
}

int
main(int argc, char **argv)
{
    xmlNode *cib_xml_orig = NULL;
    pcmk_resource_t *rsc = NULL;
    pcmk_node_t *node = NULL;
    uint32_t find_flags = 0;
    int rc = pcmk_rc_ok;

    GOptionGroup *output_group = NULL;
    gchar **processed_args = NULL;
    GOptionContext *context = NULL;

    /*
     * Parse command line arguments
     */

    args = pcmk__new_common_args(SUMMARY);
    processed_args = pcmk__cmdline_preproc(argv, "GHINSTdginpstuvx");
    context = build_arg_context(args, &output_group);

    pcmk__register_formats(output_group, formats);
    if (!g_option_context_parse_strv(context, &processed_args, &error)) {
        exit_code = CRM_EX_USAGE;
        goto done;
    }

    pcmk__cli_init_logging("crm_resource", args->verbosity);

    rc = pcmk__output_new(&out, args->output_ty, args->output_dest, argv);
    if (rc != pcmk_rc_ok) {
        exit_code = CRM_EX_ERROR;
        g_set_error(&error, PCMK__EXITC_ERROR, exit_code, _("Error creating output format %s: %s"),
                    args->output_ty, pcmk_rc_str(rc));
        goto done;
    }

    pe__register_messages(out);
    crm_resource_register_messages(out);
    lrmd__register_messages(out);
    pcmk__register_lib_messages(out);

    out->quiet = args->quiet;

    crm_log_args(argc, argv);

    /*
     * Validate option combinations
     */

    // --expired without --clear/-U doesn't make sense
    if (options.clear_expired && (options.rsc_cmd != cmd_clear)) {
        exit_code = CRM_EX_USAGE;
        g_set_error(&error, PCMK__EXITC_ERROR, exit_code, _("--expired requires --clear or -U"));
        goto done;
    }

    if (options.remainder != NULL) {
        // Commands that use positional arguments will create override_params
        if (options.override_params == NULL) {
            GString *msg = g_string_sized_new(128);
            guint len = g_strv_length(options.remainder);

            g_string_append(msg, "non-option ARGV-elements:");

            for (int i = 0; i < len; i++) {
                g_string_append_printf(msg, "\n[%d of %u] %s",
                                       i + 1, len, options.remainder[i]);
            }
            exit_code = CRM_EX_USAGE;
            g_set_error(&error, PCMK__EXITC_ERROR, exit_code, "%s", msg->str);
            g_string_free(msg, TRUE);
            goto done;
        }

        for (gchar **arg = options.remainder; *arg != NULL; arg++) {
            gchar *name = NULL;
            gchar *value = NULL;
            int rc = pcmk__scan_nvpair(*arg, &name, &value);

            if (rc != pcmk_rc_ok) {
                exit_code = CRM_EX_USAGE;
                g_set_error(&error, PCMK__EXITC_ERROR, exit_code,
                            _("Error parsing '%s' as a name=value pair"), *arg);
                goto done;
            }

            g_hash_table_insert(options.override_params, name, value);
        }
    }

    if (pcmk__str_eq(args->output_ty, "xml", pcmk__str_none)) {
        switch (options.rsc_cmd) {
            /* These are the only commands that have historically used the <list>
             * elements in their XML schema.  For all others, use the simple list
             * argument.
             */
            case cmd_get_param:
            case cmd_list_instances:
            case cmd_list_standards:
                pcmk__output_enable_list_element(out);
                break;

            default:
                break;
        }

    } else if (pcmk__str_eq(args->output_ty, "text", pcmk__str_null_matches)) {
        switch (options.rsc_cmd) {
            case cmd_colocations:
            case cmd_list_resources:
                pcmk__output_text_set_fancy(out, true);
                break;
            default:
                break;
        }
    }

    if (args->version) {
        out->version(out, false);
        goto done;
    }

    if (options.cmdline_config) {
        /* A resource configuration was given on the command line. Sanity-check
         * the values and set error if they don't make sense.
         */
        validate_cmdline_config();
        if (error != NULL) {
            exit_code = CRM_EX_USAGE;
            goto done;
        }

    } else if (options.cmdline_params != NULL) {
        exit_code = CRM_EX_USAGE;
        g_set_error(&error, PCMK__EXITC_ERROR, exit_code,
                    _("--option must be used with --validate and without -r"));
        g_hash_table_destroy(options.cmdline_params);
        goto done;
    }

    if (is_resource_required() && (options.rsc_id == NULL)) {
        exit_code = CRM_EX_USAGE;
        g_set_error(&error, PCMK__EXITC_ERROR, exit_code,
                    _("Must supply a resource id with -r"));
        goto done;
    }
    if (is_node_required() && (options.host_uname == NULL)) {
        exit_code = CRM_EX_USAGE;
        g_set_error(&error, PCMK__EXITC_ERROR, exit_code,
                    _("Must supply a node name with -N"));
        goto done;
    }

    /*
     * Set up necessary connections
     */

    // Establish a connection to the CIB if needed
    if (is_cib_required()) {
        cib_conn = cib_new();
        if ((cib_conn == NULL) || (cib_conn->cmds == NULL)) {
            exit_code = CRM_EX_DISCONNECT;
            g_set_error(&error, PCMK__EXITC_ERROR, exit_code,
                        _("Could not create CIB connection"));
            goto done;
        }
        rc = cib__signon_attempts(cib_conn, cib_command, 5);
        rc = pcmk_legacy2rc(rc);
        if (rc != pcmk_rc_ok) {
            exit_code = pcmk_rc2exitc(rc);
            g_set_error(&error, PCMK__EXITC_ERROR, exit_code,
                        _("Could not connect to the CIB: %s"), pcmk_rc_str(rc));
            goto done;
        }
    }

    // Populate scheduler data from CIB query
    if (is_scheduler_required()) {
        rc = initialize_scheduler_data(&cib_xml_orig);
        if (rc != pcmk_rc_ok) {
            exit_code = pcmk_rc2exitc(rc);
            goto done;
        }

        /* If user supplied a node name, check whether it exists.
         * Commands that don't require scheduler data ignore the node argument.
         */
        if (options.host_uname != NULL) {
            node = pcmk_find_node(scheduler, options.host_uname);

            if (node == NULL) {
                exit_code = CRM_EX_NOSUCH;
                g_set_error(&error, PCMK__EXITC_ERROR, exit_code,
                            _("Node '%s' not found"), options.host_uname);
                goto done;
            }
        }
    }

    find_flags = get_find_flags();

    // If command requires that resource exist if specified, find it
    if ((find_flags != 0) && (options.rsc_id != NULL)) {
        rsc = pe_find_resource_with_flags(scheduler->priv->resources,
                                          options.rsc_id, find_flags);
        if (rsc == NULL) {
            exit_code = CRM_EX_NOSUCH;
            g_set_error(&error, PCMK__EXITC_ERROR, exit_code,
                        _("Resource '%s' not found"), options.rsc_id);
            goto done;
        }

        /* The --ban, --clear, --move, and --restart commands do not work with
         * instances of clone resourcs.
         */
        if (pcmk__is_clone(rsc->priv->parent)
            && (strchr(options.rsc_id, ':') != NULL)
            && !accept_clone_instance()) {

            exit_code = CRM_EX_INVALID_PARAM;
            g_set_error(&error, PCMK__EXITC_ERROR, exit_code,
                        _("Cannot operate on clone resource instance '%s'"), options.rsc_id);
            goto done;
        }
    }

    // Establish a connection to the controller if needed
    if (is_controller_required()) {
        rc = pcmk_new_ipc_api(&controld_api, pcmk_ipc_controld);
        if (rc != pcmk_rc_ok) {
            exit_code = pcmk_rc2exitc(rc);
            g_set_error(&error, PCMK__EXITC_ERROR, exit_code,
                        _("Error connecting to the controller: %s"), pcmk_rc_str(rc));
            goto done;
        }
        pcmk_register_ipc_callback(controld_api, controller_event_callback,
                                   NULL);
        rc = pcmk__connect_ipc(controld_api, pcmk_ipc_dispatch_main, 5);
        if (rc != pcmk_rc_ok) {
            exit_code = pcmk_rc2exitc(rc);
            g_set_error(&error, PCMK__EXITC_ERROR, exit_code,
                        _("Error connecting to %s: %s"),
                        pcmk_ipc_name(controld_api, true), pcmk_rc_str(rc));
            goto done;
        }
    }

    /*
     * Handle requested command
     */

    /* Some of these set exit_code explicitly and return pcmk_rc_ok to skip
     * setting exit_code based on rc after the switch.
     */
    switch (options.rsc_cmd) {
        case cmd_ban:
            rc = handle_ban(rsc, node);
            break;
        case cmd_cleanup:
            rc = handle_cleanup(rsc, node);
            break;
        case cmd_clear:
            rc = handle_clear(node);
            break;
        case cmd_colocations:
            rc = handle_colocations(rsc);
            break;
        case cmd_cts:
            rc = handle_cts();
            break;
        case cmd_delete:
            rc = handle_delete();
            break;
        case cmd_delete_param:
            rc = handle_delete_param(rsc, cib_xml_orig);
            break;
        case cmd_digests:
            rc = handle_digests(rsc, node);
            break;
        case cmd_execute_agent:
            rc = handle_execute_agent(rsc);
            break;
        case cmd_fail:
            rc = handle_fail(node);
            break;
        case cmd_get_param:
            // coverity[var_deref_model] False positive
            rc = handle_get_param(rsc);
            break;
        case cmd_list_active_ops:
            rc = handle_list_active_ops(node);
            break;
        case cmd_list_agents:
            rc = handle_list_agents();
            break;
        case cmd_list_all_ops:
            rc = handle_list_all_ops(node);
            break;
        case cmd_list_alternatives:
            rc = handle_list_alternatives();
            break;
        case cmd_list_instances:
            rc = handle_list_instances();
            break;
        case cmd_list_providers:
            rc = handle_list_providers();
            break;
        case cmd_list_options:
            rc = handle_list_options();
            break;
        case cmd_list_resources:
            rc = handle_list_resources();
            break;
        case cmd_list_standards:
            rc = handle_list_standards();
            break;
        case cmd_locate:
            rc = handle_locate(rsc);
            break;
        case cmd_metadata:
            rc = handle_metadata();
            break;
        case cmd_move:
            rc = handle_move(rsc, node);
            break;
        case cmd_query_xml:
            rc = handle_query_xml(rsc);
            break;
        case cmd_query_xml_raw:
            rc = handle_query_xml_raw(rsc);
            break;
        case cmd_refresh:
            rc = handle_refresh(rsc, node);
            break;
        case cmd_restart:
            rc = handle_restart(rsc, node);
            break;
        case cmd_set_param:
            rc = handle_set_param(rsc, cib_xml_orig);
            break;
        case cmd_wait:
            rc = handle_wait();
            break;
        case cmd_why:
            rc = handle_why(rsc, node);
            break;
        default:
            exit_code = CRM_EX_USAGE;
            g_set_error(&error, PCMK__EXITC_ERROR, exit_code,
                        _("Unimplemented command: %d"), (int) options.rsc_cmd);
            goto done;
    }

    /* Convert rc into an exit code. */
    if (rc != pcmk_rc_ok && rc != pcmk_rc_no_output) {
        exit_code = pcmk_rc2exitc(rc);
    }

    /*
     * Clean up and exit
     */

done:
    /* When we get here, exit_code has been set one of two ways - either at one of
     * the spots where there's a "goto done" (which itself could have happened either
     * directly or by calling pcmk_rc2exitc), or just up above after any of the break
     * statements.
     *
     * Thus, we can use just exit_code here to decide what to do.
     */
    if (exit_code != CRM_EX_OK && exit_code != CRM_EX_USAGE) {
        if (error != NULL) {
            char *msg = crm_strdup_printf("%s\nError performing operation: %s",
                                          error->message, crm_exit_str(exit_code));
            g_clear_error(&error);
            g_set_error(&error, PCMK__EXITC_ERROR, exit_code, "%s", msg);
            free(msg);
        } else {
            g_set_error(&error, PCMK__EXITC_ERROR, exit_code,
                        _("Error performing operation: %s"), crm_exit_str(exit_code));
        }
    }

    g_free(options.host_uname);
    g_free(options.interval_spec);
    g_free(options.move_lifetime);
    g_free(options.operation);
    g_free(options.prop_id);
    free(options.prop_name);
    g_free(options.prop_set);
    g_free(options.prop_value);
    g_free(options.rsc_id);
    g_free(options.rsc_type);
    free(options.agent_spec);
    free(options.v_agent);
    free(options.v_class);
    free(options.v_provider);
    if (options.override_params != NULL) {
        g_hash_table_destroy(options.override_params);
    }
    g_strfreev(options.remainder);

    /* options.cmdline_params does not need to be destroyed here.  See the
     * comments in cli_resource_execute_from_params.
     */

    g_strfreev(processed_args);
    g_option_context_free(context);

    pcmk__xml_free(cib_xml_orig);
    cib__clean_up_connection(&cib_conn);
    pcmk_free_ipc_api(controld_api);
    pcmk_free_scheduler(scheduler);
    if (mainloop != NULL) {
        g_main_loop_unref(mainloop);
    }

    pcmk__output_and_clear_error(&error, out);

    if (out != NULL) {
        out->finish(out, exit_code, true, NULL);
        pcmk__output_free(out);
    }

    pcmk__unregister_formats();
    return crm_exit(exit_code);
}
