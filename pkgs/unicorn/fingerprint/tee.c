/* SPDX-License-Identifier: MIT */
#define _DEFAULT_SOURCE
#include "tee.h"
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/tee.h>
#include <stdbool.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

void uf_wipe(void *data, size_t size)
{
    volatile unsigned char *p = data;
    while (size--)
        *p++ = 0;
}

static int invalid(void)
{
    errno = EINVAL;
    return -1;
}

int uf_tee_alloc(struct uf_tee *tee, struct uf_buffer *buffer, size_t size)
{
    if (!tee || tee->fd < 0 || !buffer || buffer->data || !size || size > UF_TEE_MAX_XFER)
        return invalid();
    struct tee_ioctl_shm_alloc_data arg = {.size = size};
    int fd = ioctl(tee->fd, TEE_IOC_SHM_ALLOC, &arg);
    if (fd < 0)
        return -1;
    if (arg.size < size || arg.size > UF_TEE_MAX_XFER || arg.id < 0) {
        close(fd);
        errno = EPROTO;
        return -1;
    }
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    void *p = mmap(NULL, arg.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    int saved = errno;
    close(fd);
    if (p == MAP_FAILED) {
        errno = saved;
        return -1;
    }
    if (madvise(p, arg.size, MADV_DONTDUMP)) {
        saved = errno;
        munmap(p, arg.size);
        errno = saved;
        return -1;
    }
    memset(p, 0, arg.size);
    *buffer = (struct uf_buffer){.data = p, .size = arg.size, .id = arg.id};
    return 0;
}

void uf_tee_free(struct uf_buffer *buffer)
{
    int saved = errno;
    if (buffer && buffer->data) {
        uf_wipe(buffer->data, buffer->size);
        munmap(buffer->data, buffer->size);
    }
    if (buffer)
        *buffer = (struct uf_buffer){0};
    errno = saved;
}

static int discover(void)
{
    glob_t paths = {0};
    int found = -1, error = ENODEV;
    if (glob("/dev/tee[0-9]*", 0, NULL, &paths)) {
        globfree(&paths);
        errno = ENODEV;
        return -1;
    }
    for (size_t i = 0; i < paths.gl_pathc; i++) {
        int fd = open(paths.gl_pathv[i], O_RDWR | O_CLOEXEC);
        if (fd < 0) {
            if (errno == EACCES)
                error = EACCES;
            continue;
        }
        struct tee_ioctl_version_data version = {0};
        if (!ioctl(fd, TEE_IOC_VERSION, &version) && version.impl_id == 5) {
            found = fd;
            break;
        }
        close(fd);
    }
    globfree(&paths);
    if (found < 0)
        errno = error;
    return found;
}

int uf_tee_open(struct uf_tee *tee, const char *name, struct uf_tee_result *result)
{
    if (result)
        *result = (struct uf_tee_result){0};
    if (!tee || tee->fd != -1 || !name || !*name || strlen(name) >= 64)
        return invalid();
    for (const char *p = name; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
              *p == '_' || *p == '-'))
            return invalid();
    tee->fd = discover();
    if (tee->fd < 0)
        return -1;
    struct uf_buffer buffer = {0};
    struct {
        struct tee_ioctl_open_session_arg arg;
        struct tee_ioctl_param p;
    } req = {0};
    if (uf_tee_alloc(tee, &buffer, 64))
        goto fail;
    memcpy(buffer.data, name, strlen(name) + 1);
    req.arg.num_params = 1;
    req.p.attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT;
    req.p.b = strlen(name) + 1;
    req.p.c = buffer.id;
    struct tee_ioctl_buf_data data = {.buf_ptr = (uintptr_t)&req, .buf_len = sizeof(req)};
    int rc = ioctl(tee->fd, TEE_IOC_OPEN_SESSION, &data);
    uf_tee_free(&buffer);
    if (rc < 0)
        goto fail;
    if (result)
        *result = (struct uf_tee_result){req.arg.ret, req.arg.ret_origin};
    if (req.arg.ret) {
        errno = EREMOTEIO;
        goto fail;
    }
    tee->session = req.arg.session;
    return 0;
fail:;
    int saved = errno;
    close(tee->fd);
    *tee = (struct uf_tee)UF_TEE_CLOSED;
    errno = saved;
    return -1;
}

int uf_tee_close(struct uf_tee *tee)
{
    if (!tee)
        return invalid();
    if (tee->fd < 0)
        return 0;
    struct tee_ioctl_close_session_arg arg = {.session = tee->session};
    int rc = ioctl(tee->fd, TEE_IOC_CLOSE_SESSION, &arg), saved = errno;
    /* Closing the context is also the fallback if CLOSE_SESSION failed. */
    if (close(tee->fd) < 0 && !rc) {
        rc = -1;
        saved = errno;
    }
    *tee = (struct uf_tee)UF_TEE_CLOSED;
    errno = saved;
    return rc;
}

