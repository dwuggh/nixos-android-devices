/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "platform.h"
#include <errno.h>
#include <glob.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static int line(const char *path, char *value, size_t size)
{
    FILE *file = fopen(path, "re");
    if (!file)
        return -1;
    char *got = fgets(value, size, file);
    int saved = errno;
    fclose(file);
    if (!got) {
        errno = saved ? saved : EIO;
        return -1;
    }
    value[strcspn(value, "\n")] = 0;
    return 0;
}

static int check(const char *sysfs, const char *dev, char *detail, size_t size)
{
    char pattern[512], path[512], name[64], state[64] = "missing";
    glob_t entries = {0};
    unsigned found = 0;
    snprintf(pattern, sizeof(pattern), "%s/remoteproc*/name", sysfs);
    int rc = glob(pattern, 0, NULL, &entries);
    if (rc && rc != GLOB_NOMATCH) {
        globfree(&entries);
        snprintf(detail, size, "cannot enumerate remote processors");
        errno = EIO;
        return -1;
    }
    for (size_t i = 0; i < entries.gl_pathc; i++) {
        if (line(entries.gl_pathv[i], name, sizeof(name)) || strcmp(name, "cdsp"))
            continue;
        found++;
        snprintf(path, sizeof(path), "%.*s/state",
                 (int)(strlen(entries.gl_pathv[i]) - strlen("/name")), entries.gl_pathv[i]);
        if (line(path, state, sizeof(state)))
            strcpy(state, "unreadable");
    }
    globfree(&entries);
    if (found != 1 || strcmp(state, "running")) {
        snprintf(detail, size, "CDSP count=%u state=%s; inspect remoteproc boot journal", found, state);
        errno = ENODEV;
        return -1;
    }
    const char *nodes[] = {"fastrpc-cdsp", "fastrpc-cdsp-secure"};
    for (unsigned i = 0; i < 2; i++) {
        struct stat st;
        snprintf(path, sizeof(path), "%s/%s", dev, nodes[i]);
        if (!stat(path, &st) && S_ISCHR(st.st_mode))
            return 0;
    }
    snprintf(detail, size, "CDSP running but its FastRPC character device is missing");
    errno = ENODEV;
    return -1;
}

int uf_platform_check(char *detail, size_t size)
{
    return check("/sys/class/remoteproc", "/dev", detail, size);
}
