/* SPDX-License-Identifier: MIT */
#include "tee.h"
#include <assert.h>
#include <errno.h>
#include <linux/tee.h>
#include <stdarg.h>
#include <stdio.h>

static unsigned calls, expected_count;
static size_t expected_response_offset;
static size_t expected_request_length = 64;
static int error;
static uint32_t tee_code;
static int allocating, allocation_reply;
static unsigned private_patch;

int __wrap_ioctl(int fd, unsigned long command, ...)
{
    va_list ap;
    va_start(ap, command);
    struct tee_ioctl_buf_data *data = va_arg(ap, void *);
    va_end(ap);
    assert(fd == 17 && command == TEE_IOC_INVOKE);
    struct tee_ioctl_invoke_arg *arg = (void *)(uintptr_t)data->buf_ptr;
    struct tee_ioctl_param *p = (void *)(arg + 1);
    assert(arg->session == 42);
    if (allocating) {
        assert(arg->func == 0x55460001 && !arg->num_params && data->buf_len == sizeof(*arg));
        calls++;
        if (error) {
            errno = error;
            return -1;
        }
        return allocation_reply;
    }
    assert(arg->func == 0);
    assert(arg->num_params == 2 + 2 * (expected_count + private_patch));
    assert(data->buf_len == sizeof(*arg) + arg->num_params * sizeof(*p));
    assert(p[0].attr == TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT);
    assert(p[0].a == 0 && p[0].b == expected_request_length && p[0].c == 7);
    assert(p[1].attr == TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT);
    assert(p[1].a == expected_response_offset && p[1].c == 7);
    assert(p[1].b == (expected_count ? 64U : 256U));
    for (unsigned i = 0; i < expected_count; i++) {
        assert(p[2 + 2 * i].attr == TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT);
        assert(p[2 + 2 * i].a == 32 * i && p[2 + 2 * i].b == 4);
        assert(p[3 + 2 * i].attr == TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT);
        assert(p[3 + 2 * i].a == 16 * i && p[3 + 2 * i].b == 4096);
        assert(p[3 + 2 * i].c == 8 + i);
    }
    if (private_patch) {
        unsigned k = 2 + 2 * expected_count;
        assert(p[k].attr == TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT && p[k].a == 32 && p[k].b == 4 &&
               p[k].c == 1);
        assert(p[k + 1].attr == TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT && !p[k + 1].a &&
               !p[k + 1].b && !p[k + 1].c);
    }
    calls++;
    if (error) {
        errno = error;
        return -1;
    }
    arg->ret = tee_code;
    arg->ret_origin = 3;
    return 0;
}

int main(void)
{
    char dummy;
    struct uf_tee tee = {.fd = 17, .session = 42};
    struct uf_buffer header = {&dummy, 512, 7};
    struct uf_buffer payload = {&dummy, 8192, 8}, carveout = {&dummy, 8192, 9};
    struct uf_ref request = {&header, 0, 64}, response = {&header, 64, 256};
    struct uf_patch patches[2] = {{0, 4, {&payload, 0, 4096}}, {32, 4, {&carveout, 16, 4096}}};
    struct uf_tee_result result;
    expected_response_offset = 64;
    assert(!uf_tee_invoke(&tee, request, response, NULL, 0, &result));
    expected_request_length = 46;
    expected_response_offset = 48;
    assert(!uf_tee_invoke(&tee, (struct uf_ref){&header, 0, 46},
                          (struct uf_ref){&header, 48, 256}, NULL, 0, &result));
    expected_request_length = 64;
    expected_response_offset = 0;
    expected_count = 2;
    response = (struct uf_ref){&header, 0, 64};
    assert(!uf_tee_invoke(&tee, request, response, patches, 2, &result));
    tee_code = 0x12345678;
    assert(uf_tee_invoke(&tee, request, response, patches, 2, &result) == -1);
    assert(errno == EREMOTEIO && result.code == tee_code && result.origin == 3);
    tee_code = 0;
    const int failures[] = {ETIMEDOUT, EINTR, ENODEV, EIO};
    for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); i++) {
        error = failures[i];
        unsigned before = calls;
        assert(uf_tee_invoke(&tee, request, response, patches, 2, &result) == -1);
        assert(errno == error && result.code == 0 && calls == before + 1);
    }
    error = 0;
    unsigned before = calls;
#define REJECT(req, rsp, ps, n)                                                                    \
    do {                                                                                           \
        assert(uf_tee_invoke(&tee, req, rsp, ps, n, &result) == -1);                               \
        assert(errno == EINVAL && calls == before);                                                \
    } while (0)
    struct uf_ref bad[] = {{NULL, 0, 64},           {&header, 0, 0},        {&header, 513, 1},
                           {&header, SIZE_MAX, 64}, {&header, 0, SIZE_MAX}, {&header, 500, 64}};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        REJECT(bad[i], response, patches, 2);
        REJECT(request, bad[i], patches, 2);
    }
    struct uf_ref partial = {&header, 32, 64};
    REJECT(request, partial, patches, 2);
    REJECT(request, response, NULL, 1);
    REJECT(request, response, patches, SIZE_MAX);
    struct uf_patch bad_patch[] = {{0, 3, {&payload, 0, 1}},        {61, 4, {&payload, 0, 1}},
                                   {SIZE_MAX, 8, {&payload, 0, 1}}, {0, 8, {&payload, 8192, 1}},
                                   {0, 4, {&payload, 0, 0}},        {0, 4, {&header, 0, 64}}};
    for (size_t i = 0; i < sizeof(bad_patch) / sizeof(bad_patch[0]); i++)
        REJECT(request, response, bad_patch + i, 1);
    patches[1].offset = 3;
    REJECT(request, response, patches, 2);
    patches[1].offset = 32;
    patches[1].memory = patches[0].memory;
    REJECT(request, response, patches, 2);
    payload.size = SIZE_MAX;
    patches[0].memory.length = UF_TEE_MAX_XFER;
    REJECT(request, response, patches, 1);
    payload.id = -1;
    REJECT(request, response, patches, 1);
    payload.id = 8;
    payload.size = 8192;
    patches[0].memory.length = 4096;
    expected_count = 1;
    private_patch = 1;
    assert(!uf_tee_invoke_private(&tee, request, response, patches, 1, 32, &result));
    before = calls;
    assert(uf_tee_invoke_private(&tee, request, response, patches, 1, 2, &result) == -1 &&
           calls == before);
    assert(uf_tee_invoke_private(&tee, request, response, patches, 1, SIZE_MAX - 1, &result) ==
               -1 &&
           calls == before);
    allocating = 1;
    allocation_reply = 1;
    assert(uf_tee_private_alloc(&tee) == 0);
    allocation_reply = 20;
    assert(uf_tee_private_alloc(&tee) == 19);
    allocation_reply = 0;
    assert(uf_tee_private_alloc(&tee) == -1 && errno == EPROTO);
    error = ENOTTY;
    assert(uf_tee_private_alloc(&tee) == -1 && errno == ENOTTY);
    puts("TEE: separate/aliased replies, dual mappings, errno/TEE separation, invalid ranges and "
         "no retry passed");
    return 0;
}
