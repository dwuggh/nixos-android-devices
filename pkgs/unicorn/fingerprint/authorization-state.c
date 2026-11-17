/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "authorization-state.h"
#include "protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <string.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>

#define CREDENTIAL_MAGIC UINT32_C(0x55464331)
int uf_credential_derive(const char *password, size_t size, const unsigned char salt[16],
                         unsigned char secret[32])
{
    if (secret)
        uf_wipe(secret, 32);
    if (!password || !size || size > 128 || !salt || !secret) {
        errno = EINVAL;
        return -1;
    }
    memset(secret, 0, 32);
    if (EVP_PBE_scrypt(password, size, salt, 16, UINT64_C(1) << 15, 8, 1,
                      UINT64_C(64) * 1024 * 1024, secret, 32) != 1) {
        uf_wipe(secret, 32);
        errno = EIO;
        return -1;
    }
    return 0;
}
static int filename(char name[32], uint32_t uid)
{
    if (uid > UINT16_MAX) {
        errno = EINVAL;
        return -1;
    }
    snprintf(name, 32, "credential-%u", uid);
    return 0;
}
int uf_credential_load(int directory, uint32_t uid, struct uf_credential *credential)
{
    if (!credential) {
        errno = EINVAL;
        return -1;
    }
    memset(credential, 0, sizeof(*credential));
    char name[32];
    if (filename(name, uid))
        return -1;
    int fd = openat(directory, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
        return -1;
    unsigned char data[32 + UF_GATEKEEPER_HANDLE_MAX] = {0};
    int rc = -1, saved;
    struct stat st;
    if (fstat(fd, &st))
        goto out;
    if (!S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0777) != 0600 ||
        st.st_nlink != 1 || st.st_size < 41 || st.st_size > (off_t)sizeof(data)) {
        errno = EACCES;
        goto out;
    }
    size_t offset = 0;
    while (offset < (size_t)st.st_size) {
        ssize_t n = read(fd, data + offset, st.st_size - offset);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            if (!n)
                errno = EIO;
            goto out;
        }
        offset += n;
    }
    unsigned char extra;
    if (read(fd, &extra, 1) != 0) {
        errno = EPROTO;
        goto out;
    }
    uint32_t magic, version, recorded_uid, size;
    uf_wire_get32(data, sizeof(data), 0, &magic);
    uf_wire_get32(data, sizeof(data), 4, &version);
    uf_wire_get32(data, sizeof(data), 8, &recorded_uid);
    uf_wire_get32(data, sizeof(data), 12, &size);
    if (magic != CREDENTIAL_MAGIC || version != 1 || recorded_uid != uid ||
        size != offset - 32) {
        errno = EPROTO;
        goto out;
    }
    credential->uid = uid;
    credential->handle_size = size;
    memcpy(credential->salt, data + 16, 16);
    memcpy(credential->handle, data + 32, size);
    rc = 0;
out:
    saved = errno;
    uf_wipe(data, sizeof(data));
    close(fd);
    errno = saved;
    return rc;
}
int uf_credential_create(int directory, const struct uf_credential *credential)
{
    if (!credential || credential->handle_size < 9 ||
        credential->handle_size > UF_GATEKEEPER_HANDLE_MAX) {
        errno = EINVAL;
        return -1;
    }
    char name[32], temporary[64];
    if (filename(name, credential->uid))
        return -1;
    uint64_t random;
    if (getrandom(&random, sizeof(random), 0) != sizeof(random))
        return -1;
    snprintf(temporary, sizeof(temporary), ".credential-%u-%016llx", credential->uid,
             (unsigned long long)random);
    int fd = openat(directory, temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return -1;
    unsigned char data[32 + UF_GATEKEEPER_HANDLE_MAX] = {0};
    uf_wire_put32(data, sizeof(data), 0, CREDENTIAL_MAGIC);
    uf_wire_put32(data, sizeof(data), 4, 1);
    uf_wire_put32(data, sizeof(data), 8, credential->uid);
    uf_wire_put32(data, sizeof(data), 12, credential->handle_size);
    memcpy(data + 16, credential->salt, 16);
    memcpy(data + 32, credential->handle, credential->handle_size);
    size_t offset = 0, size = 32 + credential->handle_size;
    int rc = -1;
    while (offset < size) {
        ssize_t n = write(fd, data + offset, size - offset);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            if (!n)
                errno = EIO;
            goto out;
        }
        offset += n;
    }
    if (fsync(fd) || renameat2(directory, temporary, directory, name, RENAME_NOREPLACE) ||
        fsync(directory))
        goto out;
    rc = 0;
out:;
    int saved = errno;
    close(fd);
    unlinkat(directory, temporary, 0);
    uf_wipe(data, sizeof(data));
    errno = saved;
    return rc;
}
