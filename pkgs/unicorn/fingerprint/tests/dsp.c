/* SPDX-License-Identifier: MIT */
#include "dsp.h"
#include "protocol.h"
#include "rpc-abi.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
extern int arm_proxy_skel_invoke(uint32_t, remote_arg *);
static unsigned calls, registration, pending_command, closes, unloads, high_requests;
static unsigned fail_method = 99;
static bool delayed_type, type_sent;
static pthread_mutex_t boundary = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static void notify(unsigned command, int result)
{
    unsigned char data[12] = {0}; /* OEM accepts trailing bytes. */
    uf_wire_put32(data, sizeof(data), 0, command);
    uf_wire_put32(data, sizeof(data), 4, result);
    remote_arg a = {.buf = {data, sizeof(data)}};
    assert(!arm_proxy_skel_invoke(0x10000, &a));
}
static int mock_open(const char *uri, uint64_t *h)
{
    assert(strstr(uri, "&_dom=cdsp"));
    *h = 42;
    return 0;
}
static int mock_close(uint64_t h)
{
    assert(h == 42);
    closes++;
    notify(10, 0); /* Callback while tearing down must not resurrect a session. */
    return 0;
}
static int mock_register(int fd, uint32_t size, uint32_t attr)
{
    assert(fd == 19 && size == 4194304 && attr == 0x10);
    registration++;
    return 0;
}
static int mock_invoke(uint64_t h, uint32_t sc, remote_arg *a)
{
    assert(h == 42);
    calls++;
    uint32_t m = sc >> 24, x = 0;
    if (a) {
        assert(a[0].buf.nLen <= 16);
        uf_wire_get32(a[0].buf.pv, a[0].buf.nLen, 0, &x);
    }
    if (m == fail_method)
        return 1;
    switch (m) {
    case 2:
    case 6:
        assert((sc & 0xffffff) == 0 && !a);
        break;
    case 3:
        assert((sc & 0xffffff) == 0x10000 && a[0].buf.nLen == 4);
        if (x == 13 || x == 2 || x == 3 || x == 4)
            pending_command = x;
        if (x == 11) {
            assert(high_requests);
            high_requests--;
            /* Deliberately no clock completion notification. */
        }
        break;
    case 4:
        assert(a[0].buf.nLen == 8 && x == 10);
        pthread_mutex_lock(&boundary);
        type_sent = true;
        pthread_cond_signal(&condition);
        bool delay = delayed_type;
        pthread_mutex_unlock(&boundary);
        if (!delay)
            notify(10, 0);
        break;
    case 5:
        assert((sc & 0xffffff) == 0x10010 && a[1].dma.fd == 19 && a[1].dma.offset == 0 &&
               registration == 1 && x == 6);
        break;
    case 7:
        assert(!a); /* OEM returns synchronously; no notification 7. */
        break;
    case 8:
        assert(a[0].buf.nLen == 16 && x == 0);
        high_requests++;
        break;
    default:
        assert(0);
    }
    return 0;
}
void *__wrap_dlopen(const char *s, int f)
{
    (void)s; (void)f;
    return (void *)1;
}
int __wrap_dlclose(void *p)
{
    assert(p == (void *)1);
    unloads++;
    return 0;
}
void *__wrap_dlsym(void *p, const char *s)
{
    assert(p == (void *)1);
    if (!strcmp(s, "remote_handle64_open")) return (void *)mock_open;
    if (!strcmp(s, "remote_handle64_close")) return (void *)mock_close;
    if (!strcmp(s, "remote_handle64_invoke")) return (void *)mock_invoke;
    if (!strcmp(s, "remote_register_dma_handle_attr")) return (void *)mock_register;
    assert(0);
    return NULL;
}
static struct uf_dsp *start(bool delayed)
{
    registration = calls = high_requests = 0;
    delayed_type = delayed;
    type_sent = false;
    struct uf_dsp *d = uf_dsp_start(19, 3, -1);
    assert(d);
    return d;
}
static void await_type(void)
{
    pthread_mutex_lock(&boundary);
    while (!type_sent)
        pthread_cond_wait(&condition, &boundary);
    pthread_mutex_unlock(&boundary);
}
int main(void)
{
    unsigned char data[8] = {0};
    remote_arg a[2] = {{.buf = {data, 7}}, {.buf = {NULL, 0}}};
    assert(arm_proxy_skel_invoke(0x10000, a) == 14);
    a[0].buf.nLen = 8;
    assert(arm_proxy_skel_invoke(0x10001, a) == 14);
    assert(!arm_proxy_skel_invoke(0x01020000, a));
    uf_wire_put32(data, 8, 0, 1);
    a[1].buf.nLen = 1;
    assert(arm_proxy_skel_invoke(0x01020000, a) == 14);

    struct uf_dsp *d = start(true);
    await_type(); /* TA main thread must be able to run before this completion. */
    notify(10, 0);
    assert(!uf_dsp_ready(d, -1, 1000) && calls == 8);
    assert(!uf_dsp_speed(d, false, -1) && calls == 8);
    assert(!uf_dsp_speed(d, true, -1) && calls == 10);
    assert(!uf_dsp_speed(d, true, -1) && calls == 10);
    assert(!uf_dsp_speed(d, false, -1) && calls == 11);
    assert(!uf_dsp_begin(d, 6) && pending_command == 13);
    notify(11, 0); /* Optional/late frequency notification is unrelated. */
    notify(2, -1);
    notify(13, 0);
    notify(13, 1); /* First completion wins; duplicates cannot rewrite it. */
    assert(!uf_dsp_wait(d, -1, 100));
    assert(!uf_dsp_begin(d, 1));
    notify(2, 1);
    assert(uf_dsp_wait(d, -1, 100) == -1 && errno == EREMOTEIO);
    assert(uf_dsp_begin(d, 6) == -1);
    assert(!uf_dsp_close(d));
    assert(closes == 1 && unloads == 1);

    d = start(false);
    assert(!uf_dsp_ready(d, -1, 1000));
    assert(!uf_dsp_begin(d, 6));
    assert(uf_dsp_wait(d, -1, 1) == -1 && errno == ETIMEDOUT);
    notify(13, 0);
    assert(uf_dsp_begin(d, 6) == -1); /* Timed-out session cannot be reused. */
    uf_dsp_close(d);

    d = start(true);
    await_type();
    int pipefd[2];
    assert(!pipe(pipefd) && write(pipefd[1], "x", 1) == 1);
    assert(uf_dsp_ready(d, pipefd[0], 100) == -1 && errno == ECANCELED);
    uf_dsp_close(d); /* Stop wakes init's callback wait, then joins before dlclose. */
    close(pipefd[0]); close(pipefd[1]);

    const unsigned failures[] = {2, 3, 4, 5, 7, 8};
    for (unsigned i = 0; i < sizeof(failures) / sizeof(*failures); i++) {
        fail_method = failures[i];
        d = start(false);
        assert(uf_dsp_ready(d, -1, 1000) == -1 && errno == EREMOTEIO);
        uf_dsp_close(d);
    }
    fail_method = 99;
    d = start(false);
    assert(!uf_dsp_ready(d, -1, 1000));
    uf_dsp_close(d);
    assert(closes == 10 && unloads == 10);
    puts("DSP: OEM sync clocks, real callback ABI, concurrent init, failures, cancellation and reopen passed");
}
