/*
 * Copyright 2004-2023 the Pacemaker project contributors
 *
 * The version control history for this file may have further details.
 *
 * This source code is licensed under the GNU General Public License version 2
 * or later (GPLv2+) WITHOUT ANY WARRANTY.
 */

#include <crm_internal.h>

#include <crm/crm.h>

#include <crm/msg_xml.h>
#include <crm/common/xml.h>
#include <crm/cluster.h>

#include <pacemaker-controld.h>

static char *max_generation_from = NULL;
static xmlNodePtr max_generation_xml = NULL;

/*!
 * \internal
 * \brief Nodes from which a CIB sync has failed since the peer joined
 *
 * This table is of the form (<tt>node_name -> join_id</tt>). \p node_name is
 * the name of a client node from which a CIB \p sync_from() call has failed in
 * \p do_dc_join_finalize() since the client joined the cluster as a peer.
 * \p join_id is the ID of the join round in which the \p sync_from() failed,
 * and is intended for use in nack log messages.
 */
static GHashTable *failed_sync_nodes = NULL;

void finalize_join_for(gpointer key, gpointer value, gpointer user_data);
void finalize_sync_callback(xmlNode * msg, int call_id, int rc, xmlNode * output, void *user_data);
gboolean check_join_state(enum crmd_fsa_state cur_state, const char *source);

/* Numeric counter used to identify join rounds (an unsigned int would be
 * appropriate, except we get and set it in XML as int)
 */
static int current_join_id = 0;

/*!
 * \internal
 * \brief Destroy the hash table containing failed sync nodes
 */
void
controld_destroy_failed_sync_table(void)
{
    if (failed_sync_nodes != NULL) {
        g_hash_table_destroy(failed_sync_nodes);
        failed_sync_nodes = NULL;
    }
}

/*!
 * \internal
 * \brief Remove a node from the failed sync nodes table if present
 *
 * \param[in] node_name  Node name to remove
 */
void
controld_remove_failed_sync_node(const char *node_name)
{
    if (failed_sync_nodes != NULL) {
        g_hash_table_remove(failed_sync_nodes, (gchar *) node_name);
    }
}

/*!
 * \internal
 * \brief Add to a hash table a node whose CIB failed to sync
 *
 * \param[in] node_name  Name of node whose CIB failed to sync
 * \param[in] join_id    Join round when the failure occurred
 */
static void
record_failed_sync_node(const char *node_name, gint join_id)
{
    if (failed_sync_nodes == NULL) {
        failed_sync_nodes = pcmk__strikey_table(g_free, NULL);
    }

    /* If the node is already in the table then we failed to nack it during the
     * filter offer step
     */
    CRM_LOG_ASSERT(g_hash_table_insert(failed_sync_nodes, g_strdup(node_name),
                                       GINT_TO_POINTER(join_id)));
}

/*!
 * \internal
 * \brief Look up a node name in the failed sync table
 *
 * \param[in]  node_name  Name of node to look up
 * \param[out] join_id    Where to store the join ID of when the sync failed
 *
 * \return Standard Pacemaker return code. Specifically, \p pcmk_rc_ok if the
 *         node name was found, or \p pcmk_rc_node_unknown otherwise.
 * \note \p *join_id is set to -1 if the node is not found.
 */
static int
lookup_failed_sync_node(const char *node_name, gint *join_id)
{
    *join_id = -1;

    if (failed_sync_nodes != NULL) {
        gpointer result = g_hash_table_lookup(failed_sync_nodes,
                                              (gchar *) node_name);
        if (result != NULL) {
            *join_id = GPOINTER_TO_INT(result);
            return pcmk_rc_ok;
        }
    }
    return pcmk_rc_node_unknown;
}

void
crm_update_peer_join(const char *source, crm_node_t * node, enum crm_join_phase phase)
{
    enum crm_join_phase last = 0;

    CRM_CHECK(node != NULL, return);

    /* Remote nodes do not participate in joins */
    if (pcmk_is_set(node->flags, crm_remote_node)) {
        return;
    }

    last = node->join;

    if(phase == last) {
        crm_trace("Node %s join-%d phase is still %s "
                  CRM_XS " nodeid=%u source=%s",
                  node->uname, current_join_id, crm_join_phase_str(last),
                  node->id, source);

    } else if ((phase <= crm_join_none) || (phase == (last + 1))) {
        node->join = phase;
        crm_trace("Node %s join-%d phase is now %s (was %s) "
                  CRM_XS " nodeid=%u source=%s",
                 node->uname, current_join_id, crm_join_phase_str(phase),
                 crm_join_phase_str(last), node->id, source);

    } else {
        crm_warn("Rejecting join-%d phase update for node %s because "
                 "can't go from %s to %s " CRM_XS " nodeid=%u source=%s",
                 current_join_id, node->uname, crm_join_phase_str(last),
                 crm_join_phase_str(phase), node->id, source);
    }
}

