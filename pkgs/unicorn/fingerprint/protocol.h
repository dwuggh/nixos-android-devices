/* SPDX-License-Identifier: MIT */
#ifndef UNICORN_FP_PROTOCOL_H
#define UNICORN_FP_PROTOCOL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum uf_command {
    UF_AUTHKEY,
    UF_WAKE,
    UF_SLEEP,
    UF_DETECTOR_INIT,
    UF_DETECTOR_INFO,
    UF_SET_GROUP,
    UF_PRE_ENROLL,
    UF_ENROLL,
    UF_POST_ENROLL,
    UF_AUTHENTICATE,
    UF_ENUMERATE,
    UF_REMOVE,
    UF_SAVE,
    UF_CANCEL,
    UF_SCREEN,
    UF_SENSOR_INIT,
    UF_ALGO_INIT,
    UF_CAPTURE,
    UF_READ_IMAGE,
    UF_IRQ,
    UF_SENSOR_IDS,
    UF_ENROLL_IMAGE,
    UF_AUTH_IMAGE,
    UF_ENROLL_END,
    UF_POST_AUTH,
    UF_ENROLL_FINISH,
    UF_CALI_ERROR,
    UF_COMMAND_COUNT
};
enum uf_profile { UF_PROFILE_GFUNI = 1, UF_PROFILE_GFUNI6 = 2 };
size_t uf_profile_size(enum uf_profile, enum uf_command);
int uf_profile_prepare(enum uf_profile, enum uf_command, void *header, void *body, size_t size,
                       uint32_t sequence, uint64_t milliseconds, uint32_t carveout_size);
const char *uf_error_name(int32_t);
#define UF_TEMPLATE_MAX 10U
#define UF_HAT_SIZE 69U
struct uf_template {
    uint32_t group, finger;
};
struct uf_status {
    int32_t header, command;
    bool reset_requested;
};
/* Codec only: no runtime TA profile is qualified yet. Exact wire sizes from
 * pinned libgf_hal6.so; these functions do not authorize sending commands. */
size_t uf_command_size(enum uf_command);
int uf_prepare(enum uf_command, void *header, size_t header_size, void *body, size_t body_size,
               uint32_t sequence, uint64_t milliseconds, uint32_t carveout_size);
int uf_status_decode(const void *header, size_t header_size, const void *body, size_t body_size,
                     struct uf_status *);
void uf_keymaster_request(unsigned char request[64]);
/* OEM checks only the leading status and copies the entire 256-byte reply.
 * An offset/length interpretation of words 1 and 2 has NOT been established. */
int uf_keymaster_status(const void *reply, size_t size, int32_t *status);
/* Field access is explicitly little endian; no host-ABI structure is put on
 * the wire. All ranges use subtraction checks before addition. */
int uf_wire_put32(void *, size_t size, size_t offset, uint32_t);
int uf_wire_put64(void *, size_t size, size_t offset, uint64_t);
int uf_wire_get32(const void *, size_t size, size_t offset, uint32_t *);
int uf_wire_get64(const void *, size_t size, size_t offset, uint64_t *);
int uf_group_encode(void *, size_t size, uint32_t group, const char *ta_path);
/* NULL HAT reproduces the OEM's absent-token branch. It does not create or
 * sign a token; a TA rejecting that branch must report authorization required. */
int uf_enroll_encode(void *, size_t size, uint32_t group, const uint8_t *hat, size_t hat_size);
int uf_enumerate_decode(const void *, size_t size, struct uf_template *, size_t capacity,
                        size_t *count);
int uf_template_in_set(struct uf_template, const struct uf_template *, size_t count);
#endif
