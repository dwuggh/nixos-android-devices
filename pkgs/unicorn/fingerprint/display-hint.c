/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "display.h"
#include "idle-inhibit.h"
#include "layer-shell.h"
#include "viewporter.h"
#include <cairo/cairo.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <systemd/sd-daemon.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
struct output {
    struct wl_output *wl;
    int width, height, transform;
};
struct hint {
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct zwlr_layer_shell_v1 *shell;
    struct wp_viewporter *viewporter;
    struct zwp_idle_inhibit_manager_v1 *idle;
    struct output outputs[8];
    unsigned count;
    uint32_t globals[13];
    unsigned globals_count;
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer;
    struct wp_viewport *viewport;
    struct zwp_idle_inhibitor_v1 *inhibitor;
    struct wl_buffer *buffer;
    struct wl_callback *frame, *sync;
    unsigned discovery;
    struct uf_hint_message request;
    int client;
    bool dead, ready;
};
static void geometry(void *data, struct wl_output *o, int32_t x, int32_t y, int32_t w, int32_t h,
                     int32_t sub, const char *make, const char *model, int32_t transform)
{
    (void)o;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)sub;
    (void)make;
    (void)model;
    ((struct output *)data)->transform = transform;
}
static void mode(void *data, struct wl_output *o, uint32_t flags, int32_t w, int32_t h, int32_t hz)
{
    (void)o;
    (void)hz;
    if (flags & WL_OUTPUT_MODE_CURRENT) {
        ((struct output *)data)->width = w;
        ((struct output *)data)->height = h;
    }
}
static void output_done(void *d, struct wl_output *o)
{
    (void)d;
    (void)o;
}
static void scale(void *d, struct wl_output *o, int32_t factor)
{
    (void)d;
    (void)o;
    (void)factor;
}
static const struct wl_output_listener output_listener = {
    .geometry = geometry, .mode = mode, .done = output_done, .scale = scale};
