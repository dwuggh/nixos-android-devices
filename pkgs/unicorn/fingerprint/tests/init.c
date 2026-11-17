/* SPDX-License-Identifier: MIT
 * Real engine + DSP + callback dispatcher. Only TEE/FastRPC/device boundaries
 * are replaced. Sensor init cannot finish until DSP has dispatched type 10;
 * DSP cannot finish until TA algorithm init produces its notification. */
#include "../engine.c"
#include "rpc-abi.h"
#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <sys/eventfd.h>
extern int arm_proxy_skel_invoke(uint32_t, remote_arg *);
static pthread_mutex_t boundary = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static bool type_sent, rpc_closed, rpc_started;
static unsigned failure, allocations, high_requests;
static enum uf_profile profile;
int uf_authorization_request(uint64_t challenge, int cancel, unsigned char hat[69])
{
    (void)challenge; (void)cancel; (void)hat;
    assert(0); /* Initialization must not request enrollment authorization. */
    return -1;
}
void uf_wipe(void *p, size_t n) { memset(p, 0, n); }
int uf_tee_open(struct uf_tee *t, const char *name, struct uf_tee_result *r)
{
    *r = (struct uf_tee_result){0};
    t->fd = eventfd(0, EFD_CLOEXEC);
    t->session = !strcmp(name, "keymaster64") ? 1 : 2;
    return t->fd < 0 ? -1 : 0;
}
int uf_tee_close(struct uf_tee *t)
{
    if (t->fd >= 0) close(t->fd);
    t->fd = -1;
    return 0;
}
int uf_tee_alloc(struct uf_tee *t, struct uf_buffer *b, size_t n)
{
    (void)t;
    b->data = calloc(1, n); b->size = n; allocations++;
    return b->data ? 0 : -1;
}
void uf_tee_free(struct uf_buffer *b)
{
    if (b->data) {
        assert(!rpc_started || rpc_closed);
        allocations--; free(b->data); b->data = NULL;
    }
}
int uf_tee_private_alloc(struct uf_tee *t)
{
    (void)t;
    return eventfd(0, EFD_CLOEXEC);
}
int uf_tee_invoke(struct uf_tee *t, struct uf_ref q, struct uf_ref r,
                  const struct uf_patch *p, size_t count, struct uf_tee_result *result)
{
    *result = (struct uf_tee_result){0};
    if (t->session == 1) {
        assert(!count && r.offset == 64);
        memset((char *)r.buffer->data + r.offset, 0, r.length);
        return 0;
    }
    assert(count == 1);
    unsigned char *b = p[0].memory.buffer->data;
    unsigned module = get(b, 4), command = get(b, 8);
    put(q.buffer->data, 28, 0);
    put(b, 12, 0);
    if (failure == 5 && module == 1000 && command == 4)
        put(b, 12, 1000); /* Qualification must include successful cleanup. */
    if (module == 1002 && command == 1) {
        put(b, 28, failure == 1 ? 0 : 3);
        put(b, 32, 3);
    }
    if (module == 1002 && command == 2) {
        pthread_mutex_lock(&boundary);
        while (!type_sent) pthread_cond_wait(&condition, &boundary);
        pthread_mutex_unlock(&boundary);
        if (failure == 2) { put(b, 12, 1000); return 0; }
        size_t config = profile == UF_PROFILE_GFUNI ? 416 : 440;
        size_t extension = profile == UF_PROFILE_GFUNI ? 600 : 624;
        assert(p[0].memory.length == extension + 16);
        put(b, 40, 2); /* Config enroll stages. */
        put(b, 32 + config, 0x10001);
        put(b, 36 + config, failure == 4 ? 0 : 0x13);
        put(b, 40 + config, 1);
        put(b, extension - 116, 18);
        memset(b + extension - 136, 0x35, 18);
    }
    if (module == 1001 && command == 0) {
        assert(type_sent);
        unsigned char callback[12] = {0};
        uf_wire_put32(callback, sizeof(callback), 0, 10);
        remote_arg a = {.buf = {callback, sizeof(callback)}};
        assert(!arm_proxy_skel_invoke(0x10000, &a));
        if (failure == 3) put(b, 12, 1000);
    }
    return 0;
}
int uf_tee_invoke_private(struct uf_tee *t, struct uf_ref q, struct uf_ref r,
                          const struct uf_patch *p, size_t n, size_t off, struct uf_tee_result *x)
{
    assert(off == 32);
    return uf_tee_invoke(t, q, r, p, n, x);
}
int __wrap_ioctl(int fd, unsigned long request, ...)
{
    assert(fd >= 0 && (request == UF_RESET || request == UF_ENABLE_IRQ));
    return 0;
}
int uf_sensor_open(struct uf_sensor *s) { s->fd = eventfd(0, EFD_CLOEXEC); return s->fd < 0 ? -1 : 0; }
int uf_sensor_close(struct uf_sensor *s) { if (s->fd >= 0) close(s->fd); s->fd = -1; return 0; }
int uf_sensor_wait(struct uf_sensor *s, int c, int t, struct uf_irq *i)
{ (void)s; (void)c; (void)t; (void)i; assert(0); return -1; }
int uf_touch_open(struct uf_touch *t) { (void)t; assert(0); return -1; }
int uf_touch_close(struct uf_touch *t) { (void)t; return 0; }
int uf_touch_read(struct uf_touch *t) { (void)t; assert(0); return -1; }
int uf_display_open(struct uf_display *d, int c) { (void)d; (void)c; assert(0); return -1; }
int uf_display_begin(struct uf_display *d) { (void)d; assert(0); return -1; }
int uf_display_ready(struct uf_display *d) { (void)d; assert(0); return -1; }
int uf_display_end(struct uf_display *d) { (void)d; return 0; }
void uf_display_close(struct uf_display *d) { (void)d; }
static int remote_open(const char *uri, uint64_t *h)
{ assert(strstr(uri, "cdsp")); *h = 42; rpc_started = true; return 0; }
static int remote_close(uint64_t h) { assert(h == 42); rpc_closed = true; return 0; }
static int remote_register(int fd, uint32_t size, uint32_t attr)
{ assert(fcntl(fd, F_GETFD) >= 0 && size == 4194304 && attr == 0x10); return 0; }
static int remote_invoke(uint64_t h, uint32_t scalar, remote_arg *a)
{
    assert(h == 42);
    unsigned method = scalar >> 24;
    if (method == 4) {
        pthread_mutex_lock(&boundary);
        type_sent = true;
        pthread_cond_signal(&condition);
        pthread_mutex_unlock(&boundary);
    }
    if (method == 8) high_requests++;
    if (method == 3 && get(a[0].buf.pv, 0) == 11) { assert(high_requests); high_requests--; }
    return 0;
}
void *__wrap_dlopen(const char *s, int f) { (void)s; (void)f; return (void *)1; }
int __wrap_dlclose(void *p) { assert(p && rpc_closed); return 0; }
void *__wrap_dlsym(void *p, const char *s)
{
    assert(p);
    if (!strcmp(s, "remote_handle64_open")) return (void *)remote_open;
    if (!strcmp(s, "remote_handle64_close")) return (void *)remote_close;
    if (!strcmp(s, "remote_handle64_invoke")) return (void *)remote_invoke;
    if (!strcmp(s, "remote_register_dma_handle_attr")) return (void *)remote_register;
    assert(0); return NULL;
}
int __wrap_sd_id128_get_machine_app_specific(sd_id128_t app, sd_id128_t *ret)
{ *ret = app; return 0; }
int main(void)
{
    alarm(15); /* A serial init implementation deadlocks this boundary fixture. */
    for (profile = UF_PROFILE_GFUNI; profile <= UF_PROFILE_GFUNI6; profile++) {
        for (failure = 0; failure <= 5; failure++) {
            type_sent = rpc_closed = rpc_started = false;
            high_requests = 0;
            const char *name = profile == UF_PROFILE_GFUNI ? "gfuni" : "gfuni6";
            int rc = uf_qualify(name);
            assert((rc == 0) == (failure == 0));
            assert(!allocations);
            assert(!rpc_started || rpc_closed);
        }
    }
    puts("initialization: real TA engine, DSP thread and callback; both profiles, calibration and failures released");
}