static void
start_join_round(void)
{
    GHashTableIter iter;
    crm_node_t *peer = NULL;

    crm_debug("Starting new join round join-%d", current_join_id);

    g_hash_table_iter_init(&iter, crm_peer_cache);
    while (g_hash_table_iter_next(&iter, NULL, (gpointer *) &peer)) {
        crm_update_peer_join(__func__, peer, crm_join_none);
    }
    if (max_generation_from != NULL) {
        free(max_generation_from);
        max_generation_from = NULL;
    }
    if (max_generation_xml != NULL) {
        free_xml(max_generation_xml);
        max_generation_xml = NULL;
    }
    controld_clear_fsa_input_flags(R_HAVE_CIB|R_CIB_ASKED);
}

/*!
 * \internal
 * \brief Create a join message from the DC
 *
 * \param[in] join_op  Join operation name
 * \param[in] host_to  Recipient of message
 */
static xmlNode *
create_dc_message(const char *join_op, const char *host_to)
{
    xmlNode *msg = create_request(join_op, NULL, host_to, CRM_SYSTEM_CRMD,
                                  CRM_SYSTEM_DC, NULL);

    /* Identify which election this is a part of */
    crm_xml_add_int(msg, F_CRM_JOIN_ID, current_join_id);

    /* Add a field specifying whether the DC is shutting down. This keeps the
     * joining node from fencing the old DC if it becomes the new DC.
     */
    pcmk__xe_set_bool_attr(msg, F_CRM_DC_LEAVING,
                           pcmk_is_set(controld_globals.fsa_input_register,
                                       R_SHUTDOWN));
    return msg;
}

static void
join_make_offer(gpointer key, gpointer value, gpointer user_data)
{
    xmlNode *offer = NULL;
    crm_node_t *member = (crm_node_t *)value;

    CRM_ASSERT(member != NULL);
    if (crm_is_peer_active(member) == FALSE) {
        crm_info("Not making join-%d offer to inactive node %s",
                 current_join_id,
                 (member->uname? member->uname : "with unknown name"));
        if(member->expected == NULL && pcmk__str_eq(member->state, CRM_NODE_LOST, pcmk__str_casei)) {
            /* You would think this unsafe, but in fact this plus an
             * active resource is what causes it to be fenced.
             *
             * Yes, this does mean that any node that dies at the same
             * time as the old DC and is not running resource (still)
             * won't be fenced.
             *
             * I'm not happy about this either.
             */
            pcmk__update_peer_expected(__func__, member, CRMD_JOINSTATE_DOWN);
        }
        return;
    }

    if (member->uname == NULL) {
        crm_info("Not making join-%d offer to node uuid %s with unknown name",
                 current_join_id, member->uuid);
        return;
    }

    if (controld_globals.membership_id != crm_peer_seq) {
        controld_globals.membership_id = crm_peer_seq;
        crm_info("Making join-%d offers based on membership event %llu",
                 current_join_id, crm_peer_seq);
    }

    if(user_data && member->join > crm_join_none) {
        crm_info("Not making join-%d offer to already known node %s (%s)",
                 current_join_id, member->uname,
                 crm_join_phase_str(member->join));
        return;
    }

    crm_update_peer_join(__func__, (crm_node_t*)member, crm_join_none);

    offer = create_dc_message(CRM_OP_JOIN_OFFER, member->uname);

    // Advertise our feature set so the joining node can bail if not compatible
    crm_xml_add(offer, XML_ATTR_CRM_VERSION, CRM_FEATURE_SET);

    crm_info("Sending join-%d offer to %s", current_join_id, member->uname);
    send_cluster_message(member, crm_msg_crmd, offer, TRUE);
    free_xml(offer);

    crm_update_peer_join(__func__, member, crm_join_welcomed);
}

/*	 A_DC_JOIN_OFFER_ALL	*/
void
do_dc_join_offer_all(long long action,
                     enum crmd_fsa_cause cause,
                     enum crmd_fsa_state cur_state,
                     enum crmd_fsa_input current_input, fsa_data_t * msg_data)
{
    int count;

