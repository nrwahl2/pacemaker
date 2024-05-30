/*
 * Copyright 2017-2024 the Pacemaker project contributors
 *
 * The version control history for this file may have further details.
 *
 * This source code is licensed under the GNU Lesser General Public License
 * version 2.1 or later (LGPLv2.1+) WITHOUT ANY WARRANTY.
 */

#ifndef PCMK__XML_INTERNAL__H
#define PCMK__XML_INTERNAL__H

/*
 * Internal-only wrappers for and extensions to libxml2 (libxslt)
 */

#include <stdlib.h>
#include <stdint.h>   // uint32_t
#include <stdio.h>
#include <string.h>

#include <crm/crm.h>  /* transitively imports qblog.h */
#include <crm/common/output_internal.h>
#include <crm/common/xml_io_internal.h>
#include <crm/common/xml_names_internal.h>    // PCMK__XE_PROMOTABLE_LEGACY

#include <libxml/relaxng.h>

/*!
 * \brief Base for directing lib{xml2,xslt} log into standard libqb backend
 *
 * This macro implements the core of what can be needed for directing
 * libxml2 or libxslt error messaging into standard, preconfigured
 * libqb-backed log stream.
 *
 * It's a bit unfortunate that libxml2 (and more sparsely, also libxslt)
 * emits a single message by chunks (location is emitted separatedly from
 * the message itself), so we have to take the effort to combine these
 * chunks back to single message.  Whether to do this or not is driven
 * with \p dechunk toggle.
 *
 * The form of a macro was chosen for implicit deriving of __FILE__, etc.
 * and also because static dechunking buffer should be differentiated per
 * library (here we assume different functions referring to this macro
 * will not ever be using both at once), preferably also per-library
 * context of use to avoid clashes altogether.
 *
 * Note that we cannot use qb_logt, because callsite data have to be known
 * at the moment of compilation, which it is not always the case -- xml_log
 * (and unfortunately there's no clear explanation of the fail to compile).
 *
 * Also note that there's no explicit guard against said libraries producing
 * never-newline-terminated chunks (which would just keep consuming memory),
 * as it's quite improbable.  Termination of the program in between the
 * same-message chunks will raise a flag with valgrind and the likes, though.
 *
 * And lastly, regarding how dechunking combines with other non-message
 * parameters -- for \p priority, most important running specification
 * wins (possibly elevated to LOG_ERR in case of nonconformance with the
 * newline-termination "protocol"), \p dechunk is expected to always be
 * on once it was at the start, and the rest (\p postemit and \p prefix)
 * are picked directly from the last chunk entry finalizing the message
 * (also reasonable to always have it the same with all related entries).
 *
 * \param[in] priority Syslog priority for the message to be logged
 * \param[in] dechunk  Whether to dechunk new-line terminated message
 * \param[in] postemit Code to be executed once message is sent out
 * \param[in] prefix   How to prefix the message or NULL for raw passing
 * \param[in] fmt      Format string as with printf-like functions
 * \param[in] ap       Variable argument list to supplement \p fmt format string
 */
