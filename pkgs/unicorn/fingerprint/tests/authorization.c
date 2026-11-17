/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
static char provider_directory[80];
#define UF_AUTH_DIRECTORY provider_directory
#include "authorization.h"
#undef UF_AUTH_SOCKET
static char socket_path[108];
#define UF_AUTH_SOCKET socket_path
#include "../authorization.c"

static bool bad_directory, bad_peer;
int __real_fstat(int, struct stat *);
int __wrap_fstat(int fd, struct stat *st)
{
    int rc = __real_fstat(fd, st);
    if (!rc) st->st_uid = bad_directory ? 65534 : 0;
    return rc;
}
int __real_getsockopt(int, int, int, void *, socklen_t *);
int __wrap_getsockopt(int fd, int level, int option, void *value, socklen_t *size)
{
    int rc = __real_getsockopt(fd, level, option, value, size);
    if (!rc && level == SOL_SOCKET && option == SO_PEERCRED)
        ((struct ucred *)value)->uid = bad_peer ? 65534 : 0;
    return rc;
}
static void zero(const unsigned char hat[69])
{
    for (unsigned i = 0; i < 69; i++) assert(!hat[i]);
}
static int server(void)
{
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    assert(fd >= 0);
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    strcpy(address.sun_path, socket_path);
    assert(!bind(fd, (struct sockaddr *)&address, sizeof(address)) && !listen(fd, 1));
    return fd;
}
static void sequence(int mode, int expected)
{
    int fd = server();
    int cancel[2];
    assert(!pipe(cancel));
    if (mode == 4) assert(write(cancel[1], "x", 1) == 1);
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        signal(SIGPIPE, SIG_IGN);
        int client = accept(fd, NULL, NULL);
        assert(client >= 0);
        struct uf_auth_request request;
        if (mode == 5) { close(client); _exit(0); }
        assert(recv(client, &request, sizeof(request), MSG_TRUNC) == sizeof(request));
        assert(request.magic == UF_AUTH_MAGIC && request.version == 1 && request.challenge == 123);
        struct uf_auth_response response = {.magic = UF_AUTH_MAGIC, .version = 1};
        uf_wire_put64(response.hat, 69, 1, mode == 2 ? 124 : 123);
        uf_wire_put64(response.hat, 69, 9, 55);
        uf_wire_put32(response.hat, 69, 25, 0x01000000);
        /* This fixture exercises transport only; it is not a signed token. */
        memset(response.hat + 37, 0xa5, 32);
        if (mode == 1) { response.error = EAGAIN; response.retry_ms = 30000; }
        (void)send(client, &response, mode == 3 ? sizeof(response) - 1 : sizeof(response), MSG_NOSIGNAL);
        close(client);
        _exit(0);
    }
    close(fd);
    unsigned char hat[69];
    memset(hat, 0xff, sizeof(hat));
    bad_peer = mode == 5;
    int rc = uf_authorization_request(123, cancel[0], hat);
    assert(expected ? rc == -1 && errno == expected : rc == 0);
    if (expected) zero(hat);
    else assert(hat[37] == 0xa5);
    bad_peer = false;
    close(cancel[0]); close(cancel[1]);
    int status;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
    assert(!unlink(socket_path));
}
int main(void)
{
    strcpy(provider_directory, "/tmp/uf-provider-XXXXXX");
    assert(mkdtemp(provider_directory));
    snprintf(socket_path, sizeof(socket_path), "%s/provider.sock", provider_directory);
    unsigned char hat[69];
    assert(uf_authorization_request(0, -1, hat) == -1 && errno == EINVAL);
    zero(hat);
    assert(uf_authorization_request(123, -1, hat) == -1 && errno == ENOKEY);
    bad_directory = true;
    assert(uf_authorization_request(123, -1, hat) == -1 && errno == EACCES);
    bad_directory = false;
    assert(!chmod(provider_directory, 0755));
    assert(uf_authorization_request(123, -1, hat) == -1 && errno == EACCES);
    assert(!chmod(provider_directory, 0700));
    sequence(0, 0); sequence(1, EAGAIN); sequence(2, EPROTO);
    sequence(3, EPROTO); sequence(4, ECANCELED); sequence(5, EACCES);
    assert(!rmdir(provider_directory));
    puts("authorization transport: real sockets, root peers, challenge, truncation, errors and cancel passed");
}
