/* SPDX-License-Identifier: MIT */
#ifndef UNICORN_AUTHORIZATION_STATE_H
#define UNICORN_AUTHORIZATION_STATE_H
#include "gatekeeper.h"
#define UF_CREDENTIAL_DIRECTORY "/var/lib/unicorn-fingerprint-authorization"
struct uf_credential {
    uint32_t uid;
    unsigned char salt[16];
    unsigned char handle[UF_GATEKEEPER_HANDLE_MAX];
    size_t handle_size;
};
int uf_credential_load(int directory, uint32_t uid, struct uf_credential *);
int uf_credential_create(int directory, const struct uf_credential *);
int uf_credential_derive(const char *password, size_t size, const unsigned char salt[16],
                         unsigned char secret[32]);
#endif