#define PCMK__XML_LOG_BASE(priority, dechunk, postemit, prefix, fmt, ap)        \
do {                                                                            \
    if (!(dechunk) && (prefix) == NULL) {  /* quick pass */                     \
        qb_log_from_external_source_va(__func__, __FILE__, (fmt),               \
                                       (priority), __LINE__, 0, (ap));          \
        (void) (postemit);                                                      \
    } else {                                                                    \
        int CXLB_len = 0;                                                       \
        char *CXLB_buf = NULL;                                                  \
        static int CXLB_buffer_len = 0;                                         \
        static char *CXLB_buffer = NULL;                                        \
        static uint8_t CXLB_priority = 0;                                       \
                                                                                \
        CXLB_len = vasprintf(&CXLB_buf, (fmt), (ap));                           \
                                                                                \
        if (CXLB_len <= 0 || CXLB_buf[CXLB_len - 1] == '\n' || !(dechunk)) {    \
            if (CXLB_len < 0) {                                                 \
                CXLB_buf = (char *) "LOG CORRUPTION HAZARD"; /*we don't modify*/\
                CXLB_priority = QB_MIN(CXLB_priority, LOG_ERR);                 \
            } else if (CXLB_len > 0 /* && (dechunk) */                          \
                       && CXLB_buf[CXLB_len - 1] == '\n') {                     \
                CXLB_buf[CXLB_len - 1] = '\0';                                  \
            }                                                                   \
            if (CXLB_buffer) {                                                  \
                qb_log_from_external_source(__func__, __FILE__, "%s%s%s",       \
                                            CXLB_priority, __LINE__, 0,         \
                                            (prefix) != NULL ? (prefix) : "",   \
                                            CXLB_buffer, CXLB_buf);             \
                free(CXLB_buffer);                                              \
            } else {                                                            \
                qb_log_from_external_source(__func__, __FILE__, "%s%s",         \
                                            (priority), __LINE__, 0,            \
                                            (prefix) != NULL ? (prefix) : "",   \
                                            CXLB_buf);                          \
            }                                                                   \
            if (CXLB_len < 0) {                                                 \
                CXLB_buf = NULL;  /* restore temporary override */              \
            }                                                                   \
            CXLB_buffer = NULL;                                                 \
            CXLB_buffer_len = 0;                                                \
            (void) (postemit);                                                  \
                                                                                \
        } else if (CXLB_buffer == NULL) {                                       \
            CXLB_buffer_len = CXLB_len;                                         \
            CXLB_buffer = CXLB_buf;                                             \
            CXLB_buf = NULL;                                                    \
            CXLB_priority = (priority);  /* remember as a running severest */   \
                                                                                \
        } else {                                                                \
            CXLB_buffer = realloc(CXLB_buffer, 1 + CXLB_buffer_len + CXLB_len); \
            memcpy(CXLB_buffer + CXLB_buffer_len, CXLB_buf, CXLB_len);          \
            CXLB_buffer_len += CXLB_len;                                        \
            CXLB_buffer[CXLB_buffer_len] = '\0';                                \
            CXLB_priority = QB_MIN(CXLB_priority, (priority));  /* severest? */ \
        }                                                                       \
        free(CXLB_buf);                                                         \
    }                                                                           \
} while (0)

/*
 * \enum pcmk__xml_fmt_options
 * \brief Bit flags to control format in XML logs and dumps
 */
enum pcmk__xml_fmt_options {
    //! Exclude certain XML attributes (for calculating digests)
    pcmk__xml_fmt_filtered   = (1 << 0),

    //! Include indentation and newlines
    pcmk__xml_fmt_pretty     = (1 << 1),

    //! Include the opening tag of an XML element, and include XML comments
    pcmk__xml_fmt_open       = (1 << 3),

    //! Include the children of an XML element
    pcmk__xml_fmt_children   = (1 << 4),

    //! Include the closing tag of an XML element
    pcmk__xml_fmt_close      = (1 << 5),

    // @COMPAT Can we start including text nodes unconditionally?
    //! Include XML text nodes
    pcmk__xml_fmt_text       = (1 << 6),

    // @COMPAT Remove when v1 patchsets are removed
    //! Log a created XML subtree
    pcmk__xml_fmt_diff_plus  = (1 << 7),

    // @COMPAT Remove when v1 patchsets are removed
    //! Log a removed XML subtree
    pcmk__xml_fmt_diff_minus = (1 << 8),

    // @COMPAT Remove when v1 patchsets are removed
    //! Log a minimal version of an XML diff (only showing the changes)
    pcmk__xml_fmt_diff_short = (1 << 9),
};

void pcmk__xml_init(void);
void pcmk__xml_cleanup(void);

int pcmk__xml_show(pcmk__output_t *out, const char *prefix, const xmlNode *data,
                   int depth, uint32_t options);
int pcmk__xml_show_changes(pcmk__output_t *out, const xmlNode *xml);

