/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "display.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <systemd/sd-login.h>
#include <time.h>
#include <unistd.h>
static int session_socket(void)
{
    char **sessions = NULL;
    int n = sd_get_sessions(&sessions), fd = -1;
    if (n < 0) {
        errno = -n;
        return -1;
    }
    for (int i = 0; i < n; i++) {
        char *type = NULL;
        uid_t uid;
        if (sd_session_is_active(sessions[i]) <= 0 || sd_session_is_remote(sessions[i]) != 0 ||
            sd_session_get_type(sessions[i], &type) < 0 ||
            (strcmp(type, "wayland") && strcmp(type, "tty")) ||
            sd_session_get_uid(sessions[i], &uid) < 0) {
            free(type);
            continue;
        }
        free(type);
        struct sockaddr_un a = {.sun_family = AF_UNIX};
        strcpy(a.sun_path, "/run/unicorn-fingerprint-display.sock");
        int next = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
        if (next < 0)
            continue;
        struct ucred cred;
        socklen_t size = sizeof(cred);
        if (connect(next, (struct sockaddr *)&a, sizeof(a)) ||
            getsockopt(next, SOL_SOCKET, SO_PEERCRED, &cred, &size) || cred.uid != 0) {
            close(next);
            continue;
        }
        struct uf_hint_hello hello = {UF_HINT_MAGIC, 1, 2, uid};
        if (send(next, &hello, sizeof(hello), MSG_NOSIGNAL) != sizeof(hello)) {
            close(next);
            continue;
        }
        if (fd >= 0) {
            close(next);
            close(fd);
            fd = -1;
            errno = ENOTUNIQ;
            break;
        }
        fd = next;
    }
    for (int i = 0; i < n; i++)
        free(sessions[i]);
    free(sessions);
    if (fd < 0 && errno != ENOTUNIQ)
        errno = ENXIO;
    return fd;
}
int uf_display_open(struct uf_display *d, int cancel)
{
    if (!d || d->illumination != -1 || d->session != -1) {
        errno = EINVAL;
        return -1;
    }
    d->illumination = open("/dev/unicorn-fingerprint-illumination", O_RDWR | O_CLOEXEC);
    if (d->illumination < 0)
        return -1;
    struct uf_illumination_info info;
    if (ioctl(d->illumination, UF_ILLUMINATION_INFO, &info))
        goto fail;
    if (info.version != 1 || info.width != 1440 || info.height != 3200 || info.x != 586 ||
        info.y != 2346 || info.w != 267 || info.h != 267) {
        errno = EPROTO;
        goto fail;
    }
    d->session = session_socket();
    if (d->session < 0)
        goto fail;
    ssize_t random_size = getrandom(&d->nonce, sizeof(d->nonce), 0);
    if (random_size != sizeof(d->nonce)) {
        if (random_size >= 0)
            errno = EIO;
        goto fail;
    }
    struct uf_hint_message message = {UF_HINT_MAGIC, 1, d->nonce, info.x, info.y, info.w, info.h};
    if (send(d->session, &message, sizeof(message), MSG_NOSIGNAL) != sizeof(message))
        goto fail;
    struct pollfd p[2] = {{d->session, POLLIN, 0}, {cancel, POLLIN, 0}};
    /* The helper may need 1200 ms for discovery plus 1500 ms for its first
     * frame. Use a total deadline, including EINTR, with broker headroom. */
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now))
        goto fail;
    int64_t deadline = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000 + 3500;
    int rc;
    for (;;) {
        if (clock_gettime(CLOCK_MONOTONIC, &now))
            goto fail;
        int64_t left = deadline - ((int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000);
        rc = left > 0 ? poll(p, 2, left) : 0;
        if (rc >= 0 || errno != EINTR)
            break;
    }
    if (p[1].revents) {
        errno = ECANCELED;
        goto fail;
    }
    if (rc <= 0) {
        if (!rc)
            errno = ETIMEDOUT;
        goto fail;
    }
    struct uf_hint_message reply = {0};
    ssize_t got = recv(d->session, &reply, sizeof(reply), MSG_TRUNC);
    if (got == 0) {
        errno = ENXIO;
        goto fail;
    }
    if (got != sizeof(reply) || memcmp(&reply, &message, sizeof(reply))) {
        errno = EPROTO;
        goto fail;
    }
    return 0;
fail:;
    int saved = errno;
    uf_display_close(d);
    errno = saved;
    return -1;
}
int uf_display_begin(struct uf_display *d)
{
    if (!d || d->illumination < 0 || d->session < 0) {
        errno = ENXIO;
        return -1;
    }
    struct pollfd p = {d->session, POLLIN, 0};
    if (poll(&p, 1, 0) != 0) {
        errno = ENXIO;
        return -1;
    }
    uint32_t timeout = 1500;
    return ioctl(d->illumination, UF_ILLUMINATION_ACQUIRE, &timeout);
}
int uf_display_ready(struct uf_display *d)
{
    struct uf_illumination_info i;
    if (!d || d->illumination < 0) {
        errno = ENXIO;
        return -1;
    }
    struct pollfd p = {d->session, POLLIN, 0};
    if (poll(&p, 1, 0) != 0) {
        errno = ENXIO;
        return -1;
    }
    if (ioctl(d->illumination, UF_ILLUMINATION_INFO, &i))
        return -1;
    if (!i.active) {
        errno = ETIMEDOUT;
        return -1;
    }
    return 0;
}
int uf_display_end(struct uf_display *d)
{
    return d && d->illumination >= 0 ? ioctl(d->illumination, UF_ILLUMINATION_RELEASE) : 0;
}
void uf_display_close(struct uf_display *d)
{
    if (!d)
        return;
    int saved = errno;
    if (d->illumination >= 0) {
        uf_display_end(d);
        close(d->illumination);
    }
    if (d->session >= 0)
        close(d->session);
    *d = (struct uf_display)UF_DISPLAY_CLOSED;
    errno = saved;
}
