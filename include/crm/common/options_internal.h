/*
 * Copyright 2006-2023 the Pacemaker project contributors
 *
 * The version control history for this file may have further details.
 *
 * This source code is licensed under the GNU Lesser General Public License
 * version 2.1 or later (LGPLv2.1+) WITHOUT ANY WARRANTY.
 */

#ifndef PCMK__OPTIONS_INTERNAL__H
#  define PCMK__OPTIONS_INTERNAL__H

#  ifndef PCMK__CONFIG_H
#    define PCMK__CONFIG_H
#    include <config.h>   // _Noreturn
#  endif

#  include <glib.h>     // GHashTable
#  include <stdbool.h>  // bool

#include <crm/common/strings_internal.h>    // pcmk__parse_interval_spec()

_Noreturn void pcmk__cli_help(char cmd);


/*
 * Environment variable option handling
 */

const char *pcmk__env_option(const char *option);
void pcmk__set_env_option(const char *option, const char *value, bool compat);
bool pcmk__env_option_enabled(const char *daemon, const char *option);


/*
 * Cluster option handling
 */

typedef struct pcmk__cluster_option_s {
    const char *name;
    const char *alt_name;
    const char *type;
    const char *values;
    const char *default_value;

    bool (*is_valid)(const char *);

    const char *description_short;
    const char *description_long;

} pcmk__cluster_option_t;

const char *pcmk__cluster_option(GHashTable *options,
                                 const pcmk__cluster_option_t *option_list,
                                 int len, const char *name);

gchar *pcmk__format_option_metadata(const char *name, const char *desc_short,
                                    const char *desc_long,
                                    pcmk__cluster_option_t *option_list,
                                    int len);

void pcmk__validate_cluster_options(GHashTable *options,
                                    pcmk__cluster_option_t *option_list,
                                    int len);

/*!
 * \internal
 * \brief Check whether a string represents a valid interval specification
 *
 * \param[in] value  String to validate
 *
 * \return \c true if \p value is a valid interval specification, or \c false
 *         otherwise
 */
static inline bool
pcmk__valid_interval_spec(const char *value)
{
    return pcmk__parse_interval_spec(value, NULL) == pcmk_rc_ok;
}

/*!
 * \internal
 * \brief Check whether a string represents a valid boolean value
 *
 * \param[in] value  String to validate
 *
 * \return \c true if \p value is a valid boolean value, or \c false otherwise
 */
static inline bool
pcmk__valid_boolean(const char *value)
{
    return crm_str_to_boolean(value, NULL) == 1;
}

/*!
 * \internal
 * \brief Check whether a string represents a valid integer
 *
 * Valid values include \c INFINITY, \c -INFINITY, and all 64-bit integers.
 *
 * \param[in] value  String to validate
 *
 * \return \c true if \p value is a valid integer, or \c false otherwise
 */
static inline bool
pcmk__valid_int(const char *value)
{
    return (value != NULL)
           && (pcmk_str_is_infinity(value)
               || pcmk_str_is_minus_infinity(value)
               || (pcmk__scan_ll(value, NULL, 0LL) == pcmk_rc_ok));
}

/*!
 * \internal
 * \brief Check whether a string represents a valid positive integer
 *
 * Valid values include \c INFINITY and all 64-bit positive integers.
 *
 * \param[in] value  String to validate
 *
 * \return \c true if \p value is a valid positive integer, or \c false
 *         otherwise
 */
static inline bool
pcmk__valid_positive_int(const char *value)
{
    long long num = 0LL;

    return pcmk_str_is_infinity(value)
           || ((pcmk__scan_ll(value, &num, 0LL) == pcmk_rc_ok)
               && (num > 0));
}

/*!
 * \internal
 * \brief Check whether a string represents a valid
 *        \c PCMK__OPT_NO_QUORUM_POLICY value
 *
 * \param[in] value  String to validate
 *
 * \return \c true if \p value is a valid \c PCMK__OPT_NO_QUORUM_POLICY value,
 *         or \c false otherwise
 */
static inline bool
pcmk__valid_no_quorum_policy(const char *value)
{
    return pcmk__strcase_any_of(value,
                                "stop", "freeze", "ignore", "demote", "suicide",
                                NULL);
}

/*!
 * \internal
 * \brief Check whether a string represents a valid percentage
 *
 * Valid values include floating-point numbers, with an optional trailing string
 * beginning with '%'.
 *
 * \param[in] value  String to validate
 *
 * \return \c true if \p value is a valid percentage value, or \c false
 *         otherwise
 */
static inline bool
pcmk__valid_percentage(const char *value)
{
    char *end = NULL;
    float number = strtof(value, &end);

    return ((end == NULL) || (end[0] == '%')) && (number >= 0);
}

bool pcmk__valid_script(const char *value);

// from watchdog.c
long pcmk__get_sbd_timeout(void);
bool pcmk__get_sbd_sync_resource_startup(void);
long pcmk__auto_watchdog_timeout(void);
bool pcmk__valid_sbd_timeout(const char *value);