    /* Reset everyone's status back to down or in_ccm in the CIB.
     * Any nodes that are active in the CIB but not in the cluster membership
     * will be seen as offline by the scheduler anyway.
     */
    current_join_id++;
    start_join_round();

    update_dc(NULL);
    if (cause == C_HA_MESSAGE && current_input == I_NODE_JOIN) {
        crm_info("A new node joined the cluster");
    }
    g_hash_table_foreach(crm_peer_cache, join_make_offer, NULL);

    count = crmd_join_phase_count(crm_join_welcomed);
    crm_info("Waiting on join-%d requests from %d outstanding node%s",
             current_join_id, count, pcmk__plural_s(count));

    // Don't waste time by invoking the scheduler yet
}

/*	 A_DC_JOIN_OFFER_ONE	*/
void
do_dc_join_offer_one(long long action,
                     enum crmd_fsa_cause cause,
                     enum crmd_fsa_state cur_state,
                     enum crmd_fsa_input current_input, fsa_data_t * msg_data)
{
    crm_node_t *member;
    ha_msg_input_t *welcome = NULL;
    int count;
    const char *join_to = NULL;

    if (msg_data->data == NULL) {
        crm_info("Making join-%d offers to any unconfirmed nodes "
                 "because an unknown node joined", current_join_id);
        g_hash_table_foreach(crm_peer_cache, join_make_offer, &member);
        check_join_state(cur_state, __func__);
        return;
    }

    welcome = fsa_typed_data(fsa_dt_ha_msg);
    if (welcome == NULL) {
        // fsa_typed_data() already logged an error
        return;
    }

    join_to = crm_element_value(welcome->msg, F_CRM_HOST_FROM);
    if (join_to == NULL) {
        crm_err("Can't make join-%d offer to unknown node", current_join_id);
        return;
    }
    member = crm_get_peer(0, join_to);

    /* It is possible that a node will have been sick or starting up when the
     * original offer was made. However, it will either re-announce itself in
     * due course, or we can re-store the original offer on the client.
     */

    crm_update_peer_join(__func__, member, crm_join_none);
    join_make_offer(NULL, member, NULL);

    /* If the offer isn't to the local node, make an offer to the local node as
     * well, to ensure the correct value for max_generation_from.
     */
    if (strcasecmp(join_to, controld_globals.our_nodename) != 0) {
        member = crm_get_peer(0, controld_globals.our_nodename);
        join_make_offer(NULL, member, NULL);
    }

    /* This was a genuine join request; cancel any existing transition and
     * invoke the scheduler.
     */
    abort_transition(INFINITY, pcmk__graph_restart, "Node join", NULL);

    count = crmd_join_phase_count(crm_join_welcomed);
    crm_info("Waiting on join-%d requests from %d outstanding node%s",
             current_join_id, count, pcmk__plural_s(count));

    // Don't waste time by invoking the scheduler yet
}

static int
compare_int_fields(xmlNode * left, xmlNode * right, const char *field)
{
    const char *elem_l = crm_element_value(left, field);
    const char *elem_r = crm_element_value(right, field);

    long long int_elem_l;
    long long int_elem_r;

    pcmk__scan_ll(elem_l, &int_elem_l, -1LL);
    pcmk__scan_ll(elem_r, &int_elem_r, -1LL);

    if (int_elem_l < int_elem_r) {
        return -1;

    } else if (int_elem_l > int_elem_r) {
        return 1;
    }

    return 0;
}

/*	 A_DC_JOIN_PROCESS_REQ	*/
void
do_dc_join_filter_offer(long long action,
                        enum crmd_fsa_cause cause,
                        enum crmd_fsa_state cur_state,
                        enum crmd_fsa_input current_input, fsa_data_t * msg_data)
{
    xmlNode *generation = NULL;

    int cmp = 0;
    int join_id = -1;
    int count = 0;
    gint value = 0;
    gboolean ack_nack_bool = TRUE;
    ha_msg_input_t *join_ack = fsa_typed_data(fsa_dt_ha_msg);

    const char *join_from = crm_element_value(join_ack->msg, F_CRM_HOST_FROM);
    const char *ref = crm_element_value(join_ack->msg, F_CRM_REFERENCE);
    const char *join_version = crm_element_value(join_ack->msg,
                                                 XML_ATTR_CRM_VERSION);
    crm_node_t *join_node = NULL;

