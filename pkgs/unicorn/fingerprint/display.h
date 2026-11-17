/* SPDX-License-Identifier: MIT */
#ifndef UF_DISPLAY_H
#define UF_DISPLAY_H
#include <stdint.h>
#include <sys/ioctl.h>
struct uf_illumination_info {
    uint32_t version, width, height, x, y, w, h, active;
};
#define UF_ILLUMINATION_INFO _IOR('L', 0, struct uf_illumination_info)
#define UF_ILLUMINATION_ACQUIRE _IOW('L', 1, uint32_t)
#define UF_ILLUMINATION_RELEASE _IO('L', 2)
struct uf_display {
    int illumination, session;
    uint64_t nonce;
};
#define UF_DISPLAY_CLOSED {.illumination = -1, .session = -1}
struct uf_hint_hello {
    uint32_t magic, version, role, uid;
};
struct uf_hint_message {
    uint32_t magic, version;
    uint64_t nonce;
    uint32_t x, y, w, h;
};
#define UF_HINT_MAGIC 0x55464831U
int uf_display_open(struct uf_display *, int cancellation_fd);
int uf_display_begin(struct uf_display *);
int uf_display_ready(struct uf_display *);
int uf_display_end(struct uf_display *);
void uf_display_close(struct uf_display *);
#endif
