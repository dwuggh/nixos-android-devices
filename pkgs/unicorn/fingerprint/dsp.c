/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "dsp.h"
#include "protocol.h"
#include "rpc-abi.h"
#include <dlfcn.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/eventfd.h>
#include <time.h>
#include <unistd.h>
#ifndef UF_FASTRPC_LIBRARY
#define UF_FASTRPC_LIBRARY "libadsprpc.so"
#endif
struct uf_dsp {
    void *library;
    remote_handle64 handle;
    int event, expected, frequency;
    int32_t result[14];
    uint32_t pending_mask, received_mask;
    bool opened, initialized, started;
    pthread_t thread;
    int ready_event, stop_event, dma, cancel, startup_error;
    uint32_t chip;
    enum { STARTING, READY, FAILED, CLOSING } state;
    atomic_bool broken; /* Also written by the FastRPC callback thread. */
    int (*open)(const char *, remote_handle64 *);
    int (*invoke)(remote_handle64, uint32_t, remote_arg *);
    int (*close)(remote_handle64);
    int (*register_dma)(int, uint32_t, uint32_t);
};
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct uf_dsp *active;

static int64_t now_ms(void);

static int rpc(struct uf_dsp *d, unsigned method, const uint32_t *words, size_t count, int fd)
{
    unsigned char data[16] = {0};
    remote_arg args[2] = {0};
    if (count > 4 || d->broken) {
        errno = EIO;
        return -1;
    }
    for (size_t i = 0; i < count; i++)
        uf_wire_put32(data, sizeof(data), 4 * i, words[i]);
    args[0].buf.pv = data;
    args[0].buf.nLen = count * 4;
    args[1].dma.fd = fd;
    uint32_t scalar = method << 24 | (count ? 0x10000 : 0) | (fd >= 0 ? 0x10 : 0);
    int64_t start = now_ms();
    int status = d->invoke(d->handle, scalar, count ? args : NULL);
    fprintf(stderr, "event=dsp_rpc method=%u command=%u status=%d elapsed_ms=%lld\n",
            method, count ? words[0] : 0, status, (long long)(now_ms() - start));
    if (status) {
        d->broken = true;
        errno = EREMOTEIO;
        return -1;
    }
    return 0;
}
static int prepare(struct uf_dsp *d, unsigned expected)
{
    pthread_mutex_lock(&lock);
    if ((d->pending_mask & (1U << expected)) || d->broken) {
        pthread_mutex_unlock(&lock);
        errno = EBUSY;
        return -1;
    }
    uint64_t v;
    while (read(d->event, &v, sizeof(v)) == sizeof(v)) {
    }
    d->pending_mask |= 1U << expected;
    d->received_mask &= ~(1U << expected);
    d->result[expected] = 0;
    pthread_mutex_unlock(&lock);
    return 0;
}
int uf_dsp_notify(uint32_t command, int32_t result)
{
    pthread_mutex_lock(&lock);
    fprintf(stderr, "event=dsp_notification command=%u result=%d pending=%u\n",
            command, result, active && command < 14 &&
            !!(active->pending_mask & (1U << command)));
    if (active && !active->broken && command < 14 &&
        (active->pending_mask & (1U << command)) &&
        !(active->received_mask & (1U << command))) {
        active->result[command] = result;
        active->received_mask |= 1U << command;
        uint64_t one = 1;
        /* Nonblocking eventfd; no TA data, addresses or DSP strings are logged. */
        if (write(active->event, &one, sizeof(one)) != sizeof(one))
            active->broken = true;
    }
    pthread_mutex_unlock(&lock);
    return 0;
}
static int64_t now_ms(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t))
        return -1;
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static int wait_command(struct uf_dsp *d, unsigned expected, int cancel, int timeout)
{
    if (!d || !(d->pending_mask & (1U << expected)) || timeout <= 0 || timeout > 10000) {
        errno = EINVAL;
        return -1;
    }
    int64_t end = now_ms() + timeout;
    int saved = 0;
    struct pollfd p[3] = {{d->event, POLLIN, 0}, {cancel, POLLIN, 0},
                          {d->stop_event, POLLIN, 0}};
    for (;;) {
        pthread_mutex_lock(&lock);
        bool received = (d->received_mask & (1U << expected)) != 0;
        pthread_mutex_unlock(&lock);
        if (d->broken) {
            saved = EIO;
            break;
        }
        if (received) {
            struct pollfd cancellation[2] = {{cancel, POLLIN, 0}, {d->stop_event, POLLIN, 0}};
            if (poll(cancellation, 2, 0) > 0)
                saved = ECANCELED;
            break;
        }
        int64_t left = end - now_ms();
        if (left <= 0) {
            saved = ETIMEDOUT;
            break;
        }
        int rc = poll(p, 3, left);
        if (rc < 0 && errno == EINTR)
            continue;
        if (rc < 0) {
            saved = errno;
            break;
        }
        if (p[1].revents || p[2].revents) {
            saved = ECANCELED;
            break;
        }
        if (p[0].revents & POLLIN) {
            uint64_t value;
            if (read(d->event, &value, sizeof(value)) < 0 && errno != EAGAIN) {
                saved = errno;
                break;
            }
            continue;
        }
        if (p[0].revents) {
            saved = EIO;
            break;
        }
    }
    pthread_mutex_lock(&lock);
    if (!saved && (d->result[expected] || d->broken))
        saved = EREMOTEIO;
    d->pending_mask &= ~(1U << expected);
    d->received_mask &= ~(1U << expected);
    if (saved)
        d->broken = true; /* Late replies cannot satisfy a new request. */
    pthread_mutex_unlock(&lock);
    fprintf(stderr, "event=dsp_wait expected=%u errno=%d elapsed_ms=%lld\n",
            expected, saved, (long long)(now_ms() - end + timeout));
    if (saved) {
        errno = saved;
        return -1;
    }
    return 0;
}
int uf_dsp_begin(struct uf_dsp *d, unsigned command)
{
    /* OEM HAL commands 1,2,3,6 correspond to proxy 2,3,4,13. */
    unsigned proxy;
    switch (command) {
    case 1:
        proxy = 2;
        break;
    case 2:
        proxy = 3;
        break;
    case 3:
        proxy = 4;
        break;
    case 6:
        proxy = 13;
        break;
    default:
        errno = EINVAL;
        return -1;
    }
    if (!d || prepare(d, proxy))
        return -1;
    d->expected = proxy;
    uint32_t word = proxy;
    return rpc(d, 3, &word, 1, -1);
}
int uf_dsp_speed(struct uf_dsp *d, bool high, int cancel)
{
    if (!d) {
        errno = EINVAL;
        return -1;
    }
    if (d->broken) {
        errno = EIO;
        return -1;
    }
    if (d->frequency == (int)high)
        return 0;
    struct pollfd p[2] = {{cancel, POLLIN, 0}, {d->stop_event, POLLIN, 0}};
    if (poll(p, 2, 0) > 0) {
        d->broken = true;
        errno = ECANCELED;
        return -1;
    }
    const uint32_t clocks[] = {0, 1000, 0, 69};
    if (rpc(d, high ? 8 : 7, high ? clocks : NULL, high ? 4 : 0, -1))
        return -1;
    /* OEM proxy 0x194c: clocks then command 11; both complete synchronously.
     * Notification 11 is consumed by the callback without arming a waiter. */
    uint32_t command = 11;
    if (high && rpc(d, 3, &command, 1, -1))
        return -1;
    d->frequency = high;
    return 0;
}
static void *initialize(void *data)
{
    struct uf_dsp *d = data;
    int fd = d->dma, cancel = d->cancel;
    uint32_t chip = d->chip;
    d->library = dlopen(UF_FASTRPC_LIBRARY, RTLD_NOW | RTLD_LOCAL);
    if (!d->library) {
        errno = ENOENT;
        goto fail;
    }
#define LOAD(field, name)                                                                          \
    do {                                                                                           \
        *(void **)(&d->field) = dlsym(d->library, name);                                           \
        if (!d->field) {                                                                           \
            errno = ENOSYS;                                                                        \
            goto fail;                                                                             \
        }                                                                                          \
    } while (0)
    LOAD(open, "remote_handle64_open");
    LOAD(invoke, "remote_handle64_invoke");
    LOAD(close, "remote_handle64_close");
    LOAD(register_dma, "remote_register_dma_handle_attr");
#undef LOAD
    if (d->open("file:///libproxy_skel.so?proxy_skel_handle_invoke&_modver=1.0&_dom=cdsp",
                &d->handle)) {
        errno = EREMOTEIO;
        goto fail;
    }
    d->opened = true;
    if (rpc(d, 2, NULL, 0, -1))
        goto fail;
    d->initialized = true;
    uint32_t word = 9; /* Disable firmware log dumping. */
    if (rpc(d, 3, &word, 1, -1))
        goto fail;
    if (d->register_dma(fd, 4U * 1024 * 1024, 0x10)) {
        errno = EREMOTEIO;
        goto fail;
    }
    word = 6;
    if (rpc(d, 5, &word, 1, fd))
        goto fail;
    uint32_t type[] = {10, chip};
    if (prepare(d, 10) || rpc(d, 4, type, 2, -1) || wait_command(d, 10, cancel, 3000))
        goto fail;
    if (uf_dsp_speed(d, true, cancel))
        goto fail;
    word = 5;
    if (rpc(d, 3, &word, 1, -1) || uf_dsp_speed(d, false, cancel))
        goto fail;
    pthread_mutex_lock(&lock);
    d->state = READY;
    pthread_mutex_unlock(&lock);
    goto done;
fail:
    pthread_mutex_lock(&lock);
    d->startup_error = errno ? errno : EIO;
    d->state = FAILED;
    d->broken = true;
    pthread_mutex_unlock(&lock);
done:;
    uint64_t one = 1;
    if (write(d->ready_event, &one, sizeof(one)) != sizeof(one))
        d->broken = true;
    return NULL;
}
struct uf_dsp *uf_dsp_start(int fd, uint32_t chip, int cancel)
{
    struct uf_dsp *d = calloc(1, sizeof(*d));
    if (!d)
        return NULL;
    atomic_init(&d->broken, false);
    d->frequency = -1;
    d->event = d->ready_event = d->stop_event = -1;
    d->dma = fd;
    d->chip = chip;
    d->cancel = cancel;
    d->state = STARTING;
    d->event = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    d->ready_event = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    d->stop_event = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (d->event < 0 || d->ready_event < 0 || d->stop_event < 0)
        goto fail;
    pthread_mutex_lock(&lock);
    if (active) {
        pthread_mutex_unlock(&lock);
        errno = EBUSY;
        goto fail;
    }
    active = d;
    pthread_mutex_unlock(&lock);
    int err = pthread_create(&d->thread, NULL, initialize, d);
    if (err) {
        errno = err;
        goto fail;
    }
    d->started = true;
    return d;
fail:;
    int saved = errno;
    uf_dsp_close(d);
    errno = saved;
    return NULL;
}
int uf_dsp_ready(struct uf_dsp *d, int cancel, int timeout)
{
    if (!d || timeout <= 0 || timeout > 10000) {
        errno = EINVAL;
        return -1;
    }
    int64_t end = now_ms() + timeout;
    struct pollfd p[3] = {{d->ready_event, POLLIN, 0}, {cancel, POLLIN, 0},
                          {d->stop_event, POLLIN, 0}};
    int saved = 0;
    for (;;) {
        int64_t left = end - now_ms();
        int rc = poll(p, 3, left > 0 ? left : 0);
        if (rc < 0 && errno == EINTR)
            continue;
        if (rc < 0 || !rc || p[1].revents || p[2].revents) {
            saved = rc < 0 ? errno : !rc ? ETIMEDOUT : ECANCELED;
            break;
        }
        pthread_mutex_lock(&lock);
        saved = d->state == READY && !d->broken ? 0 :
                d->startup_error ? d->startup_error : EIO;
        pthread_mutex_unlock(&lock);
        break;
    }
    if (saved) {
        d->broken = true;
        uint64_t one = 1;
        if (write(d->stop_event, &one, sizeof(one)) != sizeof(one))
            d->broken = true;
        errno = saved;
        return -1;
    }
    return 0;
}
int uf_dsp_close(struct uf_dsp *d)
{
    if (!d)
        return 0;
    int rc = 0;
    uint64_t one = 1;
    if (d->stop_event >= 0)
        if (write(d->stop_event, &one, sizeof(one)) != sizeof(one))
            d->broken = true;
    /* Never detach a thread using this object or DMA. A stuck kernel RPC is
     * bounded by the owning worker/selector process watchdog, not by freeing
     * memory under a live thread. */
    if (d->started)
        pthread_join(d->thread, NULL);
    pthread_mutex_lock(&lock);
    d->state = CLOSING;
    pthread_mutex_unlock(&lock);
    /* Closing the remote handle tears down its mappings even after an error.
     * A fresh process is used per libfprint open, avoiding stale RPC globals. */
    if (d->opened) {
        if (d->initialized && !d->broken)
            rc = rpc(d, 6, NULL, 0, -1);
        if (d->close(d->handle))
            rc = -1;
    }
    pthread_mutex_lock(&lock);
    if (active == d)
        active = NULL;
    pthread_mutex_unlock(&lock);
    if (d->event >= 0)
        close(d->event);
    if (d->ready_event >= 0)
        close(d->ready_event);
    if (d->stop_event >= 0)
        close(d->stop_event);
    if (d->library)
        dlclose(d->library);
    fprintf(stderr, "event=dsp_release result=%d\n", rc);
    free(d);
    return rc;
}

int uf_dsp_wait(struct uf_dsp *d, int cancel, int timeout)
{
    if (!d) {
        errno = EINVAL;
        return -1;
    }
    return wait_command(d, d->expected, cancel, timeout);
}