/* XML search strings for guest, remote and pacemaker_remote nodes */

/* search string to find CIB resources entries for cluster nodes */
#define PCMK__XP_MEMBER_NODE_CONFIG                                 \
    "//" PCMK_XE_CIB "/" PCMK_XE_CONFIGURATION "/" PCMK_XE_NODES    \
    "/" PCMK_XE_NODE                                                \
    "[not(@" PCMK_XA_TYPE ") or @" PCMK_XA_TYPE "='" PCMK_VALUE_MEMBER "']"

/* search string to find CIB resources entries for guest nodes */
#define PCMK__XP_GUEST_NODE_CONFIG \
    "//" PCMK_XE_CIB "//" PCMK_XE_CONFIGURATION "//" PCMK_XE_PRIMITIVE  \
    "//" PCMK_XE_META_ATTRIBUTES "//" PCMK_XE_NVPAIR                    \
    "[@" PCMK_XA_NAME "='" PCMK_META_REMOTE_NODE "']"

/* search string to find CIB resources entries for remote nodes */
#define PCMK__XP_REMOTE_NODE_CONFIG                                     \
    "//" PCMK_XE_CIB "//" PCMK_XE_CONFIGURATION "//" PCMK_XE_PRIMITIVE  \
    "[@" PCMK_XA_TYPE "='" PCMK_VALUE_REMOTE "']"                       \
    "[@" PCMK_XA_PROVIDER "='pacemaker']"

/* search string to find CIB node status entries for pacemaker_remote nodes */
#define PCMK__XP_REMOTE_NODE_STATUS                                 \
    "//" PCMK_XE_CIB "//" PCMK_XE_STATUS "//" PCMK__XE_NODE_STATE   \
    "[@" PCMK_XA_REMOTE_NODE "='" PCMK_VALUE_TRUE "']"
/*!
 * \internal
 * \brief Serialize XML (using libxml) into provided descriptor
 *
 * \param[in] fd  File descriptor to (piece-wise) write to
 * \param[in] cur XML subtree to proceed
 * 
 * \return a standard Pacemaker return code
 */
int pcmk__xml2fd(int fd, xmlNode *cur);

enum pcmk__xml_artefact_ns {
    pcmk__xml_artefact_ns_legacy_rng = 1,
    pcmk__xml_artefact_ns_legacy_xslt,
    pcmk__xml_artefact_ns_base_rng,
    pcmk__xml_artefact_ns_base_xslt,
};

void pcmk__strip_xml_text(xmlNode *xml);
const char *pcmk__xe_add_last_written(xmlNode *xe);

xmlNode *pcmk__xe_first_child(const xmlNode *parent, const char *node_name,
                              const char *attr_n, const char *attr_v);


xmlNode *pcmk__xe_resolve_idref(xmlNode *xml, xmlNode *search);

void pcmk__xe_remove_attr(xmlNode *element, const char *name);
bool pcmk__xe_remove_attr_cb(xmlNode *xml, void *user_data);
void pcmk__xe_remove_matching_attrs(xmlNode *element,
                                    bool (*match)(xmlAttrPtr, void *),
                                    void *user_data);
int pcmk__xe_delete_match(xmlNode *xml, xmlNode *search);
int pcmk__xe_replace_match(xmlNode *xml, xmlNode *replace);
int pcmk__xe_update_match(xmlNode *xml, xmlNode *update, uint32_t flags);

GString *pcmk__element_xpath(const xmlNode *xml);

