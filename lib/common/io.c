/*
 * Copyright 2004-2026 the Pacemaker project contributors
 *
 * The version control history for this file may have further details.
 *
 * This source code is licensed under the GNU Lesser General Public License
 * version 2.1 or later (LGPLv2.1+) WITHOUT ANY WARRANTY.
 */

#include <crm_internal.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>            // bool, true, false
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <glib.h>               // g_*

#include <crm/crm.h>
#include <crm/common/util.h>

/*!
 * \internal
 * \brief Create a directory, including any parent directories needed
 *
 * \param[in] path_c Pathname of the directory to create
 * \param[in] mode Permissions to be used (with current umask) when creating
 *
 * \return Standard Pacemaker return code
 */
int
pcmk__build_path(const char *path_c, mode_t mode)
{
    int offset = 1, len = 0;
    int rc = pcmk_rc_ok;
    char *path = strdup(path_c);

    CRM_CHECK(path != NULL, return -ENOMEM);
    for (len = strlen(path); offset < len; offset++) {
        if (path[offset] == '/') {
            path[offset] = 0;
            if ((mkdir(path, mode) < 0) && (errno != EEXIST)) {
                rc = errno;
                goto done;
            }
            path[offset] = '/';
        }
    }
    if ((mkdir(path, mode) < 0) && (errno != EEXIST)) {
        rc = errno;
    }
done:
    free(path);
    return rc;
}

/*!
 * \internal
 * \brief Create a file name using a sequence number
 *
 * \param[in] directory  Directory that contains the file series
 * \param[in] series     Start of file name
 * \param[in] sequence   Sequence number
 * \param[in] bzip       Whether to use ".bz2" instead of ".raw" as extension
 *
 * \return Newly allocated file path (asserts on error, so always non-NULL)
 * \note The caller is responsible for freeing the return value.
 */
char *
pcmk__series_filename(const char *directory, const char *series,
                      unsigned int sequence, bool bzip)
{
    pcmk__assert((directory != NULL) && (series != NULL));
    return pcmk__assert_asprintf("%s/%s-%u.%s", directory, series, sequence,
                                 (bzip? "bz2" : "raw"));
}

/*!
 * \internal
 * \brief Read sequence number stored in a file series' .last file
 *
 * \param[in]  directory  Directory that contains the file series
 * \param[in]  series     Start of file name
 * \param[out] seq        Where to store the sequence number (unchanged on
 *                        error)
 *
 * \todo Consider returning error and checking in callers
 */
void
pcmk__read_series_sequence(const char *directory, const char *series,
                           unsigned int *seq)
{
    char *series_file = NULL;
    FILE *fp = NULL;
    unsigned int dest = 0;

    pcmk__assert((directory != NULL) && (series != NULL) && (seq != NULL));

    series_file = pcmk__assert_asprintf("%s/%s.last", directory, series);

    fp = fopen(series_file, "r");
    if (fp == NULL) {
        pcmk__debug("Could not open series file %s: %s", series_file,
                    strerror(errno));
        goto done;
    }

    errno = 0;
    if (fscanf(fp, "%u", &dest) != 1) {
        pcmk__debug("Could not read sequence number from series file %s: %s",
                    series_file, strerror((errno != 0)? errno : ENODATA));
        goto done;
    }

    *seq = dest;
    pcmk__trace("Found last sequence number %u in series file %s", *seq,
                series_file);

done:
    free(series_file);
    g_clear_pointer(&fp, (GDestroyNotify) fclose);
}

/*!
 * \internal
 * \brief Write sequence number to a file series' .last file
 *
 * \param[in] directory  Directory that contains the file series
 * \param[in] series     Start of file name
 * \param[in] sequence   Sequence number to write
 * \param[in] max        Maximum sequence value, after which it is reset to 0.
 *                       Pass 0 for no write at all. Pass a negative value for
 *                       \c UINT_MAX (effectively no maximum).
 *
 * \note This function logs some errors but does not return any to the caller
 */
void
pcmk__write_series_sequence(const char *directory, const char *series,
                            unsigned int sequence, int max)
{
    char *series_file = NULL;
    FILE *file_strm = NULL;
    int rc = 0;

    pcmk__assert((directory != NULL) && (series != NULL));

    if (max == 0) {
        goto done;
    }

    if ((max > 0) && (sequence >= max)) {
        sequence = 0;
    }

    series_file = pcmk__assert_asprintf("%s/%s.last", directory, series);

    file_strm = fopen(series_file, "w");
    if (file_strm == NULL) {
        pcmk__err("Cannot open series file %s for writing", series_file);
        goto done;
    }

    rc = fprintf(file_strm, "%u", sequence);
    if (rc < 0) {
        pcmk__err("Cannot write to series file %s", series_file);
    }

    fflush(file_strm);
    fclose(file_strm);

done:
    free(series_file);
}

