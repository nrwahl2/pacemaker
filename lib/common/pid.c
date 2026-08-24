/*
 * Copyright 2004-2026 the Pacemaker project contributors
 *
 * The version control history for this file may have further details.
 *
 * This source code is licensed under the GNU Lesser General Public License
 * version 2.1 or later (LGPLv2.1+) WITHOUT ANY WARRANTY.
 */

#include <crm_internal.h>

#include <stdbool.h>
#include <stdio.h>

#include <crm/crm.h>

int
pcmk__pid_active(pid_t pid, const char *expected_path)
{
    static pid_t last_asked_pid = 0;  /* log spam prevention */

    int kill_rc = 0;
    int rc = pcmk_rc_ok;

    if (pid <= 0) {
        return EINVAL;
    }

    kill_rc = kill(pid, 0);
    if ((kill_rc < 0) && (errno == ESRCH)) {
        return ESRCH;  /* no such PID detected */

    } else if ((expected_path == NULL) || !pcmk__procfs_has_pids()) {
        // The kill result is all we have, we can't check the name

        if (kill_rc == 0) {
            return pcmk_rc_ok;
        }

        rc = errno;

        if (last_asked_pid != pid) {
            pcmk__info("Cannot examine PID %lld: %s", (long long) pid,
                       pcmk_rc_str(rc));
            last_asked_pid = pid;
        }

        return rc; /* errno != ESRCH */

    } else {
        /* make sure PID hasn't been reused by another process
           XXX: might still be just a zombie, which could confuse decisions */
        bool paths_equal = false;
        char *found_path = NULL;

        rc = pcmk__procfs_pid2path(pid, &found_path);
        if (rc != pcmk_rc_ok) {
            // On non-EACCES, check again to filter out races
            if ((rc != EACCES) && (kill(pid, 0) < 0) && (errno == ESRCH)) {
                return ESRCH;
            }

            if (last_asked_pid != pid) {
                if (rc == EACCES) {
                    pcmk__info("Could not get executable for PID %lld: %s "
                               QB_XS " rc=%d", (long long) pid, pcmk_rc_str(rc),
                               rc);

                } else {
                    pcmk__err("Could not get executable for PID %lld: %s "
                              QB_XS " rc=%d",(long long) pid, pcmk_rc_str(rc),
                              rc);
                }

                last_asked_pid = pid;
            }

            if (rc == EACCES) {
                // Trust kill if it was OK (we can't double-check via path)
                return (kill_rc == 0)? pcmk_rc_ok : EACCES;
            }

            // Most likely errno == ENOENT
            return ESRCH;
        }

        paths_equal = pcmk__str_eq(found_path, expected_path, pcmk__str_none);
        free(found_path);

        if (paths_equal) {
            return pcmk_rc_ok;
        }
    }

    return ESRCH;
}