/*!
 * \internal
 * \enum pcmk__xml_escape_type
 * \brief Indicators of which XML characters to escape
 *
 * XML allows the escaping of special characters by replacing them with entity
 * references (for example, <tt>"&quot;"</tt>) or character references (for
 * example, <tt>"&#13;"</tt>).
 *
 * The special characters <tt>'&'</tt> (except as the beginning of an entity
 * reference) and <tt>'<'</tt> are not allowed in their literal forms in XML
 * character data. Character data is non-markup text (for example, the content
 * of a text node). <tt>'>'</tt> is allowed under most circumstances; we escape
 * it for safety and symmetry.
 *
 * For more details, see the "Character Data and Markup" section of the XML
 * spec, currently section 2.4:
 * https://www.w3.org/TR/xml/#dt-markup
 *
 * Attribute values are handled specially.
 * * If an attribute value is delimited by single quotes, then single quotes
 *   must be escaped within the value.
 * * Similarly, if an attribute value is delimited by double quotes, then double
 *   quotes must be escaped within the value.
 * * A conformant XML processor replaces a literal whitespace character (tab,
 *   newline, carriage return, space) in an attribute value with a space
 *   (\c '#x20') character. However, a reference to a whitespace character (for
 *   example, \c "&#x0A;" for \c '\n') does not get replaced.
 *   * For more details, see the "Attribute-Value Normalization" section of the
 *     XML spec, currently section 3.3.3. Note that the default attribute type
 *     is CDATA; we don't deal with NMTOKENS, etc.:
 *     https://www.w3.org/TR/xml/#AVNormalize
 *
 * Pacemaker always delimits attribute values with double quotes, so there's no
 * need to escape single quotes.
 *
 * Newlines and tabs should be escaped in attribute values when XML is
 * serialized to text, so that future parsing preserves them rather than
 * normalizing them to spaces.
 *
 * We always escape carriage returns, so that they're not converted to spaces
 * during attribute-value normalization and because displaying them as literals
 * is messy.
 */
enum pcmk__xml_escape_type {
    /*!
     * For text nodes.
     * * Escape \c '<', \c '>', and \c '&' using entity references.
     * * Do not escape \c '\n' and \c '\t'.
     * * Escape other non-printing characters using character references.
     */
    pcmk__xml_escape_text,

    /*!
     * For attribute values.
     * * Escape \c '<', \c '>', \c '&', and \c '"' using entity references.
     * * Escape \c '\n', \c '\t', and other non-printing characters using
     *   character references.
     */
    pcmk__xml_escape_attr,

    /* @COMPAT Drop escaping of at least '\n' and '\t' for
     * pcmk__xml_escape_attr_pretty when openstack-info, openstack-floating-ip,
     * and openstack-virtual-ip resource agents no longer depend on it.
     *
     * At time of writing, openstack-info may set a multiline value for the
     * openstack_ports node attribute. The other two agents query the value and
     * require it to be on one line with no spaces.
     */
    /*!
     * For attribute values displayed in text output delimited by double quotes.
     * * Escape \c '\n' as \c "\\n"
     * * Escape \c '\r' as \c "\\r"
     * * Escape \c '\t' as \c "\\t"
     * * Escape \c '"' as \c "\\""
     */
    pcmk__xml_escape_attr_pretty,
};

bool pcmk__xml_needs_escape(const char *text, enum pcmk__xml_escape_type type);
char *pcmk__xml_escape(const char *text, enum pcmk__xml_escape_type type);

/*!
 * \internal
 * \brief Get the root directory to scan XML artefacts of given kind for
 *
 * \param[in] ns governs the hierarchy nesting against the inherent root dir
 *
 * \return root directory to scan XML artefacts of given kind for
 */
char *
pcmk__xml_artefact_root(enum pcmk__xml_artefact_ns ns);

/*!
 * \internal
 * \brief Get the fully unwrapped path to particular XML artifact (RNG/XSLT)
 *
 * \param[in] ns       denotes path forming details (parent dir, suffix)
 * \param[in] filespec symbolic file specification to be combined with
 *                     #artefact_ns to form the final path
 * \return unwrapped path to particular XML artifact (RNG/XSLT)
 */
char *pcmk__xml_artefact_path(enum pcmk__xml_artefact_ns ns,
                              const char *filespec);

/*!
 * \internal
 * \brief Retrieve the value of the \c PCMK_XA_ID XML attribute
 *
 * \param[in] xml  XML element to check
 *
 * \return Value of the \c PCMK_XA_ID attribute (may be \c NULL)
 */