    if (join_from == NULL) {
        crm_err("Ignoring invalid join request without node name");
        return;
    }
    join_node = crm_get_peer(0, join_from);

    crm_element_value_int(join_ack->msg, F_CRM_JOIN_ID, &join_id);
    if (join_id != current_join_id) {
        crm_debug("Ignoring join-%d request from %s because we are on join-%d",
                  join_id, join_from, current_join_id);
        check_join_state(cur_state, __func__);
        return;
    }

    generation = join_ack->xml;
    if (max_generation_xml != NULL && generation != NULL) {
        int lpc = 0;

        const char *attributes[] = {
            XML_ATTR_GENERATION_ADMIN,
            XML_ATTR_GENERATION,
            XML_ATTR_NUMUPDATES,
        };

        for (lpc = 0; cmp == 0 && lpc < PCMK__NELEM(attributes); lpc++) {
            cmp = compare_int_fields(max_generation_xml, generation, attributes[lpc]);
        }
    }

    if (ref == NULL) {
        ref = "none"; // for logging only
    }

    if (lookup_failed_sync_node(join_from, &value) == pcmk_rc_ok) {
        crm_err("Rejecting join-%d request from node %s because we failed to "
                "sync its CIB in join-%d " CRM_XS " ref=%s",
                join_id, join_from, value, ref);
        ack_nack_bool = FALSE;

    } else if (!crm_is_peer_active(join_node)) {
        if (match_down_event(join_from) != NULL) {
            /* The join request was received after the node was fenced or
             * otherwise shutdown in a way that we're aware of. No need to log
             * an error in this rare occurrence; we know the client was recently
             * shut down, and receiving a lingering in-flight request is not
             * cause for alarm.
             */
            crm_debug("Rejecting join-%d request from inactive node %s "
                      CRM_XS " ref=%s", join_id, join_from, ref);
        } else {
            crm_err("Rejecting join-%d request from inactive node %s "
                    CRM_XS " ref=%s", join_id, join_from, ref);
        }
        ack_nack_bool = FALSE;

    } else if (generation == NULL) {
        crm_err("Rejecting invalid join-%d request from node %s "
                "missing CIB generation " CRM_XS " ref=%s",
                join_id, join_from, ref);
        ack_nack_bool = FALSE;

    } else if ((join_version == NULL)
               || !feature_set_compatible(CRM_FEATURE_SET, join_version)) {
        crm_err("Rejecting join-%d request from node %s because feature set %s"
                " is incompatible with ours (%s) " CRM_XS " ref=%s",
                join_id, join_from, (join_version? join_version : "pre-3.1.0"),
                CRM_FEATURE_SET, ref);
        ack_nack_bool = FALSE;

    } else if (max_generation_xml == NULL) {
        const char *validation = crm_element_value(generation,
                                                   XML_ATTR_VALIDATION);

        if (get_schema_version(validation) < 0) {
            crm_err("Rejecting join-%d request from %s (with first CIB "
                    "generation) due to unknown schema version %s "
                    CRM_XS " ref=%s",
                    join_id, join_from, validation, ref);
            ack_nack_bool = FALSE;

        } else {
            crm_debug("Accepting join-%d request from %s (with first CIB "
                      "generation) " CRM_XS " ref=%s",
                      join_id, join_from, ref);
            max_generation_xml = copy_xml(generation);
            pcmk__str_update(&max_generation_from, join_from);
        }

    } else if ((cmp < 0)
               || ((cmp == 0)
                   && pcmk__str_eq(join_from, controld_globals.our_nodename,
                                   pcmk__str_casei))) {
        const char *validation = crm_element_value(generation,
                                                   XML_ATTR_VALIDATION);

        if (get_schema_version(validation) < 0) {
            crm_err("Rejecting join-%d request from %s (with better CIB "
                    "generation than current best from %s) due to unknown "
                    "schema version %s " CRM_XS " ref=%s",
                    join_id, join_from, max_generation_from, validation, ref);
            ack_nack_bool = FALSE;

        } else {
            crm_debug("Accepting join-%d request from %s (with better CIB "
                      "generation than current best from %s) " CRM_XS " ref=%s",
                      join_id, join_from, max_generation_from, ref);
            crm_log_xml_debug(max_generation_xml, "Old max generation");
            crm_log_xml_debug(generation, "New max generation");

            free_xml(max_generation_xml);
            max_generation_xml = copy_xml(join_ack->xml);
            pcmk__str_update(&max_generation_from, join_from);
        }

    } else {
        crm_debug("Accepting join-%d request from %s " CRM_XS " ref=%s",
                  join_id, join_from, ref);
    }

