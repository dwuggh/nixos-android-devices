/* SPDX-License-Identifier: MIT */
#include "protocol.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    unsigned char h[64], b[284], keymaster[64];
    /* Literal OEM constants and offsets, independently recorded in protocol.md. */
    const unsigned char km_prefix[] = {0x05, 0x02, 0, 0, 2, 0, 0, 0};
    uf_keymaster_request(keymaster);
    assert(!memcmp(keymaster, km_prefix, 8));
    for (unsigned i = 8; i < 64; i++)
        assert(!keymaster[i]);
    const struct {
        enum uf_command command;
        size_t size;
        unsigned id;
    } cases[] = {{UF_AUTHKEY, 284, 10},
                 {UF_WAKE, 32, 9},
                 {UF_SLEEP, 28, 8},
                 {UF_DETECTOR_INIT, 28, 0},
                 {UF_DETECTOR_INFO, 44, 1},
                 {UF_CALI_ERROR, 28, 103}};
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        assert(!uf_prepare(cases[i].command, h, 64, b, cases[i].size, 0x12345678, 0x123456, 4096));
        assert(h[8] == (cases[i].size & 255) && h[9] == (cases[i].size >> 8));
        assert(!memcmp(h + 12, "\x78\x56\x34\x12\x56\x34\x12\0", 8));
        for (unsigned j = 0; j < 8; j++)
            assert(!h[j]);
        assert(h[40] == 0 && h[41] == 0x10);
        for (unsigned j = 32; j < 40; j++)
            assert(!h[j]);
        unsigned char module[] = {0xea, 3, 0, 0};
        if (cases[i].command == UF_AUTHKEY)
            module[0] = 0xe8;
        assert(!memcmp(b + 4, module, 4) && b[8] == cases[i].id);
        struct uf_status status;
        assert(!uf_status_decode(h, 64, b, cases[i].size, &status));
        assert(!status.header && !status.command && !status.reset_requested);
        memset(h + 28, 0xff, 4);
        b[12] = 0x23;
        b[13] = 4;
        b[0] = 1;
        assert(!uf_status_decode(h, 64, b, cases[i].size, &status));
        assert(status.header == -1 && status.command == 1059 && status.reset_requested);
    }
    assert(uf_prepare(UF_WAKE, h, 63, b, 32, 1, 0, 0) == -1 && errno == EINVAL);
    assert(uf_prepare(UF_WAKE, h, 64, b, 31, 1, 0, 0) == -1);
    assert(uf_prepare(UF_WAKE, h, 64, b, 32, 0, 0, 0) == -1);
    assert(uf_prepare(UF_WAKE, h, 64, b, 32, 1, 86400000, 0) == -1);
    assert(uf_prepare((enum uf_command)100, h, 64, b, 32, 1, 0, 0) == -1);
    struct uf_status status;
    assert(uf_status_decode(h, 64, b, 27, &status) == -1 && errno == EPROTO);
    assert(uf_status_decode(NULL, 64, b, 28, &status) == -1);
    int32_t code;
    memset(b, 0, sizeof(b));
    /* No invented offset/length schema: opaque words 1/2 may be arbitrary. */
    memset(b + 4, 0xff, 8);
    assert(!uf_keymaster_status(b, 256, &code) && code == 0);
    memset(b, 0xff, 4);
    assert(!uf_keymaster_status(b, 256, &code) && code == -1);
    assert(uf_keymaster_status(b, 255, &code) == -1 && errno == EPROTO);
    assert(uf_keymaster_status(b, 256, NULL) == -1);
    puts("OEM codec: fixed command constants, sizes, status separation and opaque Keymaster reply "
         "passed");
    return 0;
}
