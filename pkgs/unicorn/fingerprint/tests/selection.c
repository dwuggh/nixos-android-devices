/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "selection.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static unsigned calls, mask;
static const char *seen[2];
static int qualify(const char *name, void *unused)
{
    (void)unused;
    seen[calls++] = name;
    return mask & (!strcmp(name, "gfuni") ? 1 : 2) ? 0 : -1;
}
int main(void)
{
    for (mask = 0; mask < 4; mask++) {
        for (unsigned cached = 0; cached < 3; cached++) {
            calls = 0;
            const char *winner = uf_select(cached == 0 ? "invalid" : cached == 1 ? "gfuni" : "gfuni6", qualify, NULL);
            assert(!strcmp(seen[0], cached == 2 ? "gfuni6" : "gfuni"));
            assert(!!winner == !!mask);
            if (winner)
                assert(!strcmp(winner, mask == 1 ? "gfuni" : mask == 2 ? "gfuni6" : seen[0]));
            assert(calls == (!mask || strcmp(winner, seen[0]) ? 2 : 1));
        }
    }
    char directory[] = "/tmp/uf-cache-XXXXXX", path[128], value[8];
    assert(mkdtemp(directory));
    assert(uf_cache_read(directory, value) && !value[0]);
    assert(!uf_cache_write(directory, "gfuni6"));
    assert(!uf_cache_read(directory, value) && !strcmp(value, "gfuni6"));
    assert(!uf_cache_write(directory, "gfuni"));
    assert(!uf_cache_read(directory, value) && !strcmp(value, "gfuni"));
    snprintf(path, sizeof(path), "%s/candidate", directory);
    int fd = open(path, O_WRONLY | O_TRUNC);
    assert(fd >= 0 && write(fd, "gfuni6\njunk", 11) == 11);
    close(fd);
    assert(uf_cache_read(directory, value) && !value[0]);
    unlink(path);
    assert(!symlink("/dev/null", path));
    assert(uf_cache_read(directory, value));
    assert(!uf_cache_write(directory, "gfuni")); /* Atomic replace, never follow symlink. */
    assert(!chmod(directory, 0755));
    assert(uf_cache_read(directory, value));
    assert(uf_cache_write(directory, "gfuni"));
    unlink(path); rmdir(directory);
    puts("selection: full qualification, first success, cache ordering, invalid cache and protected atomic writes passed");
}