static inline const char *
pcmk__xe_id(const xmlNode *xml)
{
    return crm_element_value(xml, PCMK_XA_ID);
}

/*!
 * \internal
 * \brief Check whether an XML element is of a particular type
 *
 * \param[in] xml   XML element to compare
 * \param[in] name  XML element name to compare
 *
 * \return \c true if \p xml is of type \p name, otherwise \c false
 */
static inline bool
pcmk__xe_is(const xmlNode *xml, const char *name)
{
    return (xml != NULL) && (xml->name != NULL) && (name != NULL)
           && (strcmp((const char *) xml->name, name) == 0);
}

/*!
 * \internal
 * \brief Return first non-text child node of an XML node
 *
 * \param[in] parent  XML node to check
 *
 * \return First non-text child node of \p parent (or NULL if none)
 */
static inline xmlNode *
pcmk__xml_first_child(const xmlNode *parent)
{
    xmlNode *child = (parent? parent->children : NULL);

    while (child && (child->type == XML_TEXT_NODE)) {
        child = child->next;
    }
    return child;
}

/*!
 * \internal
 * \brief Return next non-text sibling node of an XML node
 *
 * \param[in] child  XML node to check
 *
 * \return Next non-text sibling of \p child (or NULL if none)
 */
static inline xmlNode *
pcmk__xml_next(const xmlNode *child)
{
    xmlNode *next = (child? child->next : NULL);

    while (next && (next->type == XML_TEXT_NODE)) {
        next = next->next;
    }
    return next;
}

/*!
 * \internal
 * \brief Return next non-text sibling element of an XML element
 *
 * \param[in] child  XML element to check
 *
 * \return Next sibling element of \p child (or NULL if none)
 */
static inline xmlNode *
pcmk__xe_next(const xmlNode *child)
{
    xmlNode *next = child? child->next : NULL;

    while (next && (next->type != XML_ELEMENT_NODE)) {
        next = next->next;
    }
    return next;
}

xmlNode *pcmk__xe_create(xmlNode *parent, const char *name);
void pcmk__xml_free(xmlNode *xml);
xmlNode *pcmk__xml_copy(xmlNode *parent, xmlNode *src);
xmlNode *pcmk__xe_next_same(const xmlNode *node);

void pcmk__xe_set_content(xmlNode *node, const char *format, ...)
    G_GNUC_PRINTF(2, 3);

/*!
 * \internal
 * \enum pcmk__xa_flags
 * \brief Flags for operations affecting XML attributes
 */
enum pcmk__xa_flags {
    //! Flag has no effect
    pcmk__xaf_none          = 0U,

    //! Don't overwrite existing values
    pcmk__xaf_no_overwrite  = (1U << 0),

    /*!
     * Treat values as score updates where possible (see
     * \c pcmk__xe_set_score())
     */
    pcmk__xaf_score_update  = (1U << 1),
};

int pcmk__xe_copy_attrs(xmlNode *target, const xmlNode *src, uint32_t flags);

void pcmk__xml_sanitize_id(char *id);
void pcmk__xe_set_id(xmlNode *xml, const char *format, ...)
    G_GNUC_PRINTF(2, 3);

/*!
 * \internal
 * \brief Like pcmk__xe_set_props, but takes a va_list instead of
 *        arguments directly.
 *
 * \param[in,out] node   XML to add attributes to
 * \param[in]     pairs  NULL-terminated list of name/value pairs to add
 */
void
pcmk__xe_set_propv(xmlNodePtr node, va_list pairs);

/*!
 * \internal
 * \brief Add a NULL-terminated list of name/value pairs to the given
 *        XML node as properties.
 *
 * \param[in,out] node XML node to add properties to
 * \param[in]     ...  NULL-terminated list of name/value pairs
 *
 * \note A NULL name terminates the arguments; a NULL value will be skipped.
 */
void
pcmk__xe_set_props(xmlNodePtr node, ...)
G_GNUC_NULL_TERMINATED;

