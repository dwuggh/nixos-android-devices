/* SPDX-License-Identifier: MIT */
#include "protocol.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static uint32_t word(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int main(void)
{
    unsigned char h[64], body[288], hat[69];
    const struct {
        enum uf_command command;
        unsigned id;
        size_t size;
    } specs[] = {
        {UF_SET_GROUP, 0, 288},  {UF_PRE_ENROLL, 2, 40},   {UF_ENROLL, 3, 108},
        {UF_POST_ENROLL, 4, 28}, {UF_AUTHENTICATE, 1, 48}, {UF_ENUMERATE, 7, 112},
        {UF_REMOVE, 6, 120},     {UF_SAVE, 9, 36},         {UF_CANCEL, 8, 28},
        {UF_SCREEN, 11, 36},
    };
    for (unsigned i = 0; i < sizeof(specs) / sizeof(specs[0]); i++) {
        assert(!uf_prepare(specs[i].command, h, sizeof(h), body, specs[i].size, 1, 0, 0));
        assert(word(body + 4) == 1000 && word(body + 8) == specs[i].id &&
               word(h + 8) == specs[i].size);
    }
    assert(!uf_prepare(UF_SET_GROUP, h, 64, body, 288, 1, 0, 0));
    assert(!uf_group_encode(body, 288, 42, "/data/vendor/fingerprint/linux"));
    assert(word(body + 28) == 42 && !strcmp((char *)body + 32, "/data/vendor/fingerprint/linux"));
    assert(uf_group_encode(body, 287, 0, "/x") == -1);
    char long_path[257];
    memset(long_path, 'x', 256);
    long_path[256] = 0;
    assert(uf_group_encode(body, 288, 0, long_path) == -1);
    assert(!uf_prepare(UF_ENROLL, h, 64, body, 108, 1, 0, 0));
    for (unsigned i = 0; i < 69; i++)
        hat[i] = (i * 7 + 3) & 255;
    assert(!uf_enroll_encode(body, 108, 9, hat, 69));
    assert(word(body + 28) == 9 && body[32] == 0 && !memcmp(body + 33, hat, 69));
    for (unsigned i = 102; i < 108; i++)
        assert(!body[i]);
    assert(uf_enroll_encode(body, 108, 9, hat, 68) == -1);
    assert(!uf_enroll_encode(body, 108, 9, NULL, 0));
    for (unsigned i = 32; i < 108; i++)
        assert(!body[i]);
    assert(!uf_prepare(UF_ENUMERATE, h, 64, body, 112, 1, 0, 0));
    assert(!uf_wire_put32(body, 112, 28, 2));
    assert(!uf_wire_put32(body, 112, 32, 7) && !uf_wire_put32(body, 112, 36, 8));
    assert(!uf_wire_put32(body, 112, 72, 123) && !uf_wire_put32(body, 112, 76, 456));
    struct uf_template records[10];
    size_t count;
    assert(!uf_enumerate_decode(body, 112, records, 10, &count) && count == 2);
    assert(records[0].group == 7 && records[0].finger == 123);
    assert(uf_template_in_set((struct uf_template){8, 456}, records, 2));
    assert(!uf_template_in_set((struct uf_template){7, 456}, records, 2));
    assert(!uf_template_in_set((struct uf_template){8, 999}, records, 2));
    assert(uf_enumerate_decode(body, 112, records, 1, &count) == -1 && errno == ENOSPC &&
           count == 0);
    assert(!uf_wire_put32(body, 112, 36, 7) && !uf_wire_put32(body, 112, 76, 123));
    assert(uf_enumerate_decode(body, 112, records, 10, &count) == -1 && errno == EPROTO &&
           count == 0);
    assert(!uf_wire_put32(body, 112, 28, 11));
    assert(uf_enumerate_decode(body, 112, records, 10, &count) == -1 && count == 0);
    assert(uf_wire_put32(body, 112, SIZE_MAX, 0) == -1);
    assert(uf_wire_put64(body, 7, 0, 0) == -1);
    uint64_t value;
    assert(!uf_wire_put64(body, 112, 32, UINT64_C(0xfedcba9876543210)));
    assert(!uf_wire_get64(body, 112, 32, &value) && value == UINT64_C(0xfedcba9876543210));
    assert(uf_wire_get64(body, 112, SIZE_MAX, &value) == -1);
    unsigned char large[1024];
    assert(uf_profile_size(UF_PROFILE_GFUNI, UF_SENSOR_INIT) == 616);
    assert(uf_profile_size(UF_PROFILE_GFUNI6, UF_SENSOR_INIT) == 640);
    assert(uf_profile_size(UF_PROFILE_GFUNI, UF_CAPTURE) == 80);
    assert(uf_profile_size(UF_PROFILE_GFUNI6, UF_CAPTURE) == 88);
    assert(uf_profile_size(UF_PROFILE_GFUNI, UF_ENROLL_IMAGE) == 176);
    assert(uf_profile_size(UF_PROFILE_GFUNI6, UF_ENROLL_IMAGE) == 184);
    assert(!uf_profile_prepare(UF_PROFILE_GFUNI, UF_SENSOR_INIT, h, large, 616, 123, 456, 4194304));
    assert(word(h + 4) == 616 && word(h + 8) == 123 && word(h + 12) == 0 &&
           word(h + 36) == 4194304 && word(h + 40) == 0);
    assert(
        !uf_profile_prepare(UF_PROFILE_GFUNI6, UF_SENSOR_INIT, h, large, 640, 123, 456, 4194304));
    assert(word(h + 4) == 0 && word(h + 8) == 640 && word(h + 12) == 123 && word(h + 36) == 0 &&
           word(h + 40) == 4194304);
    assert(uf_profile_prepare(UF_PROFILE_GFUNI, UF_SENSOR_INIT, h, large, 640, 1, 0, 0) == -1);
    assert(!strcmp(uf_error_name(1059), "GF_ERROR_UNTRUSTED_ENROLL"));
    assert(!strcmp(uf_error_name(1142), "GF_ERROR_ENROLL_AUTH_TOKEN_TIME_OUT"));
    puts("actions: OEM command sizes, packed HAT, group path, enumeration bounds and "
         "request-gallery membership passed");
    return 0;
}
