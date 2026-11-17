/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "ipc.h"
#include "platform.h"
#include "selection.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <systemd/sd-daemon.h>
#include <unistd.h>
#ifndef UF_APP_LOADER
#define UF_APP_LOADER "qsee-app-loader"
#endif
static volatile sig_atomic_t stopping;
static void stop(int sig)
{
    (void)sig;
    stopping = 1;
}
static void release(pid_t pid)
{
    if (pid <= 0)
        return;
    pid_t reaped = waitpid(pid, NULL, WNOHANG);
    if (reaped == pid || (reaped < 0 && errno == ECHILD))
        return;
    kill(pid, SIGTERM);
    for (unsigned i = 0; i < 100; i++) {
        if (waitpid(pid, NULL, WNOHANG) == pid)
            return;
        struct pollfd none = {-1, 0, 0};
        poll(&none, 0, 20);
    }
    kill(pid, SIGKILL);
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
    }
}
static pid_t load(const char *name)
{
    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    snprintf(address.sun_path, sizeof(address.sun_path), "/run/unicorn-fingerprint/notify-%ld",
             (long)getpid());
    unlink(address.sun_path);
    int one = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_PASSCRED, &one, sizeof(one)) ||
        bind(fd, (struct sockaddr *)&address, sizeof(address))) {
        close(fd);
        return -1;
    }
    pid_t parent = getpid(), pid = fork();
    if (!pid) {
        if (prctl(PR_SET_PDEATHSIG, SIGTERM) || getppid() != parent)
            _exit(1);
        setenv("NOTIFY_SOCKET", address.sun_path, 1);
        execl(UF_APP_LOADER, UF_APP_LOADER, name, (char *)NULL);
        _exit(127);
    }
    bool ready = false;
    for (unsigned i = 0; pid > 0 && !stopping && i < 100; i++) {
        struct pollfd p = {fd, POLLIN, 0};
        int rc = poll(&p, 1, 100);
        if (rc < 0 && errno != EINTR)
            break;
        if (rc > 0) {
            char text[256] = {0}, control[CMSG_SPACE(sizeof(struct ucred))];
            struct iovec vec = {text, sizeof(text) - 1};
            struct msghdr message = {.msg_iov = &vec,
                                     .msg_iovlen = 1,
                                     .msg_control = control,
                                     .msg_controllen = sizeof(control)};
            ssize_t n = recvmsg(fd, &message, 0);
            struct ucred *cred = NULL;
            for (struct cmsghdr *c = CMSG_FIRSTHDR(&message); c; c = CMSG_NXTHDR(&message, c))
                if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_CREDENTIALS &&
                    c->cmsg_len >= CMSG_LEN(sizeof(struct ucred)))
                    cred = (void *)CMSG_DATA(c);
            if (n > 0 && cred && cred->pid == pid && cred->uid == 0 &&
                (!strncmp(text, "READY=1\n", 8) || !strcmp(text, "READY=1"))) {
                ready = true;
                break;
            }
        }
        if (waitpid(pid, NULL, WNOHANG) == pid) {
            pid = -1;
            break;
        }
    }
    close(fd);
    unlink(address.sun_path);
    if (!ready) {
        release(pid);
        errno = EIO;
        return -1;
    }
    return pid;
}
/* A separate process gives each candidate fresh FastRPC globals and bounds
 * kernel RPCs that cannot safely be interrupted by pthread cancellation. */