static void global(void *data, struct wl_registry *r, uint32_t name, const char *interface,
                   uint32_t version)
{
    struct hint *h = data;
#define BIND(field, type, ver)                                                                     \
    if (!strcmp(interface, #type)) {                                                               \
        if (h->field || h->globals_count >= 13)                                                    \
            return;                                                                               \
        h->globals[h->globals_count++] = name;                                                     \
        h->field =                                                                                 \
            wl_registry_bind(r, name, &type##_interface, version < (ver) ? version : (ver));       \
        return;                                                                                    \
    }
    BIND(compositor, wl_compositor, 4);
    BIND(shm, wl_shm, 1);
    BIND(shell, zwlr_layer_shell_v1, 1);
    BIND(viewporter, wp_viewporter, 1);
    BIND(idle, zwp_idle_inhibit_manager_v1, 1);
#undef BIND
    if (!strcmp(interface, "wl_output") && h->count < 8) {
        h->globals[h->globals_count++] = name;
        struct output *o = &h->outputs[h->count++];
        o->wl = wl_registry_bind(r, name, &wl_output_interface, version < 2 ? version : 2);
        wl_output_add_listener(o->wl, &output_listener, o);
    }
}
static void removed(void *d, struct wl_registry *r, uint32_t name)
{
    (void)r;
    struct hint *h = d;
    for (unsigned i = 0; i < h->globals_count; i++)
        if (h->globals[i] == name)
            h->dead = true;
}
static const struct wl_registry_listener registry_listener = {global, removed};
static void frame_done(void *data, struct wl_callback *cb, uint32_t time)
{
    (void)time;
    struct hint *h = data;
    wl_callback_destroy(cb);
    h->frame = NULL;
    if (send(h->client, &h->request, sizeof(h->request), MSG_NOSIGNAL) != sizeof(h->request))
        h->dead = true;
    h->ready = true;
}
static const struct wl_callback_listener frame_listener = {frame_done};
static const struct wl_callback_listener sync_listener;
static void sync_done(void *data, struct wl_callback *cb, uint32_t serial)
{
    (void)serial;
    struct hint *h = data;
    wl_callback_destroy(cb);
    h->sync = NULL;
    h->discovery++;
    if (h->discovery < 2) {
        h->sync = wl_display_sync(h->display);
        wl_callback_add_listener(h->sync, &sync_listener, h);
    }
}
static const struct wl_callback_listener sync_listener = {sync_done};
static void configure(void *data, struct zwlr_layer_surface_v1 *layer, uint32_t serial,
                      uint32_t width, uint32_t height)
{
    struct hint *h = data;
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    if (!width || !height || width > 8192 || height > 8192) {
        h->dead = true;
        return;
    }
    /* Absorb touches inside the capture rectangle so pressing the sensor
     * cannot activate a button in the application underneath. Raw evdev
     * collection remains independent of compositor touch focus. */
    struct wl_region *region = wl_compositor_create_region(h->compositor);
    wl_region_add(region, 586U * width / 1440U, 2346U * height / 3200U,
                  (267U * width + 1439U) / 1440U, (267U * height + 3199U) / 3200U);
    wl_surface_set_input_region(h->surface, region);
    wl_region_destroy(region);
    wp_viewport_set_destination(h->viewport, width, height);
    if (h->buffer) {
        h->dead = true;
        return;
    }
    const size_t bytes = 1440U * 3200U * 4U;
    int fd = memfd_create("fingerprint-hint", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0 || ftruncate(fd, bytes)) {
        if (fd >= 0)
            close(fd);
        h->dead = true;
        return;
    }
    void *pixels = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pixels == MAP_FAILED) {
        close(fd);
        h->dead = true;
        return;
    }
    cairo_surface_t *image =
        cairo_image_surface_create_for_data(pixels, CAIRO_FORMAT_ARGB32, 1440, 3200, 1440 * 4);
    cairo_t *cr = cairo_create(image);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_arc(cr, 719.5, 2479.5, 133.5, 0, 2 * 3.141592653589793);
    cairo_fill(cr);
    cairo_destroy(cr);
    cairo_surface_flush(image);
    cairo_surface_destroy(image);
    struct wl_shm_pool *pool = wl_shm_create_pool(h->shm, fd, bytes);
    h->buffer = wl_shm_pool_create_buffer(pool, 0, 1440, 3200, 1440 * 4, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    munmap(pixels, bytes);
    close(fd);
    wl_surface_attach(h->surface, h->buffer, 0, 0);
    wl_surface_damage(h->surface, 0, 0, INT32_MAX, INT32_MAX);
    h->frame = wl_surface_frame(h->surface);
    wl_callback_add_listener(h->frame, &frame_listener, h);
    wl_surface_commit(h->surface);
}
static void closed(void *d, struct zwlr_layer_surface_v1 *l)
{
    (void)l;
    ((struct hint *)d)->dead = true;
}
static const struct zwlr_layer_surface_v1_listener layer_listener = {configure, closed};
static int64_t now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static void serve(int client)
{
    struct ucred cred;
    socklen_t size = sizeof(cred);
    if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &cred, &size) || cred.uid != 0)
        return;
    struct hint h = {.client = client};
    struct pollfd first = {client, POLLIN, 0};
    if (poll(&first, 1, 1000) != 1 ||
        recv(client, &h.request, sizeof(h.request), MSG_TRUNC) != sizeof(h.request) ||
        h.request.magic != UF_HINT_MAGIC || h.request.version != 1 || h.request.x != 586 ||
        h.request.y != 2346 || h.request.w != 267 || h.request.h != 267)
        return;
    h.display = wl_display_connect(NULL);
    if (!h.display)
        return;
    struct wl_registry *registry = wl_display_get_registry(h.display);
    wl_registry_add_listener(registry, &registry_listener, &h);
    int64_t deadline = now() + 1200;
    h.sync = wl_display_sync(h.display);
    wl_callback_add_listener(h.sync, &sync_listener, &h);
    while (h.discovery < 2 && !h.dead) {
        if (wl_display_dispatch_pending(h.display) < 0)
            goto out;
        if (h.discovery >= 2)
            break;
        while (wl_display_prepare_read(h.display) != 0)
            if (wl_display_dispatch_pending(h.display) < 0)
                goto out;
        int flush = wl_display_flush(h.display);
        if (flush < 0 && errno != EAGAIN) {
            wl_display_cancel_read(h.display);
            goto out;
        }
        struct pollfd p[2] = {{wl_display_get_fd(h.display),
                               POLLIN | (flush < 0 ? POLLOUT : 0), 0}, {client, POLLIN, 0}};
        int64_t left = deadline - now();
        int rc = left > 0 ? poll(p, 2, left) : 0;
        if (rc < 0 && errno == EINTR) {
            wl_display_cancel_read(h.display);
            continue;
        }
        if (rc <= 0 || p[1].revents) {
            wl_display_cancel_read(h.display);
            goto out;
        }
        if (p[0].revents & POLLIN) {
            if (wl_display_read_events(h.display) < 0)
                goto out;
        } else {
            wl_display_cancel_read(h.display);
            if (p[0].revents & (POLLERR | POLLHUP | POLLNVAL))
                goto out;
        }
    }
    struct output *output = NULL;
    for (unsigned i = 0; i < h.count; i++)
        if (h.outputs[i].width == 1440 && h.outputs[i].height == 3200 &&
            h.outputs[i].transform == WL_OUTPUT_TRANSFORM_NORMAL) {
            if (output) {
                output = NULL;
                break;
            }
            output = &h.outputs[i];
        }
    if (h.discovery < 2 || h.dead || !output || !h.compositor || !h.shm || !h.shell ||
        !h.viewporter || !h.idle)
        goto out;
    h.surface = wl_compositor_create_surface(h.compositor);
    struct wl_region *empty = wl_compositor_create_region(h.compositor);
    wl_surface_set_input_region(h.surface, empty);
    wl_region_destroy(empty);
    h.viewport = wp_viewporter_get_viewport(h.viewporter, h.surface);
    h.inhibitor = zwp_idle_inhibit_manager_v1_create_inhibitor(h.idle, h.surface);
    h.layer = zwlr_layer_shell_v1_get_layer_surface(
        h.shell, h.surface, output->wl, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "unicorn-fingerprint");
    zwlr_layer_surface_v1_add_listener(h.layer, &layer_listener, &h);
    zwlr_layer_surface_v1_set_anchor(h.layer, 15);
    zwlr_layer_surface_v1_set_exclusive_zone(h.layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(h.layer, 0);
    wl_surface_commit(h.surface);
    deadline = now() + 1500;
    bool watchdog_set = false;
    while (!h.dead) {
        if (wl_display_dispatch_pending(h.display) < 0)
            break;
        while (wl_display_prepare_read(h.display) != 0)
            if (wl_display_dispatch_pending(h.display) < 0)
                goto out;
        int flush = wl_display_flush(h.display);
        struct pollfd p[2] = {{wl_display_get_fd(h.display),
                               POLLIN | (flush < 0 && errno == EAGAIN ? POLLOUT : 0), 0},
                              {client, POLLIN, 0}};
        int64_t left = deadline - now();
        int rc = left > 0 ? poll(p, 2, left > 1000 ? 1000 : left) : 0;
        if (rc < 0 && errno == EINTR) {
            wl_display_cancel_read(h.display);
            continue;
        }
        if (rc < 0 || left <= 0 || p[1].revents) {
            wl_display_cancel_read(h.display);
            break;
        }
        if (p[0].revents & POLLIN) {
            if (wl_display_read_events(h.display) < 0)
                break;
        } else
            wl_display_cancel_read(h.display);
        if (p[0].revents & (POLLERR | POLLHUP | POLLNVAL))
            break;
        if (h.ready && !watchdog_set) {
            deadline = now() + 120000;
            watchdog_set = true;
        }
    }
out:
    if (h.sync)
        wl_callback_destroy(h.sync);
    if (h.frame)
        wl_callback_destroy(h.frame);
    if (h.layer)
        zwlr_layer_surface_v1_destroy(h.layer);
    if (h.inhibitor)
        zwp_idle_inhibitor_v1_destroy(h.inhibitor);
    if (h.viewport)
        wp_viewport_destroy(h.viewport);
    if (h.surface)
        wl_surface_destroy(h.surface);
    if (h.buffer)
        wl_buffer_destroy(h.buffer);
    for (unsigned i = 0; i < h.count; i++)
        wl_output_destroy(h.outputs[i].wl);
    if (h.idle)
        zwp_idle_inhibit_manager_v1_destroy(h.idle);
    if (h.viewporter)
        wp_viewporter_destroy(h.viewporter);
    if (h.shell)
        zwlr_layer_shell_v1_destroy(h.shell);
    if (h.shm)
        wl_shm_destroy(h.shm);
    if (h.compositor)
        wl_compositor_destroy(h.compositor);
    wl_registry_destroy(registry);
    wl_display_flush(h.display);
    wl_display_disconnect(h.display);
}
int main(void)
{
    int broker = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    strcpy(address.sun_path, "/run/unicorn-fingerprint-display.sock");
    if (broker < 0 || connect(broker, (struct sockaddr *)&address, sizeof(address))) {
        perror("fingerprint hint: connect to display broker");
        return 1;
    }
    struct ucred peer;
    socklen_t length = sizeof(peer);
    if (getsockopt(broker, SOL_SOCKET, SO_PEERCRED, &peer, &length)) {
        perror("fingerprint hint: broker credentials");
        return 1;
    }
    if (peer.uid != 0) {
        fprintf(stderr, "fingerprint hint: broker uid=%lu is not host root; check user namespace isolation\n",
                (unsigned long)peer.uid);
        return 1;
    }
    struct uf_hint_hello hello = {UF_HINT_MAGIC, 1, 1, getuid()};
    if (send(broker, &hello, sizeof(hello), MSG_NOSIGNAL) != sizeof(hello))
        return 1;
    signal(SIGPIPE, SIG_IGN);
    for (;;) {
        char byte = 0, control[CMSG_SPACE(sizeof(int) * 4)] = {0};
        struct iovec vector = {&byte, 1};
        struct msghdr message = {.msg_iov = &vector,
                                 .msg_iovlen = 1,
                                 .msg_control = control,
                                 .msg_controllen = sizeof(control)};
        ssize_t n = recvmsg(broker, &message, MSG_CMSG_CLOEXEC);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return 1;
        int fd = -1;
        unsigned count = 0;
        for (struct cmsghdr *c = CMSG_FIRSTHDR(&message); c; c = CMSG_NXTHDR(&message, c))
            if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
                size_t fds = (c->cmsg_len - CMSG_LEN(0)) / sizeof(int);
                int *received = (int *)CMSG_DATA(c);
                for (size_t i = 0; i < fds; i++) {
                    if (!count)
                        fd = received[i];
                    else
                        close(received[i]);
                    count++;
                }
            }
        if (fd < 0)
            continue;
        if (n != 1 || byte != 'F' || count != 1 || (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC))) {
            close(fd);
            continue;
        }
        serve(fd);
        close(fd);
    }
}
