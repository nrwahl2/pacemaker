/*
 * Copyright 2004-2023 the Pacemaker project contributors
 *
 * The version control history for this file may have further details.
 *
 * This source code is licensed under the GNU Lesser General Public License
 * version 2.1 or later (LGPLv2.1+) WITHOUT ANY WARRANTY.
 */

#ifndef CIB_INTERNAL__H
#  define CIB_INTERNAL__H
#  include <crm/cib.h>
#  include <crm/common/ipc_internal.h>
#  include <crm/common/output_internal.h>

// Request types for CIB manager IPC/CPG
#define PCMK__CIB_REQUEST_SECONDARY         "cib_slave"
#define PCMK__CIB_REQUEST_ALL_SECONDARY     "cib_slave_all"
#define PCMK__CIB_REQUEST_PRIMARY           "cib_master"
#define PCMK__CIB_REQUEST_SYNC_TO_ALL       "cib_sync"
#define PCMK__CIB_REQUEST_SYNC_TO_ONE       "cib_sync_one"
#define PCMK__CIB_REQUEST_IS_PRIMARY        "cib_ismaster"
#define PCMK__CIB_REQUEST_BUMP              "cib_bump"
#define PCMK__CIB_REQUEST_QUERY             "cib_query"
#define PCMK__CIB_REQUEST_CREATE            "cib_create"
#define PCMK__CIB_REQUEST_MODIFY            "cib_modify"
#define PCMK__CIB_REQUEST_DELETE            "cib_delete"
#define PCMK__CIB_REQUEST_ERASE             "cib_erase"
#define PCMK__CIB_REQUEST_REPLACE           "cib_replace"
#define PCMK__CIB_REQUEST_APPLY_PATCH       "cib_apply_diff"
#define PCMK__CIB_REQUEST_UPGRADE           "cib_upgrade"
#define PCMK__CIB_REQUEST_ABS_DELETE        "cib_delete_alt"
#define PCMK__CIB_REQUEST_NOOP              "noop"
#define PCMK__CIB_REQUEST_SHUTDOWN          "cib_shutdown_req"
#define PCMK__CIB_REQUEST_CLUSTER_LOCK      "cib_cluster_lock"
#define PCMK__CIB_REQUEST_CLUSTER_UNLOCK    "cib_cluster_unlock"

#  define F_CIB_CLIENTID  "cib_clientid"
#  define F_CIB_CALLOPTS  "cib_callopt"
#  define F_CIB_CALLID    "cib_callid"
#  define F_CIB_CALLDATA  "cib_calldata"
#  define F_CIB_OPERATION "cib_op"
#  define F_CIB_ISREPLY   "cib_isreplyto"
#  define F_CIB_SECTION   "cib_section"
#  define F_CIB_HOST	"cib_host"
#  define F_CIB_RC	"cib_rc"
#  define F_CIB_UPGRADE_RC      "cib_upgrade_rc"
#  define F_CIB_DELEGATED	"cib_delegated_from"
#  define F_CIB_OBJID	"cib_object"
#  define F_CIB_OBJTYPE	"cib_object_type"
#  define F_CIB_EXISTING	"cib_existing_object"
#  define F_CIB_SEENCOUNT	"cib_seen"
#  define F_CIB_TIMEOUT	"cib_timeout"
#  define F_CIB_UPDATE	"cib_update"
#  define F_CIB_CALLBACK_TOKEN	"cib_async_id"
#  define F_CIB_GLOBAL_UPDATE	"cib_update"
#  define F_CIB_UPDATE_RESULT	"cib_update_result"
#  define F_CIB_CLIENTNAME	"cib_clientname"
#  define F_CIB_NOTIFY_TYPE	"cib_notify_type"
#  define F_CIB_NOTIFY_ACTIVATE	"cib_notify_activate"
#  define F_CIB_UPDATE_DIFF	"cib_update_diff"
#  define F_CIB_USER		"cib_user"
#  define F_CIB_LOCAL_NOTIFY_ID	"cib_local_notify_id"
#  define F_CIB_PING_ID         "cib_ping_id"
#  define F_CIB_SCHEMA_MAX      "cib_schema_max"
#  define F_CIB_CHANGE_SECTION  "cib_change_section"

#  define T_CIB			"cib"
#  define T_CIB_NOTIFY		"cib_notify"
/* notify sub-types */
#  define T_CIB_PRE_NOTIFY	"cib_pre_notify"
#  define T_CIB_POST_NOTIFY	"cib_post_notify"
#  define T_CIB_UPDATE_CONFIRM	"cib_update_confirmation"
#  define T_CIB_REPLACE_NOTIFY	"cib_refresh_notify"

