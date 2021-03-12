/*
 * Copyright (C) 2010 Senko Rasic <senko.rasic@dobarkod.hr>
 * Copyright (c) 2010 Ante Karamatic <ivoks@init.hr>
 *
 * This source code is licensed under the GNU Lesser General Public License
 * version 2.1 or later (LGPLv2.1+) WITHOUT ANY WARRANTY.
 */
#ifndef UPSTART__H
#  define UPSTART__H

#  include <glib.h>
#  include "crm/services.h"

G_GNUC_INTERNAL GList *upstart_job_listall(void);
G_GNUC_INTERNAL gboolean upstart_job_exec(svc_action_t * op);
G_GNUC_INTERNAL gboolean upstart_job_exists(const gchar * name);
G_GNUC_INTERNAL void upstart_cleanup(void);

#endif  /* UPSTART__H */