    if (!ack_nack_bool) {
        if (compare_version(join_version, "3.17.0") < 0) {
            /* Clients with CRM_FEATURE_SET < 3.17.0 may respawn infinitely
             * after a nack message, don't send one
             */
            crm_update_peer_join(__func__, join_node, crm_join_nack_quiet);
        } else {
            crm_update_peer_join(__func__, join_node, crm_join_nack);
        }
        pcmk__update_peer_expected(__func__, join_node, CRMD_JOINSTATE_NACK);

    } else {
        crm_update_peer_join(__func__, join_node, crm_join_integrated);
        pcmk__update_peer_expected(__func__, join_node, CRMD_JOINSTATE_MEMBER);
    }

    count = crmd_join_phase_count(crm_join_integrated);
    crm_debug("%d node%s currently integrated in join-%d",
              count, pcmk__plural_s(count), join_id);

    if (check_join_state(cur_state, __func__) == FALSE) {
        // Don't waste time by invoking the scheduler yet
        count = crmd_join_phase_count(crm_join_welcomed);
        crm_debug("Waiting on join-%d requests from %d outstanding node%s",
                  join_id, count, pcmk__plural_s(count));
    }
}

/*	A_DC_JOIN_FINALIZE	*/
void
do_dc_join_finalize(long long action,
                    enum crmd_fsa_cause cause,
                    enum crmd_fsa_state cur_state,
                    enum crmd_fsa_input current_input, fsa_data_t * msg_data)
{
    char *sync_from = NULL;
    int rc = pcmk_ok;
    int count_welcomed = crmd_join_phase_count(crm_join_welcomed);
    int count_finalizable = crmd_join_phase_count(crm_join_integrated)
                            + crmd_join_phase_count(crm_join_nack)
                            + crmd_join_phase_count(crm_join_nack_quiet);

    /* This we can do straight away and avoid clients timing us out
     *  while we compute the latest CIB
     */
    if (count_welcomed != 0) {
        crm_debug("Waiting on join-%d requests from %d outstanding node%s "
                  "before finalizing join", current_join_id, count_welcomed,
                  pcmk__plural_s(count_welcomed));
        crmd_join_phase_log(LOG_DEBUG);
        /* crmd_fsa_stall(FALSE); Needed? */
        return;

    } else if (count_finalizable == 0) {
        crm_debug("Finalization not needed for join-%d at the current time",
                  current_join_id);
        crmd_join_phase_log(LOG_DEBUG);
        check_join_state(controld_globals.fsa_state, __func__);
        return;
    }

    controld_clear_fsa_input_flags(R_HAVE_CIB);
    if (pcmk__str_eq(max_generation_from, controld_globals.our_nodename,
                     pcmk__str_null_matches|pcmk__str_casei)) {
        controld_set_fsa_input_flags(R_HAVE_CIB);
    }

    if (pcmk_is_set(controld_globals.fsa_input_register, R_IN_TRANSITION)) {
        crm_warn("Delaying join-%d finalization while transition in progress",
                 current_join_id);
        crmd_join_phase_log(LOG_DEBUG);
        crmd_fsa_stall(FALSE);
        return;
    }

    if ((max_generation_from != NULL)
        && !pcmk_is_set(controld_globals.fsa_input_register, R_HAVE_CIB)) {
        /* ask for the agreed best CIB */
        pcmk__str_update(&sync_from, max_generation_from);
        controld_set_fsa_input_flags(R_CIB_ASKED);
        crm_notice("Finalizing join-%d for %d node%s (sync'ing CIB from %s)",
                   current_join_id, count_finalizable,
                   pcmk__plural_s(count_finalizable), sync_from);
        crm_log_xml_notice(max_generation_xml, "Requested CIB version");

    } else {
        /* Send _our_ CIB out to everyone */
        pcmk__str_update(&sync_from, controld_globals.our_nodename);
        crm_debug("Finalizing join-%d for %d node%s (sync'ing from local CIB)",
                  current_join_id, count_finalizable,
                  pcmk__plural_s(count_finalizable));
        crm_log_xml_debug(max_generation_xml, "Requested CIB version");
    }
    crmd_join_phase_log(LOG_DEBUG);

