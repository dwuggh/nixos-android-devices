/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "selection.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int valid(const char *s)
{
    return s && (!strcmp(s, "gfuni") || !strcmp(s, "gfuni6"));
}
const char *uf_select(const char *cached, int (*qualify)(const char *, void *), void *data)
{
    const char *order[2] = {"gfuni", "gfuni6"};
    if (valid(cached) && !strcmp(cached, "gfuni6")) {
        order[0] = "gfuni6";
        order[1] = "gfuni";
    }
    for (unsigned i = 0; i < 2; i++)
        if (!qualify(order[i], data))
            return order[i];
    return NULL;
}
static int directory_fd(const char *directory)
{
    int fd = open(directory, O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC | O_RDONLY);
    if (fd < 0)
        return -1;
    struct stat st;
    if (fstat(fd, &st) || st.st_uid != geteuid() || (st.st_mode & 0077)) {
        close(fd);
        errno = EACCES;
        return -1;
    }
    return fd;
}
int uf_cache_read(const char *directory, char candidate[8])
{
    candidate[0] = 0;
    int dir = directory_fd(directory);
    if (dir < 0)
        return -1;
    int fd = openat(dir, "candidate", O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    close(dir);
    if (fd < 0)
        return -1;
    struct stat st;
    char line[8] = {0};
    int rc = -1;
    if (!fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_uid == geteuid() &&
        !(st.st_mode & 0077) && (st.st_size == 6 || st.st_size == 7) &&
        read(fd, line, sizeof(line)) == st.st_size && line[st.st_size - 1] == '\n') {
        line[st.st_size - 1] = 0;
        if (valid(line)) {
            strcpy(candidate, line);
            rc = 0;
        }
    }
    close(fd);
    return rc;
}
int uf_cache_write(const char *directory, const char *candidate)
{
    if (!valid(candidate)) {
        errno = EINVAL;
        return -1;
    }
    int dir = directory_fd(directory);
    if (dir < 0)
        return -1;
    /* The selector is a single systemd instance; stale temporary names are
     * removed only within its separate private directory. Never touch TA state. */
    unlinkat(dir, "candidate.new", 0);
    int fd = openat(dir, "candidate.new", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    int rc = -1;
    if (fd >= 0) {
        char line[8];
        int n = snprintf(line, sizeof(line), "%s\n", candidate);
        if (write(fd, line, n) == n && !fsync(fd))
            rc = 0;
        if (close(fd))
            rc = -1;
        if (!rc)
            rc = renameat(dir, "candidate.new", dir, "candidate");
        if (!rc)
            rc = fsync(dir);
    }
    int saved = errno;
    unlinkat(dir, "candidate.new", 0);
    close(dir);
    errno = saved;
    return rc;
}
