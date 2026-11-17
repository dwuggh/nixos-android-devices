/* SPDX-License-Identifier: MIT
 * Test namespace shim: simulate root peers without requiring a root build. */
#define _GNU_SOURCE
#include <sys/socket.h>
#include <unistd.h>
int __real_getsockopt(int, int, int, void *, socklen_t *);
int __wrap_getsockopt(int fd, int level, int option, void *value, socklen_t *length)
{
    int rc = __real_getsockopt(fd, level, option, value, length);
    if (!rc && level == SOL_SOCKET && option == SO_PEERCRED && *length == sizeof(struct ucred))
        ((struct ucred *)value)->uid = 0;
    return rc;
}