/*!
 * \internal
 * \enum cib_change_section_info
 * \brief Flags to indicate which sections of the CIB have changed
 */
enum cib_change_section_info {
    cib_change_section_none     = 0,        //!< No sections have changed
    cib_change_section_nodes    = (1 << 0), //!< The nodes section has changed
    cib_change_section_alerts   = (1 << 1), //!< The alerts section has changed
    cib_change_section_status   = (1 << 2), //!< The status section has changed
};


gboolean cib_diff_version_details(xmlNode * diff, int *admin_epoch, int *epoch, int *updates,
                                  int *_admin_epoch, int *_epoch, int *_updates);

gboolean cib_read_config(GHashTable * options, xmlNode * current_cib);
void verify_cib_options(GHashTable * options);
gboolean cib_internal_config_changed(xmlNode * diff);

typedef struct cib_notify_client_s {
    const char *event;
    const char *obj_id;         /* implement one day */
    const char *obj_type;       /* implement one day */
    void (*callback) (const char *event, xmlNode * msg);

} cib_notify_client_t;

typedef struct cib_callback_client_s {
    void (*callback) (xmlNode *, int, int, xmlNode *, void *);
    const char *id;
    void *user_data;
    gboolean only_success;
    struct timer_rec_s *timer;
    void (*free_func)(void *);
} cib_callback_client_t;

struct timer_rec_s {
    int call_id;
    int timeout;
    guint ref;
    cib_t *cib;
};

#define cib__set_call_options(cib_call_opts, call_for, flags_to_set) do {   \
        cib_call_opts = pcmk__set_flags_as(__func__, __LINE__,              \
            LOG_TRACE, "CIB call", (call_for), (cib_call_opts),             \
            (flags_to_set), #flags_to_set); \
    } while (0)

#define cib__clear_call_options(cib_call_opts, call_for, flags_to_clear) do {  \
        cib_call_opts = pcmk__clear_flags_as(__func__, __LINE__,               \
            LOG_TRACE, "CIB call", (call_for), (cib_call_opts),                \
            (flags_to_clear), #flags_to_clear);                                \
    } while (0)

typedef int (*cib_op_t) (const char *, int, const char *, xmlNode *,
                         xmlNode *, xmlNode *, xmlNode **, xmlNode **);

cib_t *cib_new_variant(void);

int cib_perform_op(const char *op, int call_options, cib_op_t * fn, gboolean is_query,
                   const char *section, xmlNode * req, xmlNode * input,
                   gboolean manage_counters, gboolean * config_changed,
                   xmlNode * current_cib, xmlNode ** result_cib, xmlNode ** diff,
                   xmlNode ** output);

xmlNode *cib_create_op(int call_id, const char *token, const char *op, const char *host,
                       const char *section, xmlNode * data, int call_options,
                       const char *user_name);

void cib_native_callback(cib_t * cib, xmlNode * msg, int call_id, int rc);
void cib_native_notify(gpointer data, gpointer user_data);
int cib_native_register_notification(cib_t * cib, const char *callback, int enabled);
gboolean cib_client_register_callback(cib_t * cib, int call_id, int timeout, gboolean only_success,
                                      void *user_data, const char *callback_name,
                                      void (*callback) (xmlNode *, int, int, xmlNode *, void *));
gboolean cib_client_register_callback_full(cib_t *cib, int call_id,
                                           int timeout, gboolean only_success,
                                           void *user_data,
                                           const char *callback_name,
                                           void (*callback)(xmlNode *, int, int,
                                                            xmlNode *, void *),
                                           void (*free_func)(void *));

int cib_process_query(const char *op, int options, const char *section, xmlNode * req,
                      xmlNode * input, xmlNode * existing_cib, xmlNode ** result_cib,
                      xmlNode ** answer);

int cib_process_erase(const char *op, int options, const char *section, xmlNode * req,
                      xmlNode * input, xmlNode * existing_cib, xmlNode ** result_cib,
                      xmlNode ** answer);

int cib_process_bump(const char *op, int options, const char *section, xmlNode * req,
                     xmlNode * input, xmlNode * existing_cib, xmlNode ** result_cib,
                     xmlNode ** answer);

int cib_process_replace(const char *op, int options, const char *section, xmlNode * req,
                        xmlNode * input, xmlNode * existing_cib, xmlNode ** result_cib,
                        xmlNode ** answer);

int cib_process_create(const char *op, int options, const char *section, xmlNode * req,
                       xmlNode * input, xmlNode * existing_cib, xmlNode ** result_cib,
                       xmlNode ** answer);

int cib_process_modify(const char *op, int options, const char *section, xmlNode * req,
                       xmlNode * input, xmlNode * existing_cib, xmlNode ** result_cib,
                       xmlNode ** answer);