    rc = controld_globals.cib_conn->cmds->sync_from(controld_globals.cib_conn,
                                                    sync_from, NULL,
                                                    cib_quorum_override);
    fsa_register_cib_callback(rc, sync_from, finalize_sync_callback);
}

void
free_max_generation(void)
{
    free(max_generation_from);
    max_generation_from = NULL;

    free_xml(max_generation_xml);
    max_generation_xml = NULL;
}

void
finalize_sync_callback(xmlNode * msg, int call_id, int rc, xmlNode * output, void *user_data)
{
    CRM_LOG_ASSERT(-EPERM != rc);
    controld_clear_fsa_input_flags(R_CIB_ASKED);
    if (rc != pcmk_ok) {
        const char *sync_from = (const char *) user_data;

        do_crm_log(((rc == -pcmk_err_old_data)? LOG_WARNING : LOG_ERR),
                   "Could not sync CIB from %s in join-%d: %s",
                   sync_from, current_join_id, pcmk_strerror(rc));

        if (rc != -pcmk_err_old_data) {
            record_failed_sync_node(sync_from, current_join_id);
        }

        /* restart the whole join process */
        register_fsa_error_adv(C_FSA_INTERNAL, I_ELECTION_DC, NULL, NULL,
                               __func__);

    } else if (!AM_I_DC) {
        crm_debug("Sync'ed CIB for join-%d but no longer DC", current_join_id);

    } else if (controld_globals.fsa_state != S_FINALIZE_JOIN) {
        crm_debug("Sync'ed CIB for join-%d but no longer in S_FINALIZE_JOIN "
                  "(%s)", current_join_id,
                  fsa_state2string(controld_globals.fsa_state));

    } else {
        controld_set_fsa_input_flags(R_HAVE_CIB);
        controld_clear_fsa_input_flags(R_CIB_ASKED);

        /* make sure dc_uuid is re-set to us */
        if (!check_join_state(controld_globals.fsa_state, __func__)) {
            int count_finalizable = 0;

            count_finalizable = crmd_join_phase_count(crm_join_integrated)
                                + crmd_join_phase_count(crm_join_nack)
                                + crmd_join_phase_count(crm_join_nack_quiet);

            crm_debug("Notifying %d node%s of join-%d results",
                      count_finalizable, pcmk__plural_s(count_finalizable),
                      current_join_id);
            g_hash_table_foreach(crm_peer_cache, finalize_join_for, NULL);
        }
    }
}

static void
join_update_complete_callback(xmlNode * msg, int call_id, int rc, xmlNode * output, void *user_data)
{
    fsa_data_t *msg_data = NULL;

    if (rc == pcmk_ok) {
        crm_debug("join-%d node history update (via CIB call %d) complete",
                  current_join_id, call_id);
        check_join_state(controld_globals.fsa_state, __func__);

    } else {
        crm_err("join-%d node history update (via CIB call %d) failed: %s "
                "(next transition may determine resource status incorrectly)",
                current_join_id, call_id, pcmk_strerror(rc));
        crm_log_xml_debug(msg, "failed");
        register_fsa_error(C_FSA_INTERNAL, I_ERROR, NULL);
    }
}

/*	A_DC_JOIN_PROCESS_ACK	*/
void
do_dc_join_ack(long long action,
               enum crmd_fsa_cause cause,
               enum crmd_fsa_state cur_state,
               enum crmd_fsa_input current_input, fsa_data_t * msg_data)
{
    int join_id = -1;
    ha_msg_input_t *join_ack = fsa_typed_data(fsa_dt_ha_msg);
    enum controld_section_e section = controld_section_lrm;
    const int cib_opts = cib_scope_local|cib_quorum_override|cib_can_create;

    const char *op = crm_element_value(join_ack->msg, F_CRM_TASK);
    const char *join_from = crm_element_value(join_ack->msg, F_CRM_HOST_FROM);
    crm_node_t *peer = NULL;

    // Sanity checks
    if (join_from == NULL) {
        crm_warn("Ignoring message received without node identification");
        return;
    }
    if (op == NULL) {
        crm_warn("Ignoring message received from %s without task", join_from);
        return;
    }

    if (strcmp(op, CRM_OP_JOIN_CONFIRM)) {
        crm_debug("Ignoring '%s' message from %s while waiting for '%s'",
                  op, join_from, CRM_OP_JOIN_CONFIRM);
        return;
    }

    if (crm_element_value_int(join_ack->msg, F_CRM_JOIN_ID, &join_id) != 0) {
        crm_warn("Ignoring join confirmation from %s without valid join ID",
                 join_from);
        return;
    }

