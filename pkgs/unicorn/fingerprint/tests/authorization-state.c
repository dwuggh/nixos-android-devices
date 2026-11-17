/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "authorization-state.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
    char path[] = "/tmp/uf-credential-XXXXXX";
    assert(mkdtemp(path));
    int dir = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(dir >= 0);
    struct uf_credential original = {.uid = 1000, .handle_size = 58}, loaded;
    memset(original.salt, 0x71, sizeof(original.salt));
    memset(original.handle, 0x92, original.handle_size);
    assert(uf_credential_load(dir, 1000, &loaded) == -1 && errno == ENOENT);
    assert(!uf_credential_create(dir, &original));
    assert(!uf_credential_load(dir, 1000, &loaded));
    assert(loaded.uid == original.uid && loaded.handle_size == original.handle_size);
    assert(!memcmp(loaded.salt, original.salt, 16));
    assert(!memcmp(loaded.handle, original.handle, 58));
    original.handle[0]++;
    assert(uf_credential_create(dir, &original) == -1 && errno == EEXIST);
    assert(!uf_credential_load(dir, 1000, &loaded) && loaded.handle[0] == 0x92);
    assert(!fchmodat(dir, "credential-1000", 0644, 0));
    assert(uf_credential_load(dir, 1000, &loaded) == -1 && errno == EACCES);
    assert(!fchmodat(dir, "credential-1000", 0600, 0));
    assert(!linkat(dir, "credential-1000", dir, "extra", 0));
    assert(uf_credential_load(dir, 1000, &loaded) == -1 && errno == EACCES);
    assert(!unlinkat(dir, "extra", 0));
    assert(!renameat(dir, "credential-1000", dir, "saved"));
    assert(!symlinkat("saved", dir, "credential-1000"));
    assert(uf_credential_load(dir, 1000, &loaded) == -1 && errno == ELOOP);
    assert(uf_credential_create(dir, &original) == -1 && errno == EEXIST);
    assert(!unlinkat(dir, "credential-1000", 0));
    assert(!renameat(dir, "saved", dir, "credential-1000"));
    int fd = openat(dir, "credential-1000", O_WRONLY | O_CLOEXEC);
    assert(fd >= 0 && pwrite(fd, "bad", 3, 0) == 3);
    close(fd);
    assert(uf_credential_load(dir, 1000, &loaded) == -1 && errno == EPROTO);
    assert(uf_credential_load(dir, 65536, &loaded) == -1 && errno == EINVAL);
    unsigned char a[32], b[32];
    assert(!uf_credential_derive("test-only", 9, original.salt, a));
    assert(!uf_credential_derive("test-only", 9, original.salt, b) && !memcmp(a, b, 32));
    original.salt[0]++;
    assert(!uf_credential_derive("test-only", 9, original.salt, b) && memcmp(a, b, 32));
    assert(uf_credential_derive("", 0, original.salt, b) == -1 && errno == EINVAL);
    for (unsigned i = 0; i < sizeof(b); i++) assert(!b[i]);
    assert(!unlinkat(dir, "credential-1000", 0));
    close(dir);
    assert(!rmdir(path));
    puts("authorization state: atomic no-replace, modes, links, corruption and KDF passed");
}