int cib_process_delete(const char *op, int options, const char *section, xmlNode * req,
                       xmlNode * input, xmlNode * existing_cib, xmlNode ** result_cib,
                       xmlNode ** answer);

int cib_process_diff(const char *op, int options, const char *section, xmlNode * req,
                     xmlNode * input, xmlNode * existing_cib, xmlNode ** result_cib,
                     xmlNode ** answer);

int cib_process_upgrade(const char *op, int options, const char *section, xmlNode * req,
                        xmlNode * input, xmlNode * existing_cib, xmlNode ** result_cib,
                        xmlNode ** answer);

/*!
 * \internal
 * \brief Query or modify a CIB
 *
 * \param[in]     op            PCMK__CIB_REQUEST_* operation to be performed
 * \param[in]     options       Flag set of \c cib_call_options
 * \param[in]     section       XPath to query or modify
 * \param[in]     req           unused
 * \param[in]     input         Portion of CIB to modify (used with
 *                              PCMK__CIB_REQUEST_CREATE,
 *                              PCMK__CIB_REQUEST_MODIFY, and
 *                              PCMK__CIB_REQUEST_REPLACE)
 * \param[in,out] existing_cib  Input CIB (used with PCMK__CIB_REQUEST_QUERY)
 * \param[in,out] result_cib    CIB copy to make changes in (used with
 *                              PCMK__CIB_REQUEST_CREATE,
 *                              PCMK__CIB_REQUEST_MODIFY,
 *                              PCMK__CIB_REQUEST_DELETE, and
 *                              PCMK__CIB_REQUEST_REPLACE)
 * \param[out]    answer        Query result (used with PCMK__CIB_REQUEST_QUERY)
 *
 * \return Legacy Pacemaker return code
 */
int cib_process_xpath(const char *op, int options, const char *section,
                      const xmlNode *req, xmlNode *input, xmlNode *existing_cib,
                      xmlNode **result_cib, xmlNode ** answer);

gboolean cib_config_changed(xmlNode * last, xmlNode * next, xmlNode ** diff);
gboolean update_results(xmlNode * failed, xmlNode * target, const char *operation, int return_code);
int cib_update_counter(xmlNode * xml_obj, const char *field, gboolean reset);

int cib_internal_op(cib_t * cib, const char *op, const char *host,
                    const char *section, xmlNode * data,
                    xmlNode ** output_data, int call_options, const char *user_name);


int cib_file_read_and_verify(const char *filename, const char *sigfile,
                             xmlNode **root);
int cib_file_write_with_digest(xmlNode *cib_root, const char *cib_dirname,
                               const char *cib_filename);

void cib__set_output(cib_t *cib, pcmk__output_t *out);

cib_callback_client_t* cib__lookup_id (int call_id);

/*!
 * \internal
 * \brief Connect to, query, and optionally disconnect from the CIB
 *
 * Open a read-write connection to the CIB manager if an already connected
 * client is not passed in. Then query the CIB and store the resulting XML.
 * Finally, disconnect if the CIB connection isn't being returned to the caller.
 *
 * \param[in,out] out         Output object (may be \p NULL)
 * \param[in,out] cib         If not \p NULL, where to store CIB connection
 * \param[out]    cib_object  Where to store query result
 *
 * \return Standard Pacemaker return code
 *
 * \note If \p cib is not \p NULL, the caller is responsible for freeing \p *cib
 *       using \p cib_delete().
 * \note If \p *cib points to an existing \p cib_t object, this function will
 *       reuse it instead of creating a new one. If the existing client is
 *       already connected, the connection will be reused, even if it's
 *       read-only.
 */
int cib__signon_query(pcmk__output_t *out, cib_t **cib, xmlNode **cib_object);

int cib__clean_up_connection(cib_t **cib);

int cib__update_node_attr(pcmk__output_t *out, cib_t *cib, int call_options,
                          const char *section, const char *node_uuid, const char *set_type,
                          const char *set_name, const char *attr_id, const char *attr_name,
                          const char *attr_value, const char *user_name,
                          const char *node_type);

int cib__get_node_attrs(pcmk__output_t *out, cib_t *cib, const char *section,
                        const char *node_uuid, const char *set_type, const char *set_name,
                        const char *attr_id, const char *attr_name, const char *user_name,
                        xmlNode **result);

int cib__delete_node_attr(pcmk__output_t *out, cib_t *cib, int options,
                          const char *section, const char *node_uuid, const char *set_type,
                          const char *set_name, const char *attr_id, const char *attr_name,
                          const char *attr_value, const char *user_name);

#endif
