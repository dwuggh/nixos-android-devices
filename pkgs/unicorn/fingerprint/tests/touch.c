/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "touch.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
static int recover_down;
static int recover_slot;
int __wrap_ioctl(int fd, unsigned long request, ...)
{
    (void)fd;
    va_list ap;
    va_start(ap, request);
    void *argument = va_arg(ap, void *);
    va_end(ap);
    if (request == EVIOCGABS(ABS_MT_SLOT)) {
        *(struct input_absinfo *)argument =
            (struct input_absinfo){.value = recover_slot, .maximum = 15};
        return 0;
    }
    assert(_IOC_TYPE(request) == 'E' && _IOC_NR(request) == 0x0a);
    int *values = argument;
    unsigned code = values[0];
    for (unsigned i = 1; i < 17; i++)
        values[i] = code == ABS_MT_TRACKING_ID ? -1 : 0;
    if (recover_down) {
        if (code == ABS_MT_TRACKING_ID)
            values[1] = 5;
        if (code == ABS_MT_POSITION_X)
            values[1] = 7190;
        if (code == ABS_MT_POSITION_Y)
            values[1] = 24790;
    }
    return 0;
}
static void event(int fd, unsigned type, unsigned code, int value)
{
    struct input_event e = {.type = type, .code = code, .value = value};
    assert(write(fd, &e, sizeof(e)) == sizeof(e));
}
int main(void)
{
    int p[2];
    assert(!pipe2(p, O_CLOEXEC | O_NONBLOCK));
    struct uf_touch t = UF_TOUCH_CLOSED;
    t.fd = p[0];
    t.maximum_x = 14399;
    t.maximum_y = 31999;
    for (unsigned i = 0; i < 16; i++)
        t.slots[i].id = -1;
    event(p[1], EV_ABS, ABS_MT_SLOT, 0);
    event(p[1], EV_ABS, ABS_MT_TRACKING_ID, 1);
    event(p[1], EV_ABS, ABS_MT_POSITION_X, 7190);
    event(p[1], EV_ABS, ABS_MT_POSITION_Y, 24790);
    event(p[1], EV_SYN, SYN_REPORT, 0);
    assert(uf_touch_read(&t) == 1 && t.down && t.x == 719 && t.y == 2479);
    event(p[1], EV_ABS, ABS_MT_POSITION_X, 8000);
    event(p[1], EV_SYN, SYN_REPORT, 0);
    assert(uf_touch_read(&t) == 0 && t.down);
    event(p[1], EV_ABS, ABS_MT_SLOT, 1);
    event(p[1], EV_ABS, ABS_MT_TRACKING_ID, 2);
    event(p[1], EV_ABS, ABS_MT_POSITION_X, 7190);
    event(p[1], EV_ABS, ABS_MT_POSITION_Y, 24790);
    event(p[1], EV_SYN, SYN_REPORT, 0);
    assert(uf_touch_read(&t) == 1 && !t.down); /* Multiple fingers cannot start exposure. */
    recover_down = 1;
    recover_slot = 3;
    event(p[1], EV_SYN, SYN_DROPPED, 0);
    event(p[1], EV_ABS, ABS_MT_POSITION_X, 0);
    event(p[1], EV_SYN, SYN_REPORT, 0);
    assert(uf_touch_read(&t) == 1 && t.down && t.x == 719 && t.slot == 3);
    recover_down = 0;
    event(p[1], EV_SYN, SYN_DROPPED, 0);
    event(p[1], EV_SYN, SYN_REPORT, 0);
    assert(uf_touch_read(&t) == 1 && !t.down);
    event(p[1], EV_ABS, ABS_MT_SLOT, 16);
    assert(uf_touch_read(&t) == -1 && errno == EPROTO);
    assert(!uf_touch_close(&t) && !uf_touch_close(&t));
    close(p[1]);
    puts("touch: native coordinate scaling, region transitions, multi-touch rejection, SYN_DROPPED "
         "recovery, bounds and close passed");
}