// Constants for environment variable names
#define PCMK__ENV_AUTHKEY_LOCATION          "authkey_location"
#define PCMK__ENV_BLACKBOX                  "blackbox"
#define PCMK__ENV_CALLGRIND_ENABLED         "callgrind_enabled"
#define PCMK__ENV_CLUSTER_TYPE              "cluster_type"
#define PCMK__ENV_DEBUG                     "debug"
#define PCMK__ENV_DH_MAX_BITS               "dh_max_bits"
#define PCMK__ENV_DH_MIN_BITS               "dh_min_bits"
#define PCMK__ENV_FAIL_FAST                 "fail_fast"
#define PCMK__ENV_IPC_BUFFER                "ipc_buffer"
#define PCMK__ENV_IPC_TYPE                  "ipc_type"
#define PCMK__ENV_LOGFACILITY               "logfacility"
#define PCMK__ENV_LOGFILE                   "logfile"
#define PCMK__ENV_LOGFILE_MODE              "logfile_mode"
#define PCMK__ENV_LOGPRIORITY               "logpriority"
#define PCMK__ENV_NODE_ACTION_LIMIT         "node_action_limit"
#define PCMK__ENV_NODE_START_STATE          "node_start_state"
#define PCMK__ENV_PANIC_ACTION              "panic_action"
#define PCMK__ENV_PHYSICAL_HOST             "physical_host"
#define PCMK__ENV_REMOTE_ADDRESS            "remote_address"
#define PCMK__ENV_REMOTE_SCHEMA_DIR         "remote_schema_directory"
#define PCMK__ENV_REMOTE_PID1               "remote_pid1"
#define PCMK__ENV_REMOTE_PORT               "remote_port"
#define PCMK__ENV_RESPAWNED                 "respawned"
#define PCMK__ENV_SCHEMA_DIRECTORY          "schema_directory"
#define PCMK__ENV_SERVICE                   "service"
#define PCMK__ENV_STDERR                    "stderr"
#define PCMK__ENV_TLS_PRIORITIES            "tls_priorities"
#define PCMK__ENV_TRACE_BLACKBOX            "trace_blackbox"
#define PCMK__ENV_TRACE_FILES               "trace_files"
#define PCMK__ENV_TRACE_FORMATS             "trace_formats"
#define PCMK__ENV_TRACE_FUNCTIONS           "trace_functions"
#define PCMK__ENV_TRACE_TAGS                "trace_tags"
#define PCMK__ENV_VALGRIND_ENABLED          "valgrind_enabled"

// @COMPAT Drop at 3.0.0; default is plenty
#define PCMK__ENV_CIB_TIMEOUT               "cib_timeout"

// @COMPAT Drop at 3.0.0; likely last used in 1.1.24
#define PCMK__ENV_MCP                       "mcp"

// @COMPAT Drop at 3.0.0; added unused in 1.1.9
#define PCMK__ENV_QUORUM_TYPE               "quorum_type"

/* @COMPAT Drop at 3.0.0; added to debug shutdown issues when Pacemaker is
 * managed by systemd, but no longer useful.
 */
#define PCMK__ENV_SHUTDOWN_DELAY            "shutdown_delay"