/*!
 * \internal
 * \brief Change the owner and group of a file series' .last file
 *
 * \param[in] directory  Directory that contains series
 * \param[in] series     Series to change
 * \param[in] uid        User ID of desired file owner
 * \param[in] gid        Group ID of desired file group
 *
 * \return Standard Pacemaker return code
 * \note The caller must have the appropriate privileges.
 */
int
pcmk__chown_series_sequence(const char *directory, const char *series,
                            uid_t uid, gid_t gid)
{
    char *series_file = NULL;
    int rc = pcmk_rc_ok;

    pcmk__assert((directory != NULL) && (series != NULL));

    series_file = pcmk__assert_asprintf("%s/%s.last", directory, series);

    if (chown(series_file, uid, gid) < 0) {
        rc = errno;
    }

    free(series_file);
    return rc;
}

static bool
pcmk__daemon_user_can_write(const char *target_name, struct stat *target_stat)
{
    uid_t daemon_uid = 0;
    int rc = pcmk__daemon_user(&daemon_uid, NULL);

    if (rc != pcmk_rc_ok) {
        pcmk__notice("Could not find user " CRM_DAEMON_USER ": %s",
                     pcmk_rc_str(rc));
        return false;
    }
    if (target_stat->st_uid != daemon_uid) {
        pcmk__notice("%s is not owned by user " CRM_DAEMON_USER " "
                     QB_XS " uid %lld != %lld",
                     target_name, (long long) daemon_uid,
                     (long long) target_stat->st_uid);
        return false;
    }
    if ((target_stat->st_mode & (S_IRUSR | S_IWUSR)) == 0) {
        pcmk__notice("%s is not readable and writable by user %s "
                     QB_XS " st_mode=0%lo",
                     target_name, CRM_DAEMON_USER,
                     (unsigned long) target_stat->st_mode);
        return false;
    }
    return true;
}

static bool
pcmk__daemon_group_can_write(const char *target_name, struct stat *target_stat)
{
    gid_t daemon_gid = 0;
    int rc = pcmk__daemon_user(NULL, &daemon_gid);

    if (rc != pcmk_rc_ok) {
        pcmk__notice("Could not find group " CRM_DAEMON_GROUP ": %s",
                     pcmk_rc_str(rc));
        return false;
    }

    if (target_stat->st_gid != daemon_gid) {
        pcmk__notice("%s is not owned by group " CRM_DAEMON_GROUP " "
                     QB_XS " gid %lld != %lld",
                     target_name, (long long) daemon_gid,
                     (long long) target_stat->st_gid);
        return false;
    }

    if ((target_stat->st_mode & (S_IRGRP | S_IWGRP)) == 0) {
        pcmk__notice("%s is not readable and writable by group %s "
                     QB_XS " st_mode=0%lo",
                     target_name, CRM_DAEMON_GROUP,
                     (unsigned long) target_stat->st_mode);
        return false;
    }
    return true;
}

/*!
 * \internal
 * \brief Check whether a directory or file is writable by the cluster daemon
 *
 * Return true if either the cluster daemon user or cluster daemon group has
 * write permission on a specified file or directory.
 *
 * \param[in] dir      Directory to check (this argument must be specified, and
 *                     the directory must exist)
 * \param[in] file     File to check (only the directory will be checked if this
 *                     argument is not specified or the file does not exist)
 *
 * \return true if target is writable by cluster daemon, false otherwise
 */
