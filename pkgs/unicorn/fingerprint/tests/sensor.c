/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "sensor.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    int events[2], cancel[2];
    assert(!pipe(events) && !pipe(cancel));
    assert(!fcntl(events[0], F_SETFL, O_NONBLOCK));
    struct uf_sensor sensor = {.fd = events[0]};
    struct uf_irq event, wire = {41, 123456789};
    assert(uf_sensor_wait(&sensor, cancel[0], 0, &event) == -1 && errno == ETIMEDOUT);
    assert(write(events[1], &wire, sizeof(wire)) == sizeof(wire));
    assert(!uf_sensor_wait(&sensor, cancel[0], 100, &event));
    assert(event.sequence == 41 && event.timestamp_ns == 123456789);
    wire.sequence = 47; /* Merged IRQ edges yield one notification. */
    assert(write(events[1], &wire, sizeof(wire)) == sizeof(wire));
    assert(!uf_sensor_wait(&sensor, -1, 100, &event) && event.sequence == 47);
    assert(write(events[1], &wire, sizeof(wire)) == sizeof(wire));
    assert(uf_sensor_wait(&sensor, -1, 100, &event) == -1 && errno == EPROTO);
    wire.sequence++;
    assert(write(events[1], &wire, sizeof(wire)) == sizeof(wire));
    assert(write(cancel[1], "x", 1) == 1);
    assert(uf_sensor_wait(&sensor, cancel[0], 100, &event) == -1 && errno == ECANCELED);
    char ch;
    assert(read(cancel[0], &ch, 1) == 1); /* Caller still owns cancellation. */
    assert(!uf_sensor_wait(&sensor, cancel[0], 100, &event));
    assert(write(events[1], "x", 1) == 1);
    assert(uf_sensor_wait(&sensor, -1, 100, &event) == -1 && errno == EPROTO);
    close(events[1]);
    assert(uf_sensor_wait(&sensor, -1, 100, &event) == -1 && errno == ENODEV);
    assert(!uf_sensor_close(&sensor) && sensor.fd == -1);
    assert(!uf_sensor_close(&sensor));
    close(cancel[0]);
    close(cancel[1]);
    puts("IRQ wait: merged edges, cancellation priority, deadline, short reads, device loss and "
         "close passed");
    return 0;
}
