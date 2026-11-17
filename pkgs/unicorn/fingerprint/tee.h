/* SPDX-License-Identifier: MIT */
#ifndef UNICORN_FP_TEE_H
#define UNICORN_FP_TEE_H
#include <stddef.h>
#include <stdint.h>

#define UF_TEE_MAX_XFER (16U * 1024U * 1024U)
#define UF_TEE_MAX_PATCHES 4U
#define UF_TEE_CLOSED {.fd = -1}

struct uf_tee {
    int fd;
    uint32_t session;
};
struct uf_buffer {
    void *data;
    size_t size;
    int32_t id;
};
struct uf_ref {
    const struct uf_buffer *buffer;
    size_t offset, length;
};
struct uf_patch {
    size_t offset;
    unsigned width;
    struct uf_ref memory;
};
/* TEE return codes are separate from errno and the application's wire status. */
struct uf_tee_result {
    uint32_t code, origin;
};

void uf_wipe(void *data, size_t size);
/* Client attachment only. Loading and listeners belong to qsee-supplicant. */
int uf_tee_open(struct uf_tee *, const char *name, struct uf_tee_result *);
int uf_tee_close(struct uf_tee *);
int uf_tee_alloc(struct uf_tee *, struct uf_buffer *, size_t size);
void uf_tee_free(struct uf_buffer *);
/* Synchronous ioctl: this kernel has no cancel_req operation. Never retry an
 * interrupted command: its side effects may already have happened. */
int uf_tee_invoke(struct uf_tee *, struct uf_ref request, struct uf_ref response,
                  const struct uf_patch *, size_t count, struct uf_tee_result *);
/* Local QSEECOM extension: one persistent, non-CPU-mappable 4 MiB DMA buffer.
 * The context retains it through TA session teardown. Caller owns returned fd. */
int uf_tee_private_alloc(struct uf_tee *);
int uf_tee_invoke_private(struct uf_tee *, struct uf_ref, struct uf_ref, const struct uf_patch *,
                          size_t, size_t private_offset, struct uf_tee_result *);
#endif