bool
pcmk__daemon_can_write(const char *dir, const char *file)
{
    int s_res = 0;
    struct stat buf;
    char *full_file = NULL;
    const char *target = NULL;

    // Caller must supply directory
    pcmk__assert(dir != NULL);

    // If file is given, check whether it exists as a regular file
    if (file != NULL) {
        full_file = pcmk__assert_asprintf("%s/%s", dir, file);
        target = full_file;

        s_res = stat(full_file, &buf);
        if (s_res < 0) {
            pcmk__notice("%s not found: %s", target, pcmk_rc_str(errno));
            g_clear_pointer(&full_file, &free);
            target = NULL;

        } else if (!S_ISREG(buf.st_mode)) {
            pcmk__err("%s must be a regular file " QB_XS " st_mode=0%lo",
                      target, (unsigned long) buf.st_mode);
            free(full_file);
            return false;
        }
    }

    // If file is not given, ensure dir exists as directory
    if (target == NULL) {
        target = dir;
        s_res = stat(dir, &buf);
        if (s_res < 0) {
            pcmk__err("%s not found: %s", dir, pcmk_rc_str(errno));
            return false;

        } else if (!S_ISDIR(buf.st_mode)) {
            pcmk__err("%s must be a directory " QB_XS " st_mode=0%lo", dir,
                      (unsigned long) buf.st_mode);
            return false;
        }
    }

    if (!pcmk__daemon_user_can_write(target, &buf)
        && !pcmk__daemon_group_can_write(target, &buf)) {

        pcmk__err("%s must be owned and writable by either user "
                  CRM_DAEMON_USER " or group " CRM_DAEMON_GROUP " "
                  QB_XS " st_mode=0%lo",
                  target, (unsigned long) buf.st_mode);
        free(full_file);
        return false;
    }

    free(full_file);
    return true;
}

/*!
 * \internal
 * \brief Flush and sync a directory to disk
 *
 * \param[in] name Directory to flush and sync
 * \note This function logs errors but does not return them to the caller
 */
void
pcmk__sync_directory(const char *name)
{
    int fd = -1;

    fd = open(name, O_DIRECTORY | O_RDONLY);
    if (fd < 0) {
        pcmk__err("Could not open %s for syncing: %s", name, strerror(errno));
        return;
    }

    if (fsync(fd) < 0) {
        pcmk__err("Could not sync %s: %s", name, strerror(errno));
    }

    if (close(fd) < 0) {
        pcmk__err("Could not close %s after fsync: %s", name, strerror(errno));
    }
}

/*!
 * \internal
 * \brief Write text to a file, flush and sync it to disk, then close the file
 *
 * \param[in] fd        File descriptor opened for writing
 * \param[in] contents  String to write to file
 *
 * \return Standard Pacemaker return code
 */
int
pcmk__write_sync(int fd, const char *contents)
{
    int rc = 0;
    FILE *fp = fdopen(fd, "w");

    if (fp == NULL) {
        return errno;
    }
    if ((contents != NULL) && (fprintf(fp, "%s", contents) < 0)) {
        rc = EIO;
    }
    if (fflush(fp) != 0) {
        rc = errno;
    }
    if (fsync(fileno(fp)) < 0) {
        rc = errno;
    }
    fclose(fp);
    return rc;
}

/*!
 * \internal
 * \brief Close open file descriptors except standard streams
 *
 * Close all file descriptors (except stdin, stdout, and stderr), which is a
 * best practice for a new child process forked for the purpose of executing an
 * external program.
 */
void
pcmk__close_fds_in_child(void)
{
    DIR *dir;
    struct rlimit rlim;
    rlim_t max_fd;
    const int min_fd = STDERR_FILENO + 1;

    /* Find the current process's (soft) limit for open files. getrlimit()
     * should always work, but have a fallback just in case.
     */
    if (getrlimit(RLIMIT_NOFILE, &rlim) == 0) {
        max_fd = rlim.rlim_cur - 1;
    } else {
        long conf_max = sysconf(_SC_OPEN_MAX);

        max_fd = (conf_max > 0)? conf_max : 1024;
    }

    /* First try /proc.  If that returns NULL (either because opening the
     * directory failed, or because procfs isn't supported on this platform),
     * fall back to /dev/fd.
     */
    dir = pcmk__procfs_fd_dir();
    if (dir == NULL) {
        dir = opendir("/dev/fd");
    }

    if (dir != NULL) {
        struct dirent *entry;
        int dir_fd = dirfd(dir);

        while ((entry = readdir(dir)) != NULL) {
            int lpc = atoi(entry->d_name);

            /* How could one of these entries be higher than max_fd, you ask?
             * It isn't possible in normal operation, but when run under
             * valgrind, valgrind can open high-numbered file descriptors for
             * its own use that are higher than the process's soft limit.
             * These will show up in the fd directory but aren't closable.
             */
            if ((lpc >= min_fd) && (lpc <= max_fd) && (lpc != dir_fd)) {
                close(lpc);
            }
        }
        closedir(dir);
        return;
    }

    /* If no fd directory is available, iterate over all possible descriptors.
     * This is less efficient due to the overhead of many system calls.
     */
    for (int lpc = max_fd; lpc >= min_fd; lpc--) {
        close(lpc);
    }
}