static bool valid_ref(struct uf_ref r)
{
    return r.buffer && r.buffer->data && r.buffer->id >= 0 && r.length &&
           r.length <= r.buffer->size && r.offset <= r.buffer->size - r.length;
}

static bool overlaps(struct uf_ref a, struct uf_ref b)
{
    return a.buffer->id == b.buffer->id && a.offset < b.offset + b.length &&
           b.offset < a.offset + a.length;
}

static void memref(struct tee_ioctl_param *p, struct uf_ref r, uint64_t attr)
{
    *p = (struct tee_ioctl_param){.attr = attr, .a = r.offset, .b = r.length, .c = r.buffer->id};
}

static int invoke(struct uf_tee *tee, struct uf_ref request, struct uf_ref response,
                  const struct uf_patch *patches, size_t count, size_t private_offset,
                  struct uf_tee_result *result)
{
    bool private = private_offset != SIZE_MAX;
    if (result)
        *result = (struct uf_tee_result){0};
    if (!tee || tee->fd < 0 || !valid_ref(request) || !valid_ref(response) ||
        count > UF_TEE_MAX_PATCHES - private || (count && !patches) ||
        (private && (request.length < 4 || private_offset > request.length - 4)))
        return invalid();
    /* The kernel preserves exact co-location. Partial overlap has different
     * staging semantics and is not part of either verified OEM call layout. */
    if (overlaps(request, response) && request.offset != response.offset)
        return invalid();
    size_t total = request.length;
    if (total > UF_TEE_MAX_XFER || response.length > UF_TEE_MAX_XFER - total)
        return invalid();
    total += response.length;
    for (size_t i = 0; i < count; i++) {
        const struct uf_patch *p = patches + i;
        if (private && p->offset < private_offset + 4 && private_offset < p->offset + p->width)
            return invalid();
        if ((p->width != 4 && p->width != 8) || p->width > request.length ||
            p->offset > request.length - p->width || !valid_ref(p->memory) ||
            p->memory.length > UF_TEE_MAX_XFER - total || overlaps(p->memory, request) ||
            overlaps(p->memory, response))
            return invalid();
        for (size_t j = 0; j < i; j++) {
            if ((p->offset < patches[j].offset + patches[j].width &&
                 patches[j].offset < p->offset + p->width) ||
                overlaps(p->memory, patches[j].memory))
                return invalid();
        }
        total += p->memory.length;
    }
    struct {
        struct tee_ioctl_invoke_arg arg;
        struct tee_ioctl_param p[10];
    } req = {0};
    req.arg.session = tee->session;
    req.arg.num_params = 2 + 2 * (count + private);
    memref(req.p, request, TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT);
    memref(req.p + 1, response, TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT);
    for (size_t i = 0; i < count; i++) {
        req.p[2 + 2 * i] = (struct tee_ioctl_param){
            .attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT,
            .a = patches[i].offset,
            .b = patches[i].width,
        };
        memref(req.p + 3 + 2 * i, patches[i].memory, TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT);
    }
    if (private) {
        req.p[2 + 2 * count] = (struct tee_ioctl_param){
            .attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT,
            .a = private_offset,
            .b = 4,
            .c = 1,
        };
        req.p[3 + 2 * count].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
    }
    struct tee_ioctl_buf_data data = {
        .buf_ptr = (uintptr_t)&req,
        .buf_len = sizeof(req.arg) + req.arg.num_params * sizeof(req.p[0]),
    };
    if (ioctl(tee->fd, TEE_IOC_INVOKE, &data) < 0)
        return -1;
    if (result)
        *result = (struct uf_tee_result){req.arg.ret, req.arg.ret_origin};
    if (req.arg.ret) {
        errno = EREMOTEIO;
        return -1;
    }
    return 0;
}

int uf_tee_invoke(struct uf_tee *t, struct uf_ref q, struct uf_ref r, const struct uf_patch *p,
                  size_t n, struct uf_tee_result *result)
{
    return invoke(t, q, r, p, n, SIZE_MAX, result);
}
int uf_tee_invoke_private(struct uf_tee *t, struct uf_ref q, struct uf_ref r,
                          const struct uf_patch *p, size_t n, size_t off,
                          struct uf_tee_result *result)
{
    return invoke(t, q, r, p, n, off, result);
}
int uf_tee_private_alloc(struct uf_tee *t)
{
    if (!t || t->fd < 0)
        return invalid();
    struct tee_ioctl_invoke_arg arg = {.func = 0x55460001, .session = t->session};
    struct tee_ioctl_buf_data data = {.buf_ptr = (uintptr_t)&arg, .buf_len = sizeof(arg)};
    int rc = ioctl(t->fd, TEE_IOC_INVOKE, &data);
    if (rc < 0)
        return -1;
    if (!rc) {
        errno = EPROTO;
        return -1;
    }
    return rc - 1;
}