/*!
 * \internal
 * \brief Get first attribute of an XML element
 *
 * \param[in] xe  XML element to check
 *
 * \return First attribute of \p xe (or NULL if \p xe is NULL or has none)
 */
static inline xmlAttr *
pcmk__xe_first_attr(const xmlNode *xe)
{
    return (xe == NULL)? NULL : xe->properties;
}

/*!
 * \internal
 * \brief Extract the ID attribute from an XML element
 *
 * \param[in] xpath String to search
 * \param[in] node  Node to get the ID for
 *
 * \return ID attribute of \p node in xpath string \p xpath
 */
char *
pcmk__xpath_node_id(const char *xpath, const char *node);

/*!
 * \internal
 * \brief Print an informational message if an xpath query returned multiple
 *        items with the same ID.
 *
 * \param[in,out] out       The output object
 * \param[in]     search    The xpath search result, most typically the result of
 *                          calling cib->cmds->query().
 * \param[in]     name      The name searched for
 */
void
pcmk__warn_multiple_name_matches(pcmk__output_t *out, xmlNode *search,
                                 const char *name);

/* internal XML-related utilities */

enum xml_private_flags {
     pcmk__xf_none        = 0x0000,
     pcmk__xf_dirty       = 0x0001,
     pcmk__xf_deleted     = 0x0002,
     pcmk__xf_created     = 0x0004,
     pcmk__xf_modified    = 0x0008,

     pcmk__xf_tracking    = 0x0010,
     pcmk__xf_processed   = 0x0020,
     pcmk__xf_skip        = 0x0040,
     pcmk__xf_moved       = 0x0080,

     pcmk__xf_acl_enabled = 0x0100,
     pcmk__xf_acl_read    = 0x0200,
     pcmk__xf_acl_write   = 0x0400,
     pcmk__xf_acl_deny    = 0x0800,

     pcmk__xf_acl_create  = 0x1000,
     pcmk__xf_acl_denied  = 0x2000,
     pcmk__xf_lazy        = 0x4000,
};

void pcmk__set_xml_doc_flag(xmlNode *xml, enum xml_private_flags flag);

/*!
 * \internal
 * \brief Iterate over child elements of \p xml
 *
 * This function iterates over the children of \p xml, performing the
 * callback function \p handler on each node.  If the callback returns
 * a value other than pcmk_rc_ok, the iteration stops and the value is
 * returned.  It is therefore possible that not all children will be
 * visited.
 *
 * \param[in,out] xml                 The starting XML node.  Can be NULL.
 * \param[in]     child_element_name  The name that the node must match in order
 *                                    for \p handler to be run.  If NULL, all
 *                                    child elements will match.
 * \param[in]     handler             The callback function.
 * \param[in,out] userdata            User data to pass to the callback function.
 *                                    Can be NULL.
 *
 * \return Standard Pacemaker return code
 */
int
pcmk__xe_foreach_child(xmlNode *xml, const char *child_element_name,
                       int (*handler)(xmlNode *xml, void *userdata),
                       void *userdata);

bool pcmk__xml_tree_foreach(xmlNode *xml, bool (*fn)(xmlNode *, void *),
                            void *user_data);

static inline const char *
pcmk__xml_attr_value(const xmlAttr *attr)
{
    return ((attr == NULL) || (attr->children == NULL))? NULL
           : (const char *) attr->children->content;
}

// @COMPAT Remove when v1 patchsets are removed
xmlNode *pcmk__diff_v1_xml_object(xmlNode *left, xmlNode *right, bool suppress);

// @COMPAT Drop when PCMK__XE_PROMOTABLE_LEGACY is removed
static inline const char *
pcmk__map_element_name(const xmlNode *xml)
{
    if (xml == NULL) {
        return NULL;
    } else if (pcmk__xe_is(xml, PCMK__XE_PROMOTABLE_LEGACY)) {
        return PCMK_XE_CLONE;
    } else {
        return (const char *) xml->name;
    }
}

#endif // PCMK__XML_INTERNAL__H
