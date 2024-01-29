/*
 * Copyright 2024 the Pacemaker project contributors
 *
 * The version control history for this file may have further details.
 *
 * This source code is licensed under the GNU Lesser General Public License
 * version 2.1 or later (LGPLv2.1+) WITHOUT ANY WARRANTY.
 */

#include <crm_internal.h>

#include <pacemaker.h>
#include <pacemaker-internal.h>

/*!
 * \internal
 * \brief List all available cluster options
 *
 * These are options that affect the entire cluster.
 *
 * \param[in,out] out  Output object
 *
 * \return Standard Pacemaker return code
 */
int
pcmk__list_cluster_options(pcmk__output_t *out)
{
    const char *name = "cluster-options";
    const char *desc_short = "Pacemaker cluster options";
    const char *desc_long = NULL;

    desc_long = "Also known as properties, these are options that affect "
                "behavior across the entire cluster. They are configured "
                "within " PCMK_XE_CLUSTER_PROPERTY_SET " elements inside the "
                PCMK_XE_CRM_CONFIG " subsection of the CIB "
                PCMK_XE_CONFIGURATION " section.";

    return pcmk__output_cluster_options(out, name, desc_short, desc_long,
                                        pcmk__opt_context_none);
}