static int qualify(const char *name, void *unused)
{
    (void)unused;
    if (stopping)
        return -1;
    pid_t loader = load(name);
    if (loader < 0)
        return -1;
    pid_t parent = getpid(), pid = fork();
    if (!pid) {
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent)
            _exit(1);
        int rc = uf_qualify(name), saved = errno;
        fprintf(stderr, "event=initialization_probe ta=%s accepted=%u errno=%d\n",
                name, !rc, rc ? saved : 0);
        _exit(rc ? 1 : 0);
    }
    int result = -1, status;
    for (unsigned i = 0; pid > 0 && !stopping && i < 300; i++) {
        pid_t reaped = waitpid(pid, &status, WNOHANG);
        if (reaped == pid) {
            result = WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
            pid = -1;
            break;
        }
        if (reaped < 0 && errno != EINTR)
            break;
        poll(NULL, 0, 100);
    }
    if (pid > 0)
        fprintf(stderr, "event=initialization_aborted ta=%s stopping=%u\n", name, !!stopping);
    release(pid); /* Reap before unloading the TA or probing another candidate. */
    release(loader);
    return stopping ? -1 : result;
}
int main(void)
{
    if (geteuid() != 0)
        return 1;
    struct sigaction action = {.sa_handler = stop};
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    umask(0077);
    unlink("/run/unicorn-fingerprint/profile");
    unlink("/run/unicorn-fingerprint/profile.new");
    char dependency[192] = {0};
    unsigned waiting = 0;
    while (uf_platform_check(dependency, sizeof(dependency))) {
        /* Allow asynchronous remoteproc boot/udev to settle, without repeatedly
         * loading either TA when their shared prerequisite cannot start. */
        if (stopping)
            return 0;
        if (waiting++ == 100) {
            fprintf(stderr, "event=platform_unavailable %s\n", dependency);
            return 78;
        }
        poll(NULL, 0, 100);
    }
    pid_t km = load("keymaster64"), chosen = -1;
    int rc = 1;
    const char *winner = NULL;
    if (km < 0) {
        rc = 78;
        goto out;
    }
    char cached[8] = {0};
    uf_cache_read("/var/lib/unicorn-fingerprint-selector", cached);
    winner = uf_select(cached, qualify, NULL);
    if (!winner || stopping) {
        fprintf(stderr, "event=selection_failed\n");
        rc = stopping ? 0 : 78;
        goto out;
    }
    if (uf_cache_write("/var/lib/unicorn-fingerprint-selector", winner))
        fprintf(stderr, "event=selection_cache_write_failed errno=%d\n", errno);
    chosen = load(winner);
    if (chosen < 0)
        goto out;
    int fd = open("/run/unicorn-fingerprint/profile.new",
                  O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        goto out;
    char line[16];
    int length = snprintf(line, sizeof(line), "%s\n", winner);
    bool written = write(fd, line, length) == length && !fsync(fd) && !fchmod(fd, 0644);
    if (close(fd) || !written ||
        rename("/run/unicorn-fingerprint/profile.new", "/run/unicorn-fingerprint/profile"))
        goto out;
    int control = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    strcpy(address.sun_path, "/run/unicorn-fingerprint/control");
    unlink(address.sun_path);
    if (control < 0 || bind(control, (struct sockaddr *)&address, sizeof(address)) ||
        listen(control, 1)) {
        if (control >= 0)
            close(control);
        goto out;
    }
    sd_notify(
        0, "READY=1\nSTATUS=Unicorn full initialization passed; biometric acceptance pending");
    while (!stopping) {
        if (waitpid(km, NULL, WNOHANG) == km || waitpid(chosen, NULL, WNOHANG) == chosen)
            break;
        struct pollfd p = {control, POLLIN, 0};
        int ready = poll(&p, 1, 250);
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready <= 0)
            continue;
        int client = accept4(control, NULL, NULL, SOCK_CLOEXEC);
        if (client < 0)
            continue;
        struct ucred peer;
        socklen_t length = sizeof(peer);
        int process = -1;
        if (!getsockopt(client, SOL_SOCKET, SO_PEERCRED, &peer, &length) && peer.uid == 0)
            process = syscall(SYS_pidfd_open, peer.pid, 0);
        if (process < 0) {
            close(client);
            continue;
        }
        char acknowledgement = 'R';
        if (send(client, &acknowledgement, 1, MSG_NOSIGNAL) == 1) {
            /* Wait for actual process exit, including close of TEE and DSP
             * files. A socket HUP alone can precede file-table destruction. */
            struct pollfd exit_event = {process, POLLIN, 0};
            while (!stopping) {
                int status = poll(&exit_event, 1, 250);
                if (status < 0 && errno == EINTR)
                    continue;
                if (status > 0)
                    break;
                if (status < 0) {
                    stopping = 1;
                    break;
                }
            }
        }
        close(process);
        close(client);
        if (stopping)
            break;
        release(chosen);
        chosen = -1;
        chosen = load(winner);
        if (chosen < 0)
            break;
    }
    close(control);
    unlink(address.sun_path);
    rc = stopping ? 0 : 1;
out:
    unlink("/run/unicorn-fingerprint/profile");
    unlink("/run/unicorn-fingerprint/profile.new");
    release(chosen);
    release(km);
    return rc;
}