// Constants for cluster option names
#define PCMK__OPT_BATCH_LIMIT               "batch-limit"
#define PCMK__OPT_CLUSTER_DELAY             "cluster-delay"
#define PCMK__OPT_CLUSTER_INFRASTRUCTURE    "cluster-infrastructure"
#define PCMK__OPT_CLUSTER_IPC_LIMIT         "cluster-ipc-limit"
#define PCMK__OPT_CLUSTER_NAME              "cluster-name"
#define PCMK__OPT_CLUSTER_RECHECK_INTERVAL  "cluster-recheck-interval"
#define PCMK__OPT_CONCURRENT_FENCING        "concurrent-fencing"
#define PCMK__OPT_DC_DEADTIME               "dc-deadtime"
#define PCMK__OPT_DC_VERSION                "dc-version"
#define PCMK__OPT_ELECTION_TIMEOUT          "election-timeout"
#define PCMK__OPT_ENABLE_ACL                "enable-acl"
#define PCMK__OPT_ENABLE_STARTUP_PROBES     "enable-startup-probes"
#define PCMK__OPT_FENCE_REACTION            "fence-reaction"
#define PCMK__OPT_HAVE_WATCHDOG             "have-watchdog"
#define PCMK__OPT_JOIN_FINALIZATION_TIMEOUT "join-finalization-timeout"
#define PCMK__OPT_JOIN_INTEGRATION_TIMEOUT  "join-integration-timeout"
#define PCMK__OPT_LOAD_THRESHOLD            "load-threshold"
#define PCMK__OPT_MAINTENANCE_MODE          "maintenance-mode"
#define PCMK__OPT_MIGRATION_LIMIT           "migration-limit"
#define PCMK__OPT_NO_QUORUM_POLICY          "no-quorum-policy"
#define PCMK__OPT_NODE_ACTION_LIMIT         "node-action-limit"
#define PCMK__OPT_NODE_HEALTH_BASE          "node-health-base"
#define PCMK__OPT_NODE_HEALTH_GREEN         "node-health-green"
#define PCMK__OPT_NODE_HEALTH_RED           "node-health-red"
#define PCMK__OPT_NODE_HEALTH_STRATEGY      "node-health-strategy"
#define PCMK__OPT_NODE_HEALTH_YELLOW        "node-health-yellow"
#define PCMK__OPT_NODE_PENDING_TIMEOUT      "node-pending-timeout"
#define PCMK__OPT_PE_ERROR_SERIES_MAX       "pe-error-series-max"
#define PCMK__OPT_PE_INPUT_SERIES_MAX       "pe-input-series-max"
#define PCMK__OPT_PE_WARN_SERIES_MAX        "pe-warn-series-max"
#define PCMK__OPT_PLACEMENT_STRATEGY        "placement-strategy"
#define PCMK__OPT_PRIORITY_FENCING_DELAY    "priority-fencing-delay"
#define PCMK__OPT_REMOVE_AFTER_STOP         "remove-after-stop"
#define PCMK__OPT_SHUTDOWN_ESCALATION       "shutdown-escalation"
#define PCMK__OPT_SHUTDOWN_LOCK             "shutdown-lock"
#define PCMK__OPT_SHUTDOWN_LOCK_LIMIT       "shutdown-lock-limit"
#define PCMK__OPT_START_FAILURE_IS_FATAL    "start-failure-is-fatal"
#define PCMK__OPT_STARTUP_FENCING           "startup-fencing"
#define PCMK__OPT_STONITH_ACTION            "stonith-action"
#define PCMK__OPT_STONITH_ENABLED           "stonith-enabled"
#define PCMK__OPT_STONITH_MAX_ATTEMPTS      "stonith-max-attempts"
#define PCMK__OPT_STONITH_TIMEOUT           "stonith-timeout"
#define PCMK__OPT_STONITH_WATCHDOG_TIMEOUT  "stonith-watchdog-timeout"
#define PCMK__OPT_STOP_ALL_RESOURCES        "stop-all-resources"
#define PCMK__OPT_STOP_ORPHAN_ACTIONS       "stop-orphan-actions"
#define PCMK__OPT_STOP_ORPHAN_RESOURCES     "stop-orphan-resources"
#define PCMK__OPT_SYMMETRIC_CLUSTER         "symmetric-cluster"
#define PCMK__OPT_TRANSITION_DELAY          "transition-delay"

// Constants for meta-attribute names
#define PCMK__META_ALLOW_UNHEALTHY_NODES    "allow-unhealthy-nodes"
#define PCMK__META_CLONE_INSTANCE_NUM       "clone"
#define PCMK__META_CLONE_MAX                "clone-max"
#define PCMK__META_CLONE_MIN                "clone-min"
#define PCMK__META_CLONE_NODE_MAX           "clone-node-max"
#define PCMK__META_CONTAINER_ATTR_TARGET    "container-attribute-target"
#define PCMK__META_ENABLED                  "enabled"
#define PCMK__META_FAILURE_TIMEOUT          "failure-timeout"
#define PCMK__META_INTERLEAVE               "interleave"
#define PCMK__META_IS_MANAGED               "is-managed"
#define PCMK__META_MIGRATION_THRESHOLD      "migration-threshold"
#define PCMK__META_ORDERED                  "ordered"
#define PCMK__META_PROMOTABLE               "promotable"
#define PCMK__META_PROMOTED_MAX             "promoted-max"
#define PCMK__META_PROMOTED_NODE_MAX        "promoted-node-max"
#define PCMK__META_RESTART_TYPE             "restart-type"
#define PCMK__META_TARGET_ROLE              "target-role"

// @COMPAT Deprecated alias for PCMK__META_PROMOTED_MAX since 2.0.0
#define PCMK__META_PROMOTED_MAX_LEGACY      "master-max"

// @COMPAT Deprecated alias for PCMK__META_PROMOTED_NODE_MAX since 2.0.0
#define PCMK__META_PROMOTED_NODE_MAX_LEGACY "master-node-max"

// Constants for enumerated values for various options
#define PCMK__VALUE_CLUSTER                 "cluster"
#define PCMK__VALUE_CUSTOM                  "custom"
#define PCMK__VALUE_FENCING                 "fencing"
#define PCMK__VALUE_GREEN                   "green"
#define PCMK__VALUE_LOCAL                   "local"
#define PCMK__VALUE_MIGRATE_ON_RED          "migrate-on-red"
#define PCMK__VALUE_NONE                    "none"
#define PCMK__VALUE_NOTHING                 "nothing"
#define PCMK__VALUE_ONLY_GREEN              "only-green"
#define PCMK__VALUE_PROGRESSIVE             "progressive"
#define PCMK__VALUE_QUORUM                  "quorum"
#define PCMK__VALUE_RED                     "red"
#define PCMK__VALUE_UNFENCING               "unfencing"
#define PCMK__VALUE_YELLOW                  "yellow"

#endif // PCMK__OPTIONS_INTERNAL__H
