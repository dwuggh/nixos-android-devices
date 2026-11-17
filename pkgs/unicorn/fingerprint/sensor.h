/* SPDX-License-Identifier: MIT */
#ifndef UNICORN_FP_SENSOR_H
#define UNICORN_FP_SENSOR_H
#include <stdint.h>
#include <sys/ioctl.h>

#define UF_RESET _IO('F', 0)
#define UF_ENABLE_IRQ _IO('F', 1)
#define UF_DISABLE_IRQ _IO('F', 2)
struct uf_irq {
    uint64_t sequence, timestamp_ns;
};
struct uf_sensor {
    int fd;
    uint64_t sequence;
    int have_sequence;
};
#define UF_SENSOR_CLOSED {.fd = -1}
int uf_sensor_open(struct uf_sensor *);
int uf_sensor_close(struct uf_sensor *);
/* cancellation_fd is a caller-owned eventfd/pipe, or -1. Cancellation wins
 * over simultaneous IRQ delivery. The descriptor is not consumed here.
 * One event is a notification to drain TA IRQ state, NOT one sample per edge. */
int uf_sensor_wait(struct uf_sensor *, int cancellation_fd, int timeout_ms, struct uf_irq *);
#endif
