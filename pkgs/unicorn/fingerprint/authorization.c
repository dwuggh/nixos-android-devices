/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "authorization.h"
#include "protocol.h"
#include "tee.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static int64_t now(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t))
        return -1;
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
int uf_authorization_request(uint64_t challenge, int cancel, unsigned char hat[69])
{
    if (hat)
        memset(hat, 0, 69);
    if (!hat || !challenge) {
        errno = EINVAL;
        return -1;
    }
    int directory = open(UF_AUTH_DIRECTORY, O_DIRECTORY | O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (directory < 0) {
        if (errno == ENOENT)
            errno = ENOKEY;
        return -1;
    }
    struct stat st;
    int valid = !fstat(directory, &st) && st.st_uid == 0 && (st.st_mode & 0777) == 0700;
    close(directory);
    if (!valid) {
        errno = EACCES;
        return -1;
    }
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0)
        return -1;
    int rc = -1, saved;
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    strcpy(address.sun_path, UF_AUTH_SOCKET);
    /* A provider starts listening before the wrapper launches fprintd-enroll.
     * A full/not-listening socket is a failed session, never an unbounded wait. */
    if (connect(fd, (struct sockaddr *)&address, sizeof(address))) {
        if (errno == ENOENT || errno == ECONNREFUSED || errno == EAGAIN)
            errno = ENOKEY;
        goto out;
    }
    struct ucred peer;
    socklen_t size = sizeof(peer);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &size) || peer.uid != 0) {
        errno = EACCES;
        goto out;
    }
    struct uf_auth_request request = {UF_AUTH_MAGIC, 1, challenge};
    if (send(fd, &request, sizeof(request), MSG_NOSIGNAL) != sizeof(request))
        goto out;
    int64_t deadline = now() + 15000;
    struct pollfd p[] = {{fd, POLLIN, 0}, {cancel, POLLIN, 0}};
    for (;;) {
        int64_t left = deadline - now();
        int ready = left > 0 ? poll(p, 2, left) : 0;
        if (ready < 0 && errno == EINTR)
            continue;
        if (p[1].revents) {
            errno = ECANCELED;
            goto out;
        }
        if (ready <= 0) {
            if (!ready)
                errno = ETIMEDOUT;
            goto out;
        }
        break;
    }
    struct uf_auth_response response = {0};
    ssize_t length = recv(fd, &response, sizeof(response), MSG_TRUNC);
    if (length != sizeof(response) || response.magic != UF_AUTH_MAGIC || response.version != 1 ||
        response.error < 0 || response.error > 4095 || response.reserved[0] ||
        response.reserved[1] || response.reserved[2]) {
        uf_wipe(&response, sizeof(response));
        errno = EPROTO;
        goto out;
    }
    if (response.error) {
        fprintf(stderr, "event=enroll_token_provider errno=%d retry_ms=%u\n",
                response.error, response.retry_ms);
        errno = response.error;
        uf_wipe(&response, sizeof(response));
        goto out;
    }
    uint64_t received_challenge, sid;
    uint32_t authenticator;
    uf_wire_get64(response.hat, 69, 1, &received_challenge);
    uf_wire_get64(response.hat, 69, 9, &sid);
    uf_wire_get32(response.hat, 69, 25, &authenticator);
    if (response.hat[0] || received_challenge != challenge || !sid ||
        authenticator != UINT32_C(0x01000000) || response.retry_ms) {
        uf_wipe(&response, sizeof(response));
        errno = EPROTO;
        goto out;
    }
    memcpy(hat, response.hat, 69);
    uf_wipe(&response, sizeof(response));
    rc = 0;
out:
    saved = errno;
    close(fd);
    errno = saved;
    return rc;
}
