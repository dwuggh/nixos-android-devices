/* SPDX-License-Identifier: MIT */
#ifndef UF_IPC_H
#define UF_IPC_H
#include "protocol.h"
#define UF_IPC_MAGIC 0x55465031U
#define UF_IPC_VERSION 1U
enum uf_operation {
    UF_OP_INIT = 1,
    UF_OP_LIST,
    UF_OP_DELETE,
    UF_OP_ENROLL,
    UF_OP_VERIFY,
    UF_OP_IDENTIFY,
    UF_OP_CLOSE
};
enum uf_event { UF_EVENT_DONE = 100, UF_EVENT_PROGRESS, UF_EVENT_RETRY, UF_EVENT_FINGER };
/* Private SOCK_SEQPACKET channel between same-architecture native processes.
 * It contains locators/status only, never TA payloads, HATs or images. */
struct uf_message {
    uint32_t magic, version, operation, request, profile, stages;
    int32_t code, error;
    uint32_t count, flags;
    struct uf_template prints[UF_TEMPLATE_MAX];
    char device[80];
};
_Static_assert(sizeof(struct uf_message) == 200, "private IPC version 1");
int uf_worker_serve(int fd);
int uf_qualify(const char *name);
int uf_probe(const char *name, struct uf_message *result);
#endif
