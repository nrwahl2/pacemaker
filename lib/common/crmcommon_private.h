/*
 * Copyright 2018-2024 the Pacemaker project contributors
 *
 * The version control history for this file may have further details.
 *
 * This source code is licensed under the GNU Lesser General Public License
 * version 2.1 or later (LGPLv2.1+) WITHOUT ANY WARRANTY.
 */

#ifndef CRMCOMMON_PRIVATE__H
#  define CRMCOMMON_PRIVATE__H

/* This header is for the sole use of libcrmcommon, so that functions can be
 * declared with G_GNUC_INTERNAL for efficiency.
 */

#include <stdint.h>         // uint8_t, uint32_t
#include <stdbool.h>        // bool
#include <sys/types.h>      // size_t
#include <glib.h>           // GList
#include <libxml/tree.h>    // xmlNode, xmlAttr
#include <qb/qbipcc.h>      // struct qb_ipc_response_header

// Decent chunk size for processing large amounts of data
#define PCMK__BUFFER_SIZE 4096

#if defined(PCMK__UNIT_TESTING)
#undef G_GNUC_INTERNAL
#define G_GNUC_INTERNAL
#endif

/* When deleting portions of an XML tree, we keep a record so we can know later
 * (e.g. when checking differences) that something was deleted.
 */
typedef struct pcmk__deleted_xml_s {
        char *path;
        int position;
} pcmk__deleted_xml_t;

typedef struct xml_node_private_s {
        long check;
        uint32_t flags;
} xml_node_private_t;

typedef struct xml_doc_private_s {
        long check;
        uint32_t flags;
        char *user;
        GList *acls;
        GList *deleted_objs; // List of pcmk__deleted_xml_t
} xml_doc_private_t;

#define pcmk__set_xml_flags(xml_priv, flags_to_set) do {                    \
        (xml_priv)->flags = pcmk__set_flags_as(__func__, __LINE__,          \
            LOG_NEVER, "XML", "XML node", (xml_priv)->flags,                \
            (flags_to_set), #flags_to_set);                                 \
    } while (0)

#define pcmk__clear_xml_flags(xml_priv, flags_to_clear) do {                \
        (xml_priv)->flags = pcmk__clear_flags_as(__func__, __LINE__,        \
            LOG_NEVER, "XML", "XML node", (xml_priv)->flags,                \
            (flags_to_clear), #flags_to_clear);                             \
    } while (0)

G_GNUC_INTERNAL
void pcmk__xml2text(const xmlNode *data, uint32_t options, GString *buffer,
                    int depth);

G_GNUC_INTERNAL
bool pcmk__tracking_xml_changes(xmlNode *xml, bool lazy);

G_GNUC_INTERNAL
void pcmk__mark_xml_created(xmlNode *xml);

G_GNUC_INTERNAL
int pcmk__xml_position(const xmlNode *xml,
                       enum xml_private_flags ignore_if_set);

G_GNUC_INTERNAL
xmlNode *pcmk__xml_match(const xmlNode *haystack, const xmlNode *needle,
                         bool exact);

G_GNUC_INTERNAL
void pcmk__xml_update(xmlNode *parent, xmlNode *target, xmlNode *update,
                      bool as_diff);

G_GNUC_INTERNAL
xmlNode *pcmk__xc_match(const xmlNode *root, const xmlNode *search_comment,
                        bool exact);

G_GNUC_INTERNAL
void pcmk__xc_update(xmlNode *parent, xmlNode *target, xmlNode *update);

G_GNUC_INTERNAL
void pcmk__free_acls(GList *acls);

G_GNUC_INTERNAL
void pcmk__unpack_acl(xmlNode *source, xmlNode *target, const char *user);

G_GNUC_INTERNAL
bool pcmk__is_user_in_group(const char *user, const char *group);

G_GNUC_INTERNAL
void pcmk__apply_acl(xmlNode *xml);

G_GNUC_INTERNAL
void pcmk__apply_creation_acl(xmlNode *xml, bool check_top);

G_GNUC_INTERNAL
void pcmk__mark_xml_attr_dirty(xmlAttr *a);

G_GNUC_INTERNAL
bool pcmk__xa_filterable(const char *name);

G_GNUC_INTERNAL
void pcmk__log_xmllib_err(void *ctx, const char *fmt, ...)
G_GNUC_PRINTF(2, 3);

G_GNUC_INTERNAL
void pcmk__mark_xml_node_dirty(xmlNode *xml);

G_GNUC_INTERNAL
bool pcmk__marked_as_deleted(xmlAttrPtr a, void *user_data);

G_GNUC_INTERNAL
void pcmk__dump_xml_attr(const xmlAttr *attr, GString *buffer);

/*
 * IPC
 */

#define PCMK__IPC_VERSION 1

#define PCMK__CONTROLD_API_MAJOR "1"
#define PCMK__CONTROLD_API_MINOR "0"

