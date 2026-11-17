/* SPDX-License-Identifier: MIT
 * Test-only worker: no TEE, sensor, DSP, illumination or template material. */
#define _GNU_SOURCE
#include "ipc.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
int main(void)
{
    bool present = true;
    for (;;) {
        struct uf_message q, r = {.magic = UF_IPC_MAGIC,
                                  .version = UF_IPC_VERSION,
                                  .operation = UF_EVENT_DONE,
                                  .profile = UF_PROFILE_GFUNI6};
        if (recv(3, &q, sizeof(q), MSG_TRUNC) != sizeof(q))
            return 0;
        r.request = q.request;
        switch (q.operation) {
        case UF_OP_INIT:
            r.stages = 4;
            memset(r.device, 'a', 70);
            r.device[70] = 0;
            break;
        case UF_OP_LIST:
            r.count = present ? 1 : 0;
            r.prints[0] = (struct uf_template){0, 4711};
            break;
        case UF_OP_DELETE:
            present = false;
            break;
        case UF_OP_ENROLL:
            if (getenv("UF_TEST_ENROLL_CODE")) {
                r.code = atoi(getenv("UF_TEST_ENROLL_CODE"));
                assert(r.code == 1058 || r.code == 1059 || r.code == 1142);
                r.error = EREMOTEIO;
                break;
            }
            r.operation = UF_EVENT_PROGRESS;
            r.stages = 2;
            assert(send(3, &r, sizeof(r), MSG_NOSIGNAL) == sizeof(r));
            r.operation = UF_EVENT_DONE;
            r.count = 1;
            r.prints[0] = (struct uf_template){0, 4711};
            break;
        case UF_OP_VERIFY:
        case UF_OP_IDENTIFY: {
            const char *mode = getenv("UF_TEST_MATCH");
            if (mode && !strcmp(mode, "wait")) {
                for (;;)
                    pause();
            }
            if (mode && !strcmp(mode, "retry")) {
                r.flags = 2;
                r.code = 1013;
                break;
            }
            if (mode && !strcmp(mode, "none"))
                break;
            r.flags = 1;
            r.count = 1;
            r.prints[0] = (struct uf_template){0, mode && !strcmp(mode, "outside") ? 999 : 4711};
            if (mode && !strcmp(mode, "flags"))
                r.flags = 3;
            if (mode && !strcmp(mode, "zero"))
                r.prints[0].finger = 0;
            if (mode && !strcmp(mode, "profile"))
                r.profile = UF_PROFILE_GFUNI;
            break;
        }
        default:
            return 2;
        }
        if (send(3, &r, sizeof(r), MSG_NOSIGNAL) != sizeof(r))
            return 1;
    }
}