    peer = crm_get_peer(0, join_from);
    if (peer->join != crm_join_finalized) {
        crm_info("Ignoring out-of-sequence join-%d confirmation from %s "
                 "(currently %s not %s)",
                 join_id, join_from, crm_join_phase_str(peer->join),
                 crm_join_phase_str(crm_join_finalized));
        return;
    }

    if (join_id != current_join_id) {
        crm_err("Rejecting join-%d confirmation from %s "
                "because currently on join-%d",
                join_id, join_from, current_join_id);
        crm_update_peer_join(__func__, peer, crm_join_nack);
        return;
    }

    /* Update CIB with node's current executor state. A new transition will be
     * triggered later, when the CIB notifies us of the change.
     */
    if (pcmk_is_set(controld_globals.flags, controld_shutdown_lock_enabled)) {
        section = controld_section_lrm_unlocked;
    }
    controld_delete_node_state(join_from, section, cib_scope_local);

    if (pcmk__str_eq(join_from, controld_globals.our_nodename,
                     pcmk__str_casei)) {
        xmlNode *execd_state = controld_query_executor_state();

        if (execd_state == NULL) {
            crm_err("Could not ack join-%d for ourselves: Local operation "
                    "history failed",
                    join_id);
            register_fsa_error(C_FSA_INTERNAL, I_FAIL, NULL);
            return;
        }
        crm_debug("Updating local node history for join-%d from query result",
                  join_id);
        controld_update_cib(XML_CIB_TAG_STATUS, execd_state, cib_opts,
                            join_update_complete_callback);
        free_xml(execd_state);

    } else {
        crm_debug("Updating node history for %s from join-%d confirmation",
                  join_from, join_id);
        controld_update_cib(XML_CIB_TAG_STATUS, join_ack->xml, cib_opts,
                            join_update_complete_callback);
    }
    crm_update_peer_join(__func__, peer, crm_join_confirmed);
}

void
finalize_join_for(gpointer key, gpointer value, gpointer user_data)
{
    xmlNode *acknak = NULL;
    xmlNode *tmp1 = NULL;
    crm_node_t *join_node = value;
    const char *join_to = join_node->uname;
    bool integrated = false;

    switch (join_node->join) {
        case crm_join_integrated:
            integrated = true;
            break;
        case crm_join_nack:
        case crm_join_nack_quiet:
            break;
        default:
            crm_trace("Not updating non-integrated and non-nacked node %s (%s) "
                      "for join-%d", join_to,
                      crm_join_phase_str(join_node->join), current_join_id);
            return;
    }

    /* Update the <node> element with the node's name and UUID, in case they
     * weren't known before
     */
    crm_trace("Updating node name and UUID in CIB for %s", join_to);
    tmp1 = create_xml_node(NULL, XML_CIB_TAG_NODE);
    set_uuid(tmp1, XML_ATTR_ID, join_node);
    crm_xml_add(tmp1, XML_ATTR_UNAME, join_to);
    fsa_cib_anon_update(XML_CIB_TAG_NODES, tmp1);
    free_xml(tmp1);

    if (join_node->join == crm_join_nack_quiet) {
        crm_trace("Not sending nack message to node %s with feature set older "
                  "than 3.17.0", join_to);
        return;
    }

    join_node = crm_get_peer(0, join_to);
    if (!crm_is_peer_active(join_node)) {
        /*
         * NACK'ing nodes that the membership layer doesn't know about yet
         * simply creates more churn
         *
         * Better to leave them waiting and let the join restart when
         * the new membership event comes in
         *
         * All other NACKs (due to versions etc) should still be processed
         */
        pcmk__update_peer_expected(__func__, join_node, CRMD_JOINSTATE_PENDING);
        return;
    }

    // Acknowledge or nack node's join request
    crm_debug("%sing join-%d request from %s",
              integrated? "Acknowledg" : "Nack", current_join_id, join_to);
    acknak = create_dc_message(CRM_OP_JOIN_ACKNAK, join_to);
    pcmk__xe_set_bool_attr(acknak, CRM_OP_JOIN_ACKNAK, integrated);

    if (integrated) {
        // No change needed for a nacked node
        crm_update_peer_join(__func__, join_node, crm_join_finalized);
        pcmk__update_peer_expected(__func__, join_node, CRMD_JOINSTATE_MEMBER);
    }
    send_cluster_message(join_node, crm_msg_crmd, acknak, TRUE);
    free_xml(acknak);
    return;
}

