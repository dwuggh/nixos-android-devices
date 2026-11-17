/* SPDX-License-Identifier: MIT
 * Exercise the real state machine with TA/DSP/display boundary fakes. The
 * synthetic responses test control flow; they are not hardware evidence. */
#include "../engine.c"
#include <assert.h>
static unsigned command_calls[1100], remaining, algorithm_error, authorization_error;
static int provider_error;
static bool light, display_available, save_visible = true;
static unsigned provisional, early_finish;
static bool touch_advance;
static unsigned queued_transitions;
static int dsp_error;
static bool reset_requested;
static struct uf_template match;
static struct uf_message received[32];
static int external_fd[2];
int uf_authorization_request(uint64_t challenge, int cancel, unsigned char hat[69])
{
    assert(challenge == 123 && cancel == external_fd[0]);
    if (provider_error) {
        errno = provider_error;
        return -1;
    }
    memset(hat, 0x55, 69); /* Boundary fixture; not a signed authorization. */
    return 0;
}
void uf_wipe(void *p, size_t n)
{
    memset(p, 0, n);
}
int uf_tee_open(struct uf_tee *t, const char *n, struct uf_tee_result *r)
{
    (void)t;
    (void)n;
    (void)r;
    assert(0);
    return -1;
}
int uf_tee_alloc(struct uf_tee *t, struct uf_buffer *b, size_t n)
{
    (void)t;
    (void)b;
    (void)n;
    assert(0);
    return -1;
}
void uf_tee_free(struct uf_buffer *b)
{
    (void)b;
}
int uf_tee_close(struct uf_tee *t)
{
    (void)t;
    return 0;
}
int uf_tee_private_alloc(struct uf_tee *t)
{
    (void)t;
    assert(0);
    return -1;
}
int uf_tee_invoke(struct uf_tee *t, struct uf_ref q, struct uf_ref r, const struct uf_patch *p,
                  size_t count, struct uf_tee_result *result)
{
    (void)t;
    (void)r;
    (void)result;
    assert(count == 1 && p[0].offset == 0 && p[0].width == 4);
    unsigned char *b = p[0].memory.buffer->data;
    uint32_t module = get(b, 4), command = get(b, 8);
    assert(command < 1100);
    command_calls[command]++;
    put(q.buffer->data, 28, 0);
    put(b, 12, 0);
    if (module == 1000 && command == 2)
        put64(b, 32, 123);
    if (module == 1000 && command == 3) {
        put(b, 12, authorization_error);
        return 0;
    }
    if (module == 1002 && (command == 3 || command == 101))
        assert(light);
    if (module == 1001 && (command == 1 || command == 2)) {
        assert(light || (command == 2 && get(b, 28) == 1));
        put(b, 12, algorithm_error);
        b[0] = reset_requested;
        if (command == 1) {
            put(b, 28, 0);
            put(b, 32, 4711);
            put(b, 40, remaining);
            b[136] = early_finish != 0;
        } else {
            put(b, 44, match.group);
            put(b, 48, match.finger);
            b[169] = provisional && command_calls[2] <= provisional;
        }
    }
    if (module == 1000 && command == 7) {
        put(b, 28, save_visible ? 1 : 0);
        put(b, 32, 0);
        put(b, 72, 4711);
    }
    if (module == 1000 && (command == 9 || command == 12))
        assert(!light);
    return 0;
}
int uf_tee_invoke_private(struct uf_tee *t, struct uf_ref q, struct uf_ref r,
                          const struct uf_patch *p, size_t n, size_t off, struct uf_tee_result *x)
{
    assert(off == 32);
    return uf_tee_invoke(t, q, r, p, n, x);
}
int uf_sensor_open(struct uf_sensor *s)
{
    (void)s;
    assert(0);
    return -1;
}
int uf_sensor_close(struct uf_sensor *s)
{
    (void)s;
    return 0;
}
int uf_sensor_wait(struct uf_sensor *s, int c, int t, struct uf_irq *i)
{
    (void)s;
    (void)c;
    (void)t;
    (void)i;
    assert(0);
    return -1;
}
struct uf_dsp *uf_dsp_start(int fd, uint32_t chip, int cancel)
{
    (void)fd;
    (void)chip;
    (void)cancel;
    assert(0);
    return NULL;
}
int uf_dsp_ready(struct uf_dsp *d, int c, int t)
{
    (void)d; (void)c; (void)t;
    return 0;
}
int uf_dsp_close(struct uf_dsp *d)
{
    (void)d;
    return 0;
}
int uf_dsp_begin(struct uf_dsp *d, unsigned c)
{
    (void)d;
    assert(c == 6);
    return 0;
}
int uf_dsp_wait(struct uf_dsp *d, int c, int t)
{
    (void)d;
    (void)c;
    (void)t;
    if (dsp_error) {
        errno = dsp_error;
        return -1;
    }
    return 0;
}
int uf_dsp_speed(struct uf_dsp *d, bool high, int c)
{
    (void)d;
    (void)high;
    (void)c;
    return 0;
}
int uf_touch_open(struct uf_touch *t)
{
    t->fd = external_fd[0];
    t->down = true;
    t->x = 719;
    t->y = 2479;
    return 0;
}
int uf_touch_close(struct uf_touch *t)
{
    t->fd = -1;
    return 0;
}
int uf_touch_read(struct uf_touch *t)
{
    if (queued_transitions) {
        queued_transitions--;
        t->down = !t->down;
        return 1;
    }
    if (touch_advance) {
        t->down = !t->down;
        touch_advance = false;
        return 1;
    }
    return 0;
}
int __wrap_poll(struct pollfd *p, nfds_t n, int timeout)
{
    (void)timeout;
    assert(n == 3);
    p[0].revents = POLLIN;
    touch_advance = true;
    return 1;
}
int uf_display_open(struct uf_display *d, int c)
{
    (void)c;
    if (!display_available) {
        errno = ENXIO;
        return -1;
    }
    d->session = external_fd[0];
    d->illumination = external_fd[0];
    return 0;
}
int uf_display_begin(struct uf_display *d)
{
    (void)d;
    light = true;
    return 0;
}
int uf_display_ready(struct uf_display *d)
{
    (void)d;
    assert(light);
    return 0;
}
int uf_display_end(struct uf_display *d)
{
    (void)d;
    light = false;
    return 0;
}
void uf_display_close(struct uf_display *d)
{
    light = false;
    *d = (struct uf_display)UF_DISPLAY_CLOSED;
}
static struct engine setup(unsigned char header[64], unsigned char body[1024])
{
    memset(command_calls, 0, sizeof(command_calls));
    algorithm_error = authorization_error = 0;
    provider_error = 0;
    remaining = 0;
    provisional = early_finish = 0;
    touch_advance = false;
    queued_transitions = 0;
    dsp_error = 0;
    reset_requested = false;
    light = false;
    display_available = true;
    save_visible = true;
    match = (struct uf_template){0, 4711};
    struct engine e = empty();
    e.profile = UF_PROFILE_GFUNI6;
    e.stages = 4;
    e.cali = 0x13;
    e.initialized = true;
    e.header = (struct uf_buffer){header, 64, 1};
    e.body = (struct uf_buffer){body, 1024, 2};
    e.channel = external_fd[0];
    e.dma = 19;
    return e;
}
static void drain(void)
{
    while (recv(external_fd[1], received, sizeof(received), MSG_DONTWAIT) > 0) {
    }
}
int main(void)
{
    char identity[80], other[80];
    unsigned char serial[18] = {1};
    format_identity(identity, SD_ID128_NULL, serial, 16);
    format_identity(other, SD_ID128_NULL, serial, 18);
    assert(strlen(identity) == 70 && strcmp(identity, other));
    strcpy(identity, other);
    serial[17] = 2;
    format_identity(other, SD_ID128_NULL, serial, 18);
    assert(strcmp(identity, other));
    assert(!socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, external_fd));
    unsigned char h[64], b[1024];
    struct engine e = setup(h, b);
    queued_transitions = 2;
    assert(finger_still_down(&e) == -1 && !e.touch.down && !queued_transitions && e.code == 1096);
    e = setup(h, b);
    struct uf_message request = {.operation = UF_OP_ENROLL, .request = 1}, reply = {0};
    assert(!capture(&e, &request, &reply) && reply.count == 1 && reply.prints[0].finger == 4711 &&
           !light);
    assert(command_calls[9] >= 1 && command_calls[7] == 1 && command_calls[29] == 1 &&
           command_calls[4] == 1);
    drain();
    e = setup(h, b);
    save_visible = false;
    reply = (struct uf_message){0};
    assert(capture(&e, &request, &reply) == -1 && errno == EIO && !light);
    drain();
    e = setup(h, b);
    algorithm_error = 1015;
    assert(capture(&e, &request, &reply) == -1 && e.code == 1015 && !light);
    assert(!command_calls[6] && command_calls[9] == 1); /* Wake only; no save/remove. */
    drain();
    const unsigned auth_errors[] = {1058, 1059, 1142};
    for (unsigned profile = UF_PROFILE_GFUNI; profile <= UF_PROFILE_GFUNI6; profile++) {
        for (unsigned i = 0; i < sizeof(auth_errors) / sizeof(auth_errors[0]); i++) {
            e = setup(h, b);
            e.profile = profile;
            authorization_error = auth_errors[i];
            reply = (struct uf_message){0};
            assert(capture(&e, &request, &reply) == -1 && errno == EREMOTEIO &&
                   e.code == (int32_t)auth_errors[i] && command_calls[4] == 1 && !light);
            assert(command_calls[2] == 1 && command_calls[3] == 1); /* No replay. */
            assert(!command_calls[1] && !command_calls[101] && !command_calls[9] &&
                   !command_calls[6] && !command_calls[29] && !reply.count);
            for (unsigned j = 33; j < 33 + UF_HAT_SIZE; j++)
                assert(!((unsigned char *)e.body.data)[j]);
            assert(recv(external_fd[1], received, sizeof(received), MSG_DONTWAIT) == -1 &&
                   errno == EAGAIN); /* No sample/progress before authorization. */
        }
    }
    e = setup(h, b);
    provider_error = ENOKEY;
    assert(capture(&e, &request, &reply) == -1 && errno == ENOKEY && !e.code && !light);
    assert(!command_calls[3] && !command_calls[9] && !command_calls[6] && command_calls[4] == 1);
    drain();
    e = setup(h, b);
    display_available = false;
    assert(capture(&e, &request, &reply) == -1 && errno == ENXIO && command_calls[4] == 1 &&
           !light);
    drain();
    e = setup(h, b);
    request.operation = UF_OP_VERIFY;
    request.count = 1;
    request.prints[0] = (struct uf_template){0, 4711};
    reply = (struct uf_message){0};
    assert(!capture(&e, &request, &reply) && reply.flags == 1 && reply.count == 1 &&
           command_calls[12] == 4 && !light);
    drain();
    e = setup(h, b);
    match.finger = 999;
    reply = (struct uf_message){0};
    assert(!capture(&e, &request, &reply) && reply.flags == 0 && reply.count == 0 && !light);
    drain();
    e = setup(h, b);
    algorithm_error = 1008;
    reply = (struct uf_message){0};
    assert(!capture(&e, &request, &reply) && reply.flags == 0 && reply.count == 0 && !light);
    drain();
    e = setup(h, b);
    algorithm_error = 1013;
    reply = (struct uf_message){0};
    assert(!capture(&e, &request, &reply) && reply.flags == 2 && reply.code == 1013 && !light);
    drain();
    e = setup(h, b);
    request.operation = UF_OP_VERIFY;
    provisional = 1;
    e.config[0xf0] = 1;
    put(e.config, 0x20, 1);
    reply = (struct uf_message){0};
    assert(!capture(&e, &request, &reply) && reply.flags == 1 && command_calls[2] == 2 &&
           command_calls[12] == 5);
    drain();
    e = setup(h, b);
    provisional = 99;
    reply = (struct uf_message){0};
    assert(!capture(&e, &request, &reply) && reply.flags == 2 && reply.code == 1066 &&
           !reply.count);
    drain();
    e = setup(h, b);
    algorithm_error = 1008;
    e.config[0xf0] = 1;
    put(e.config, 0x20, 1);
    reply = (struct uf_message){0};
    assert(!capture(&e, &request, &reply) && !reply.flags && command_calls[2] == 2 &&
           command_calls[3] == 1);
    drain();
    e = setup(h, b);
    algorithm_error = 1008;
    dsp_error = ETIMEDOUT;
    reply = (struct uf_message){0};
    assert(capture(&e, &request, &reply) == -1 && errno == ETIMEDOUT && !reply.flags &&
           e.code == 0);
    drain();
    e = setup(h, b);
    request.operation = UF_OP_ENROLL;
    early_finish = 1;
    remaining = 2;
    reply = (struct uf_message){0};
    assert(!capture(&e, &request, &reply) && reply.count == 1 && command_calls[24] == 1 && !light);
    drain();
    e = setup(h, b);
    request.operation = UF_OP_VERIFY;
    algorithm_error = 1008;
    reset_requested = true;
    put(e.config, 0x20, 2);
    reply = (struct uf_message){0};
    assert(capture(&e, &request, &reply) == -1 && errno == EIO && e.code == 0 &&
           command_calls[2] == 1 && !reply.flags && !light);
    drain();
    close(external_fd[0]);
    close(external_fd[1]);
    puts("engine: enrollment commit, failed persistence, authorization, no display, gallery "
         "isolation, no-match/retry and cleanup passed");
}
