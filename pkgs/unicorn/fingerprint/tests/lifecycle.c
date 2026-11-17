/* SPDX-License-Identifier: MIT */
#define _DEFAULT_SOURCE
#include "tee.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/tee.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

static unsigned char memory[4096];
static unsigned contexts, shm_fds, mappings;
static int fail_alloc, fail_mmap, fail_madvise, fail_open, fail_close;
static unsigned glob_freed;

int __wrap_glob(const char *pattern, int flags, int (*err)(const char *, int), glob_t *paths)
{
    (void)flags;
    (void)err;
    assert(!strcmp(pattern, "/dev/tee[0-9]*"));
    static char *names[] = {"/dev/tee0", "/dev/tee42", NULL};
    paths->gl_pathc = 2;
    paths->gl_pathv = names;
    return 0;
}
void __wrap_globfree(glob_t *p)
{
    assert(p->gl_pathc == 2);
    glob_freed++;
}
int __wrap_open(const char *path, int flags, ...)
{
    assert(flags & O_CLOEXEC);
    contexts++;
    return !strcmp(path, "/dev/tee0") ? 20 : 21;
}
int __wrap_close(int fd)
{
    if (fd == 100) {
        assert(shm_fds);
        shm_fds--;
    } else {
        assert(fd == 20 || fd == 21);
        assert(contexts);
        contexts--;
    }
    return 0;
}
int __wrap_fcntl(int fd, int command, ...)
{
    assert(fd == 100 && command == F_SETFD);
    return 0;
}
void *__wrap_mmap(void *p, size_t n, int prot, int flags, int fd, off_t off)
{
    assert(!p && n == 4096 && prot == (PROT_READ | PROT_WRITE) && flags == MAP_SHARED &&
           fd == 100 && !off);
    if (fail_mmap) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    mappings++;
    return memory;
}
int __wrap_madvise(void *p, size_t n, int advice)
{
    assert(p == memory && n == 4096 && advice == MADV_DONTDUMP);
    if (fail_madvise) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}
int __wrap_munmap(void *p, size_t n)
{
    assert(p == memory && n == 4096 && mappings);
    mappings--;
    return 0;
}
int __wrap_ioctl(int fd, unsigned long cmd, ...)
{
    va_list ap;
    va_start(ap, cmd);
    void *data = va_arg(ap, void *);
    va_end(ap);
    if (cmd == TEE_IOC_VERSION) {
        assert(fd == 20 || fd == 21);
        ((struct tee_ioctl_version_data *)data)->impl_id = fd == 20 ? 1 : 5;
    } else if (cmd == TEE_IOC_SHM_ALLOC) {
        assert(fd == 21);
        if (fail_alloc) {
            errno = ENOMEM;
            return -1;
        }
        struct tee_ioctl_shm_alloc_data *arg = data;
        arg->size = 4096;
        arg->id = 3;
        shm_fds++;
        return 100;
    } else if (cmd == TEE_IOC_OPEN_SESSION) {
        struct tee_ioctl_buf_data *d = data;
        struct tee_ioctl_open_session_arg *a = (void *)(uintptr_t)d->buf_ptr;
        assert(a->num_params == 1 && !strcmp((char *)memory, "keymaster64"));
        a->session = 42;
        if (fail_open) {
            a->ret = 17;
            a->ret_origin = 2;
        }
    } else {
        assert(cmd == TEE_IOC_CLOSE_SESSION && fd == 21);
        assert(((struct tee_ioctl_close_session_arg *)data)->session == 42);
        if (fail_close) {
            errno = EIO;
            return -1;
        }
    }
    return 0;
}

int main(void)
{
    struct uf_tee t = UF_TEE_CLOSED;
    struct uf_tee_result result;
    assert(uf_tee_open(&t, "../gfuni", &result) == -1 && errno == EINVAL && !contexts);
    for (int i = 0; i < 4; i++) {
        fail_alloc = i == 0;
        fail_mmap = i == 1;
        fail_madvise = i == 2;
        fail_open = i == 3;
        assert(uf_tee_open(&t, "keymaster64", &result) == -1);
        assert(t.fd == -1 && !contexts && !shm_fds && !mappings);
        if (fail_open)
            assert(errno == EREMOTEIO && result.code == 17 && result.origin == 2);
    }
    fail_alloc = fail_mmap = fail_madvise = fail_open = 0;
    assert(!uf_tee_open(&t, "keymaster64", &result) && t.session == 42);
    assert(contexts == 1 && !shm_fds && !mappings && glob_freed == 5);
    for (unsigned i = 0; i < sizeof(memory); i++)
        assert(!memory[i]);
    struct uf_buffer buffer = {0};
    assert(!uf_tee_alloc(&t, &buffer, 320));
    memset(buffer.data, 0x5a, buffer.size);
    uf_tee_free(&buffer);
    assert(!buffer.data && !mappings && !shm_fds);
    for (unsigned i = 0; i < sizeof(memory); i++)
        assert(!memory[i]);
    uf_tee_free(&buffer);
    fail_close = 1;
    assert(uf_tee_close(&t) == -1 && errno == EIO && t.fd == -1 && !contexts);
    assert(!uf_tee_close(&t));
    puts("TEE lifetime: impl_id discovery, partial failures, memory wiping and "
         "failed-session-close fallback passed");
    return 0;
}
