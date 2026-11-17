/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "touch.h"
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/input.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
static int snapshot(struct uf_touch *t)
{
    struct input_absinfo current;
    if (ioctl(t->fd, EVIOCGABS(ABS_MT_SLOT), &current))
        return -1;
    if (current.minimum != 0 || current.maximum < 0 || current.maximum >= 16 || current.value < 0 ||
        current.value > current.maximum) {
        errno = EPROTO;
        return -1;
    }
    t->slot = current.value;
    const int codes[] = {ABS_MT_TRACKING_ID, ABS_MT_POSITION_X, ABS_MT_POSITION_Y};
    for (unsigned c = 0; c < 3; c++) {
        int values[17];
        for (unsigned i = 0; i < 17; i++)
            values[i] = c == 0 ? -1 : 0;
        values[0] = codes[c];
        if (ioctl(t->fd, EVIOCGMTSLOTS(sizeof(values)), values))
            return -1;
        for (unsigned i = 0; i < 16; i++) {
            if (c == 0)
                t->slots[i].id = values[i + 1];
            if (c == 1)
                t->slots[i].x = values[i + 1];
            if (c == 2)
                t->slots[i].y = values[i + 1];
        }
    }
    return 0;
}
static int update(struct uf_touch *t)
{
    bool down = false;
    for (unsigned i = 0; i < 16; i++) {
        if (t->slots[i].id < 0)
            continue;
        int x = t->slots[i].x, y = t->slots[i].y;
        if (x < 0 || x > t->maximum_x || y < 0 || y > t->maximum_y)
            continue;
        x = (int)((long long)x * 1440 / (t->maximum_x + 1));
        y = (int)((long long)y * 3200 / (t->maximum_y + 1));
        if (x >= 586 && x < 853 && y >= 2346 && y < 2613) {
            /* Multi-touch in the capture region is rejected as a transition
             * to up. No coordinates from unrelated touches leave this module. */
            if (down) {
                down = false;
                break;
            }
            down = true;
            t->x = x;
            t->y = y;
        }
    }
    int changed = down != t->down;
    t->down = down;
    return changed;
}
int uf_touch_open(struct uf_touch *t)
{
    if (!t || t->fd != -1) {
        errno = EINVAL;
        return -1;
    }
    glob_t paths = {0};
    if (glob("/dev/input/event*", 0, NULL, &paths)) {
        globfree(&paths);
        errno = ENODEV;
        return -1;
    }
    for (size_t i = 0; i < paths.gl_pathc; i++) {
        int fd = open(paths.gl_pathv[i], O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        char name[128] = {0};
        struct input_absinfo x, y, slot;
        if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) > 0 &&
            !strcmp(name, "ST FTS Xiaomi 12S Pro touchscreen") &&
            !ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &x) &&
            !ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &y) &&
            !ioctl(fd, EVIOCGABS(ABS_MT_SLOT), &slot) && x.minimum == 0 && x.maximum == 14399 &&
            y.minimum == 0 && y.maximum == 31999 && slot.minimum == 0 && slot.maximum >= 0 &&
            slot.maximum < 16 && slot.value >= 0 && slot.value <= slot.maximum) {
            t->fd = fd;
            t->maximum_x = x.maximum;
            t->maximum_y = y.maximum;
            for (unsigned j = 0; j < 16; j++)
                t->slots[j].id = -1;
            if (snapshot(t)) {
                close(fd);
                t->fd = -1;
                continue;
            }
            update(t);
            break;
        }
        close(fd);
    }
    globfree(&paths);
    if (t->fd < 0) {
        errno = ENODEV;
        return -1;
    }
    return 0;
}
int uf_touch_read(struct uf_touch *t)
{
    if (!t || t->fd < 0) {
        errno = EINVAL;
        return -1;
    }
    struct input_event e;
    for (;;) {
        ssize_t n = read(t->fd, &e, sizeof(e));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN)
                return 0;
            return -1;
        }
        if (n != sizeof(e)) {
            errno = ENODEV;
            return -1;
        }
        if (e.type == EV_SYN && e.code == SYN_DROPPED) {
            t->dropped = true;
            continue;
        }
        if (e.type == EV_SYN && e.code == SYN_REPORT) {
            if (t->dropped) {
                if (snapshot(t))
                    return -1;
                t->dropped = false;
            }
            int changed = update(t);
            if (changed)
                return 1;
            continue;
        }
        if (t->dropped || e.type != EV_ABS)
            continue;
        if (e.code == ABS_MT_SLOT) {
            if (e.value < 0 || e.value >= 16) {
                errno = EPROTO;
                return -1;
            }
            t->slot = e.value;
        } else if (e.code == ABS_MT_TRACKING_ID)
            t->slots[t->slot].id = e.value;
        else if (e.code == ABS_MT_POSITION_X)
            t->slots[t->slot].x = e.value;
        else if (e.code == ABS_MT_POSITION_Y)
            t->slots[t->slot].y = e.value;
    }
}
int uf_touch_close(struct uf_touch *t)
{
    if (!t) {
        errno = EINVAL;
        return -1;
    }
    int rc = t->fd < 0 ? 0 : close(t->fd);
    *t = (struct uf_touch)UF_TOUCH_CLOSED;
    return rc;
}