gboolean
check_join_state(enum crmd_fsa_state cur_state, const char *source)
{
    static unsigned long long highest_seq = 0;

    if (controld_globals.membership_id != crm_peer_seq) {
        crm_debug("join-%d: Membership changed from %llu to %llu "
                  CRM_XS " highest=%llu state=%s for=%s",
                  current_join_id, controld_globals.membership_id, crm_peer_seq,
                  highest_seq, fsa_state2string(cur_state), source);
        if(highest_seq < crm_peer_seq) {
            /* Don't spam the FSA with duplicates */
            highest_seq = crm_peer_seq;
            register_fsa_input_before(C_FSA_INTERNAL, I_NODE_JOIN, NULL);
        }

    } else if (cur_state == S_INTEGRATION) {
        if (crmd_join_phase_count(crm_join_welcomed) == 0) {
            int count = crmd_join_phase_count(crm_join_integrated);

            crm_debug("join-%d: Integration of %d peer%s complete "
                      CRM_XS " state=%s for=%s",
                      current_join_id, count, pcmk__plural_s(count),
                      fsa_state2string(cur_state), source);
            register_fsa_input_before(C_FSA_INTERNAL, I_INTEGRATED, NULL);
            return TRUE;
        }

    } else if (cur_state == S_FINALIZE_JOIN) {
        if (!pcmk_is_set(controld_globals.fsa_input_register, R_HAVE_CIB)) {
            crm_debug("join-%d: Delaying finalization until we have CIB "
                      CRM_XS " state=%s for=%s",
                      current_join_id, fsa_state2string(cur_state), source);
            return TRUE;

        } else if (crmd_join_phase_count(crm_join_welcomed) != 0) {
            int count = crmd_join_phase_count(crm_join_welcomed);

            crm_debug("join-%d: Still waiting on %d welcomed node%s "
                      CRM_XS " state=%s for=%s",
                      current_join_id, count, pcmk__plural_s(count),
                      fsa_state2string(cur_state), source);
            crmd_join_phase_log(LOG_DEBUG);

        } else if (crmd_join_phase_count(crm_join_integrated) != 0) {
            int count = crmd_join_phase_count(crm_join_integrated);

            crm_debug("join-%d: Still waiting on %d integrated node%s "
                      CRM_XS " state=%s for=%s",
                      current_join_id, count, pcmk__plural_s(count),
                      fsa_state2string(cur_state), source);
            crmd_join_phase_log(LOG_DEBUG);

        } else if (crmd_join_phase_count(crm_join_finalized) != 0) {
            int count = crmd_join_phase_count(crm_join_finalized);

            crm_debug("join-%d: Still waiting on %d finalized node%s "
                      CRM_XS " state=%s for=%s",
                      current_join_id, count, pcmk__plural_s(count),
                      fsa_state2string(cur_state), source);
            crmd_join_phase_log(LOG_DEBUG);

        } else {
            crm_debug("join-%d: Complete " CRM_XS " state=%s for=%s",
                      current_join_id, fsa_state2string(cur_state), source);
            register_fsa_input_later(C_FSA_INTERNAL, I_FINALIZED, NULL);
            return TRUE;
        }
    }

    return FALSE;
}

void
do_dc_join_final(long long action,
                 enum crmd_fsa_cause cause,
                 enum crmd_fsa_state cur_state,
                 enum crmd_fsa_input current_input, fsa_data_t * msg_data)
{
    crm_debug("Ensuring DC, quorum and node attributes are up-to-date");
    crm_update_quorum(crm_have_quorum, TRUE);
}

int crmd_join_phase_count(enum crm_join_phase phase)
{
    int count = 0;
    crm_node_t *peer;
    GHashTableIter iter;

    g_hash_table_iter_init(&iter, crm_peer_cache);
    while (g_hash_table_iter_next(&iter, NULL, (gpointer *) &peer)) {
        if(peer->join == phase) {
            count++;
        }
    }
    return count;
}

void crmd_join_phase_log(int level)
{
    crm_node_t *peer;
    GHashTableIter iter;

    g_hash_table_iter_init(&iter, crm_peer_cache);
    while (g_hash_table_iter_next(&iter, NULL, (gpointer *) &peer)) {
        do_crm_log(level, "join-%d: %s=%s", current_join_id, peer->uname,
                   crm_join_phase_str(peer->join));
    }
}
