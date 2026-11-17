/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "display.h"
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <systemd/sd-daemon.h>
#include <time.h>
#include <unistd.h>
#define CLIENTS 32
struct client {
    int fd;
    uid_t uid;
    bool registered;
    int64_t deadline;
};
static int64_t now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static void drop(struct client *c)
{
    if (c->fd >= 0)
        close(c->fd);
    *c = (struct client){.fd = -1};
}
static int pass(int helper, int root)
{
    char control[CMSG_SPACE(sizeof(int))] = {0}, byte = 'F';
    struct iovec iov = {&byte, 1};
    struct msghdr m = {.msg_iov = &iov,
                       .msg_iovlen = 1,
                       .msg_control = control,
                       .msg_controllen = sizeof(control)};
    struct cmsghdr *c = CMSG_FIRSTHDR(&m);
    c->cmsg_level = SOL_SOCKET;
    c->cmsg_type = SCM_RIGHTS;
    c->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(c), &root, sizeof(root));
    return sendmsg(helper, &m, MSG_NOSIGNAL) == 1 ? 0 : -1;
}
int main(void)
{
    if (sd_listen_fds(0) != 1 || sd_is_socket(3, AF_UNIX, SOCK_SEQPACKET, 1) <= 0)
        return 2;
    struct client clients[CLIENTS] = {0};
    for (unsigned i = 0; i < CLIENTS; i++)
        clients[i].fd = -1;
    for (;;) {
        struct pollfd p[CLIENTS + 1] = {{3, POLLIN, 0}};
        for (unsigned i = 0; i < CLIENTS; i++)
            p[i + 1] = (struct pollfd){clients[i].fd, POLLIN, 0};
        int ready = poll(p, CLIENTS + 1, 250);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            return 1;
        }
        for (unsigned i = 0; i < CLIENTS; i++) {
            struct client *c = &clients[i];
            if (c->fd < 0)
                continue;
            if (p[i + 1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                drop(c);
                continue;
            }
            if (!c->registered && now() >= c->deadline) {
                drop(c);
                continue;
            }
            if (!(p[i + 1].revents & POLLIN))
                continue;
            if (c->registered) {
                drop(c);
                continue;
            }
            struct uf_hint_hello hello;
            if (recv(c->fd, &hello, sizeof(hello), MSG_DONTWAIT | MSG_TRUNC) != sizeof(hello) ||
                hello.magic != UF_HINT_MAGIC || hello.version != 1) {
                drop(c);
                continue;
            }
            if (hello.role == 1 && hello.uid == c->uid) {
                bool duplicate = false;
                for (unsigned j = 0; j < CLIENTS; j++)
                    if (j != i && clients[j].fd >= 0 && clients[j].registered &&
                        clients[j].uid == c->uid)
                        duplicate = true;
                if (duplicate) {
                    drop(c);
                    continue;
                }
                c->registered = true;
            } else if (hello.role == 2 && c->uid == 0) {
                for (unsigned j = 0; j < CLIENTS; j++)
                    if (clients[j].fd >= 0 && clients[j].registered &&
                        clients[j].uid == hello.uid) {
                        if (pass(clients[j].fd, c->fd))
                            drop(&clients[j]);
                        break;
                    }
                drop(c); /* Helper owns a duplicate of the root endpoint. */
            } else
                drop(c);
        }
        if (p[0].revents & POLLIN) {
            int fd = accept4(3, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
            if (fd < 0)
                continue;
            struct ucred peer;
            socklen_t n = sizeof(peer);
            int slot = -1;
            for (unsigned i = 0; i < CLIENTS; i++)
                if (clients[i].fd < 0) {
                    slot = i;
                    break;
                }
            if (slot < 0 || getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &n)) {
                close(fd);
                continue;
            }
            clients[slot] = (struct client){fd, peer.uid, false, now() + 2000};
        }
    }
}
