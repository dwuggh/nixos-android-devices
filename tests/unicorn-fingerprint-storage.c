/* SPDX-License-Identifier: MIT
 * Tests the pinned listener's real GPFS handler, not a storage simulation. */
#include "qsee_supplicant.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fail_rename;
int __real_renameat(int, const char *, int, const char *);
int __wrap_renameat(int a, const char *b, int c, const char *d)
{
    if (fail_rename) { errno = EIO; return -1; }
    return __real_renameat(a,b,c,d);
}
static void p32(void *p, uint32_t v) { memcpy(p,&v,4); }
static uint32_t u32(const void *p) { uint32_t v; memcpy(&v,p,4); return v; }
static void request(uint8_t *b, unsigned command, const char *name)
{
    memset(b,0,QS_GPFS_BUFFER_SIZE); p32(b,command);
    strcpy((char *)b + 4,name);
}

int main(void)
{
    char root[] = "/tmp/unicorn-storage.XXXXXX";
    assert(mkdtemp(root));
    uint8_t *b = calloc(1,QS_GPFS_BUFFER_SIZE); assert(b);
    struct qs_store store = {.root_fd = -1};
    assert(!qs_store_open(&store,root,0600,0700));
    /* Absolute Android paths remain names inside the isolated state root. */
    const char *name = "/persist/fingerprint/object.so";
    request(b,1,name); p32(b+0x108,6); memcpy(b+0x110,"sealed",6);
    assert(!qs_gpfs_dispatch(&store,b,QS_GPFS_BUFFER_SIZE) && !u32(b+4));
    qs_store_close(&store);
    assert(!qs_store_open(&store,root,0600,0700));
    request(b,0,name); p32(b+0x108,6);
    assert(!qs_gpfs_dispatch(&store,b,QS_GPFS_BUFFER_SIZE));
    assert(!u32(b+4) && u32(b+8)==6 && !memcmp(b+12,"sealed",6));
    fail_rename=1;
    request(b,1,name); p32(b+0x108,3); memcpy(b+0x110,"new",3);
    assert(!qs_gpfs_dispatch(&store,b,QS_GPFS_BUFFER_SIZE) && u32(b+4));
    fail_rename=0;
    request(b,0,name); p32(b+0x108,6);
    assert(!qs_gpfs_dispatch(&store,b,QS_GPFS_BUFFER_SIZE));
    assert(!u32(b+4) && u32(b+8)==6 && !memcmp(b+12,"sealed",6));
    char path[512];
    snprintf(path,sizeof(path),"%s/persist/fingerprint",root);
    DIR *dir = opendir(path); assert(dir);
    struct dirent *entry;
    while ((entry=readdir(dir))) assert(!strstr(entry->d_name,".tmp."));
    closedir(dir);
    request(b,1,"../escape"); p32(b+0x108,1);
    assert(!qs_gpfs_dispatch(&store,b,QS_GPFS_BUFFER_SIZE) && u32(b+4));
    snprintf(path,sizeof(path),"%s/outside",root); assert(!symlink("/tmp",path));
    request(b,1,"outside/escape"); p32(b+0x108,1);
    assert(!qs_gpfs_dispatch(&store,b,QS_GPFS_BUFFER_SIZE) && u32(b+4));
    snprintf(path,sizeof(path),"%s/persist/fingerprint/object.so",root);
    struct stat st; assert(!stat(path,&st) && (st.st_mode & 0777)==0600);
    qs_store_close(&store); free(b);
    assert(!unlink(path));
    snprintf(path,sizeof(path),"%s/outside",root); assert(!unlink(path));
    snprintf(path,sizeof(path),"%s/persist/fingerprint",root); assert(!rmdir(path));
    snprintf(path,sizeof(path),"%s/persist",root); assert(!rmdir(path));
    assert(!rmdir(root));
    puts("listeners: isolated paths, reopen persistence, failed replacement preserves old object, temporary cleanup passed");
    return 0;
}
