/* SPDX-License-Identifier: MIT */
#ifndef UF_TOUCH_H
#define UF_TOUCH_H
#include <stdbool.h>
struct uf_touch {
    int fd, slot, maximum_x, maximum_y;
    struct {
        int id, x, y;
    } slots[16];
    bool down, dropped;
    unsigned x, y;
};
#define UF_TOUCH_CLOSED {.fd = -1}
int uf_touch_open(struct uf_touch *);
int uf_touch_close(struct uf_touch *);
/* Drain a complete evdev frame; return 1 on ROI down/up transition, 0 otherwise.
 * No EVIOCGRAB: normal Wayland touch delivery continues. */
int uf_touch_read(struct uf_touch *);
#endif
