/* SPDX-License-Identifier: MIT */
#ifndef UNICORN_AUTHORIZATION_H
#define UNICORN_AUTHORIZATION_H
#include <stdint.h>
#ifndef UF_AUTH_DIRECTORY
#define UF_AUTH_DIRECTORY "/run/unicorn-fingerprint-authorization"
#endif
#define UF_AUTH_SOCKET UF_AUTH_DIRECTORY "/provider.sock"
#define UF_AUTH_MAGIC UINT32_C(0x55464131)
struct uf_auth_request {
    uint32_t magic, version;
    uint64_t challenge;
};
struct uf_auth_response {
    uint32_t magic, version;
    int32_t error;
    uint32_t retry_ms;
    unsigned char hat[69];
    unsigned char reserved[3];
};
_Static_assert(sizeof(struct uf_auth_request) == 16, "authorization request version 1");
_Static_assert(sizeof(struct uf_auth_response) == 88, "authorization response version 1");
int uf_authorization_request(uint64_t challenge, int cancel, unsigned char hat[69]);
#endif