// IPC behavior that varies by daemon
typedef struct pcmk__ipc_methods_s {
    /*!
     * \internal
     * \brief Allocate any private data needed by daemon IPC
     *
     * \param[in,out] api  IPC API connection
     *
     * \return Standard Pacemaker return code
     */
    int (*new_data)(pcmk_ipc_api_t *api);

    /*!
     * \internal
     * \brief Free any private data used by daemon IPC
     *
     * \param[in,out] api_data  Data allocated by new_data() method
     */
    void (*free_data)(void *api_data);

    /*!
     * \internal
     * \brief Perform daemon-specific handling after successful connection
     *
     * Some daemons require clients to register before sending any other
     * commands. The controller requires a CRM_OP_HELLO (with no reply), and
     * the CIB manager, executor, and fencer require a CRM_OP_REGISTER (with a
     * reply). Ideally this would be consistent across all daemons, but for now
     * this allows each to do its own authorization.
     *
     * \param[in,out] api  IPC API connection
     *
     * \return Standard Pacemaker return code
     */
    int (*post_connect)(pcmk_ipc_api_t *api);

    /*!
     * \internal
     * \brief Check whether an IPC request results in a reply
     *
     * \param[in,out] api      IPC API connection
     * \param[in]     request  IPC request XML
     *
     * \return true if request would result in an IPC reply, false otherwise
     */
    bool (*reply_expected)(pcmk_ipc_api_t *api, const xmlNode *request);

    /*!
     * \internal
     * \brief Perform daemon-specific handling of an IPC message
     *
     * \param[in,out] api  IPC API connection
     * \param[in,out] msg  Message read from IPC connection
     *
     * \return true if more IPC reply messages should be expected
     */
    bool (*dispatch)(pcmk_ipc_api_t *api, xmlNode *msg);

    /*!
     * \internal
     * \brief Perform daemon-specific handling of an IPC disconnect
     *
     * \param[in,out] api  IPC API connection
     */
    void (*post_disconnect)(pcmk_ipc_api_t *api);
} pcmk__ipc_methods_t;

// Implementation of pcmk_ipc_api_t
struct pcmk_ipc_api_s {
    enum pcmk_ipc_server server;          // Daemon this IPC API instance is for
    enum pcmk_ipc_dispatch dispatch_type; // How replies should be dispatched
    size_t ipc_size_max;                  // maximum IPC buffer size
    crm_ipc_t *ipc;                       // IPC connection
    mainloop_io_t *mainloop_io;     // If using mainloop, I/O source for IPC
    bool free_on_disconnect;        // Whether disconnect should free object
    pcmk_ipc_callback_t cb;         // Caller-registered callback (if any)
    void *user_data;                // Caller-registered data (if any)
    void *api_data;                 // For daemon-specific use
    pcmk__ipc_methods_t *cmds;      // Behavior that varies by daemon
};

typedef struct pcmk__ipc_header_s {
    struct qb_ipc_response_header qb;
    uint32_t size_uncompressed;
    uint32_t size_compressed;
    uint32_t flags;
    uint8_t version;
} pcmk__ipc_header_t;

G_GNUC_INTERNAL
int pcmk__send_ipc_request(pcmk_ipc_api_t *api, const xmlNode *request);

G_GNUC_INTERNAL
void pcmk__call_ipc_callback(pcmk_ipc_api_t *api,
                             enum pcmk_ipc_event event_type,
                             crm_exit_t status, void *event_data);

G_GNUC_INTERNAL
unsigned int pcmk__ipc_buffer_size(unsigned int max);

G_GNUC_INTERNAL
bool pcmk__valid_ipc_header(const pcmk__ipc_header_t *header);

G_GNUC_INTERNAL
pcmk__ipc_methods_t *pcmk__attrd_api_methods(void);

G_GNUC_INTERNAL
pcmk__ipc_methods_t *pcmk__controld_api_methods(void);

G_GNUC_INTERNAL
pcmk__ipc_methods_t *pcmk__pacemakerd_api_methods(void);

G_GNUC_INTERNAL
pcmk__ipc_methods_t *pcmk__schedulerd_api_methods(void);


/*
 * Logging
 */

//! XML is newly created
#define PCMK__XML_PREFIX_CREATED "++"

//! XML has been deleted
#define PCMK__XML_PREFIX_DELETED "--"

//! XML has been modified
#define PCMK__XML_PREFIX_MODIFIED "+ "

//! XML has been moved
#define PCMK__XML_PREFIX_MOVED "+~"

/*
 * Output
 */
G_GNUC_INTERNAL
int pcmk__bare_output_new(pcmk__output_t **out, const char *fmt_name,
                          const char *filename, char **argv);

G_GNUC_INTERNAL
void pcmk__register_option_messages(pcmk__output_t *out);

G_GNUC_INTERNAL
void pcmk__register_patchset_messages(pcmk__output_t *out);


/*
 * Utils
 */
#define PCMK__PW_BUFFER_LEN 500


/*
 * Schemas
 */
typedef struct {
    unsigned char v[2];
} pcmk__schema_version_t;

enum pcmk__schema_validator {
    pcmk__schema_validator_none,
    pcmk__schema_validator_rng
};

typedef struct {
    char *name;
    char *transform;
    void *cache;
    enum pcmk__schema_validator validator;
    pcmk__schema_version_t version;
    char *transform_enter;
    bool transform_onleave;
} pcmk__schema_t;

G_GNUC_INTERNAL
int pcmk__find_x_0_schema_index(GList *schemas);


#endif  // CRMCOMMON_PRIVATE__H
