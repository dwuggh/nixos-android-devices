/* SPDX-License-Identifier: MIT */
#ifndef UNICORN_GATEKEEPER_H
#define UNICORN_GATEKEEPER_H
#include "tee.h"
#include <stdbool.h>

/* Resident Keymaster client. Credential operations belong to an independent
 * authorization provider, never to the fingerprint worker IPC. */
struct uf_gatekeeper {
    struct uf_tee tee;
    struct uf_buffer buffer;
    uint32_t api_major, api_minor, ta_major, ta_minor;
    int32_t status;
    uint32_t retry_ms;
    uint32_t command;
    size_t request_bytes, response_offset;
    bool broken;
};
#define UF_GATEKEEPER_CLOSED {.tee = UF_TEE_CLOSED}

/* OEM 0x200 query and 0x207 client-version negotiation only: no credential
 * enrollment, verification, deletion or fingerprint command is issued. */
int uf_gatekeeper_open(struct uf_gatekeeper *, struct uf_tee_result *);
int uf_gatekeeper_close(struct uf_gatekeeper *);

/* Separate Linux credential namespace; Android UIDs are never accepted by
 * these provisioning/verification entry points. No delete/reset API exists. */
#define UF_GATEKEEPER_UID_BASE UINT32_C(0x55460000)
#define UF_GATEKEEPER_HANDLE_MAX 1024U
int uf_gatekeeper_enroll(struct uf_gatekeeper *, uint32_t uid,
                         const unsigned char password[32], unsigned char *handle,
                         size_t *handle_size, struct uf_tee_result *);
int uf_gatekeeper_verify(struct uf_gatekeeper *, uint32_t uid, uint64_t challenge,
                         const unsigned char password[32], const unsigned char *handle,
                         size_t handle_size, unsigned char hat[69], struct uf_tee_result *);
#endif
