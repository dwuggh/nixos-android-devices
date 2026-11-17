/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "sensor.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>

_Static_assert(sizeof(struct uf_irq) == 16, "kernel fingerprint event ABI");

int uf_sensor_open(struct uf_sensor *sensor)
{
    if (!sensor || sensor->fd != -1) {
        errno = EINVAL;
        return -1;
    }
    sensor->fd = open("/dev/unicorn-fingerprint", O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (sensor->fd < 0)
        return -1;
    sensor->have_sequence = 0;
    /* Caller explicitly enables IRQ and resets only after protocol readiness.
     * Merely opening holds reset asserted; close releases the kernel power lease. */
    return 0;
}

int uf_sensor_close(struct uf_sensor *sensor)
{
    if (!sensor) {
        errno = EINVAL;
        return -1;
    }
    if (sensor->fd < 0)
        return 0;
    int rc = close(sensor->fd);
    *sensor = (struct uf_sensor)UF_SENSOR_CLOSED;
    return rc;
}

static int64_t now_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now))
        return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

int uf_sensor_wait(struct uf_sensor *sensor, int cancel, int timeout, struct uf_irq *event)
{
    if (!sensor || sensor->fd < 0 || !event || timeout < 0 || cancel < -1 || cancel == sensor->fd) {
        errno = EINVAL;
        return -1;
    }
    int64_t now = now_ms();
    if (now < 0)
        return -1;
    int64_t deadline = now + timeout;
    struct pollfd fds[2] = {{sensor->fd, POLLIN, 0}, {cancel, POLLIN, 0}};
    for (;;) {
        int64_t left = deadline - now;
        int wait = left <= 0 ? 0 : left > INT_MAX ? INT_MAX : (int)left;
        int rc = poll(fds, 2, wait);
        if (rc < 0 && errno != EINTR)
            return -1;
        if (rc >= 0) {
            if (fds[1].revents) {
                errno = ECANCELED;
                return -1;
            }
            if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                errno = ENODEV;
                return -1;
            }
            if (fds[0].revents & POLLIN) {
                struct uf_irq next;
                ssize_t n = read(sensor->fd, &next, sizeof(next));
                if (n == sizeof(next)) {
                    /* Sequence jumps represent merged edges. Never fabricate
                     * captures from that count; the TA drain protocol is pending. */
                    if (sensor->have_sequence && next.sequence == sensor->sequence) {
                        errno = EPROTO;
                        return -1;
                    }
                    sensor->sequence = next.sequence;
                    sensor->have_sequence = 1;
                    *event = next;
                    return 0;
                }
                if (n >= 0) {
                    errno = EPROTO;
                    return -1;
                }
                if (errno != EAGAIN && errno != EINTR)
                    return -1;
            }
        }
        now = now_ms();
        if (now < 0)
            return -1;
        if (now >= deadline) {
            errno = ETIMEDOUT;
            return -1;
        }
    }
}
