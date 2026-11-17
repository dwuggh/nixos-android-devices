/* SPDX-License-Identifier: MIT
 * Real resident-client handshake with only the TEE boundary replaced. */
#include "gatekeeper.h"
#include "protocol.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned failure, calls, allocations, sessions;
static unsigned credential_calls, response_mode;
static size_t truncate_response = SIZE_MAX;
static const unsigned char secret[32] = {[0 ... 31] = 0x52};
static const unsigned char password_handle[58] = {
    2, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
void uf_wipe(void *data, size_t size)
{
    memset(data, 0, size);
}
int uf_tee_open(struct uf_tee *tee, const char *name, struct uf_tee_result *result)
{
    assert(!strcmp(name, "keymaster64") && tee->fd == -1 && result);
    if (failure == 1) {
        errno = ENOENT;
        return -1;
    }
    tee->fd = 40;
    tee->session = 7;
    sessions++;
    return 0;
}
int uf_tee_alloc(struct uf_tee *tee, struct uf_buffer *b, size_t size)
{
    assert(tee->fd == 40 && size == 40960 && !b->data);
    if (failure == 2) {
        errno = ENOMEM;
        return -1;
    }
    b->data = malloc(size);
    assert(b->data);
    memset(b->data, 0xaa, size);
    b->size = size;
    allocations++;
    return 0;
}
void uf_tee_free(struct uf_buffer *b)
{
    if (b->data) {
        memset(b->data, 0, b->size);
        free(b->data);
        allocations--;
        *b = (struct uf_buffer){0};
    }
}
int uf_tee_close(struct uf_tee *tee)
{
    if (tee->fd >= 0) {
        assert(tee->fd == 40 && sessions == 1);
        sessions--;
        tee->fd = -1;
        if (failure == 7) {
            errno = EIO;
            return -1;
        }
    }
    return 0;
}
int uf_tee_invoke(struct uf_tee *tee, struct uf_ref q, struct uf_ref r,
                  const struct uf_patch *patches, size_t count, struct uf_tee_result *result)
{
    assert(tee->fd == 40 && q.buffer == r.buffer && !q.offset && !patches && !count && result);
    unsigned char *data = q.buffer->data;
    uint32_t command;
    assert(!uf_wire_get32(data, q.buffer->size, 0, &command));
    if (command == 0x21001 || command == 0x21002) {
        credential_calls++;
        assert(!failure && r.length + r.offset == 40960);
        static const unsigned char enroll[] = {
            1, 0x10, 2, 0, 0xa2, 3, 0x1a, 0x55, 0x46, 0, 0x2a, 8, 0x58, 0x20};
        static const unsigned char verify[] = {
            2, 0x10, 2, 0, 0xa4, 3, 0x1a, 0x55, 0x46, 0, 0x2a,
            4, 0x1b, 1, 2, 3, 4, 5, 6, 7, 8, 6, 0x58, 58};
        size_t offset;
        if (command == 0x21001) {
            assert(!memcmp(data, enroll, sizeof(enroll)));
            offset = sizeof(enroll);
        } else {
            assert(!memcmp(data, verify, sizeof(verify)));
            offset = sizeof(verify);
            assert(!memcmp(data + offset, password_handle, 58));
            offset += 58;
            assert(!memcmp(data + offset, "\x09\x58\x20", 3));
            offset += 3;
        }
        assert(!memcmp(data + offset, secret, 32));
        offset += 32;
        /* Literal lengths from OEM sendCmd's x2 argument, distinct from x3.
         * These non-word-sized messages catch accidental CBOR padding. */
        assert(q.length == offset);
        assert(q.length == (command == 0x21001 ? 46U : 117U));
        assert(r.offset == (command == 0x21001 ? 48U : 120U));
        for (size_t i = offset; i < q.buffer->size; i++)
            assert(!data[i]);
        if (response_mode == 1 || response_mode == 2) {
            uf_wire_put32(data, q.buffer->size, r.offset,
                          response_mode == 1 ? (uint32_t)-30 : 5000);
            return 0;
        }
        if (response_mode == 3) {
            errno = EINTR;
            return -1;
        }
        if (command == 0x21001) {
            unsigned char *out = data + r.offset + 8;
            out[0] = 0xa1;
            out[1] = 6;
            out[2] = 0x58;
            out[3] = 58;
            memcpy(out + 4, password_handle, 58);
            uf_wire_put32(data, q.buffer->size, r.offset + 4, 62);
        } else {
            /* Literal OEM field labels and independently specified byte
             * order. Synthetic MAC bytes are a codec fixture, not a HAT. */
            static const unsigned char payload[] = {
                0xa7, 11, 0, 4, 0x1b, 1, 2, 3, 4, 5, 6, 7, 8,
                12, 0x1b, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                13, 0, 14, 0x1a, 1, 0, 0, 0,
                15, 0x1b, 0xe8, 3, 0, 0, 0, 0, 0, 0,
                16, 0x58, 32,
                0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
                0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
                0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
                0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf};
            unsigned char *out = data + r.offset + 8;
            memcpy(out, payload, sizeof(payload));
            if (response_mode == 4) out[2] = 1; /* Unsupported HAT version. */
            if (response_mode == 5) out[12] ^= 1; /* Different challenge. */
            if (response_mode == 6) out[22] ^= 1; /* Different secure user. */
            if (response_mode == 7) out[13] = 4; /* Duplicate key. */
            if (response_mode == 8) out[43] = 31; /* Short signature. */
            if (response_mode == 9) out[25] = 17; /* Unknown key. */
            if (response_mode == 10) out[29] = 1; /* Wrong authenticator type. */
            size_t length = truncate_response < sizeof(payload) ? truncate_response : sizeof(payload);
            uf_wire_put32(data, q.buffer->size, r.offset + 4,
                          response_mode == 11 ? UINT32_MAX : (uint32_t)length);
            if (response_mode == 12) {
                out[length] = 0;
                uf_wire_put32(data, q.buffer->size, r.offset + 4, length + 1);
            }
        }
        return 0;
    }
    calls++;
    if (calls == 1) {
        static const unsigned char request[] = {0, 2, 0, 0};
        assert(q.length == sizeof(request) && r.offset == 4 && r.length == 40956);
        assert(!memcmp(data, request, sizeof(request)));
    } else {
        static const unsigned char request[] = {
            7, 2, 0, 0, 4, 0, 0, 0, 5, 0, 0, 0,
            4, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0};
        assert(calls == 2 && q.length == sizeof(request) && r.offset == 24 && r.length == 40936);
        assert(!memcmp(data, request, sizeof(request)));
    }
    for (size_t i = q.length; i < q.buffer->size; i++)
        assert(!data[i]);
    if ((failure == 3 && calls == 1) || (failure == 5 && calls == 2)) {
        errno = EINTR; /* Result uncertain: never replay. */
        return -1;
    }
    if (failure == 4 || failure == 6) {
        if (calls == (failure == 4 ? 1U : 2U)) {
            uf_wire_put32(data, q.buffer->size, r.offset, (uint32_t)-38);
            return 0;
        }
    }
    if (calls == 1) {
        const unsigned values[] = {0, 4, 1, 4, 558};
        for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); i++)
            uf_wire_put32(data, q.buffer->size, r.offset + 4 * i, values[i]);
    }
    return 0;
}
int main(void)
{
    for (failure = 0; failure <= 7; failure++) {
        calls = 0;
        struct uf_gatekeeper g = UF_GATEKEEPER_CLOSED;
        struct uf_tee_result result = {0};
        int rc = uf_gatekeeper_open(&g, &result);
        if (!failure || failure == 7) {
            assert(!rc && calls == 2 && g.api_major == 4 && g.api_minor == 1 &&
                   g.ta_major == 4 && g.ta_minor == 558 && !g.status);
            assert(uf_gatekeeper_open(&g, &result) == -1 && errno == EINVAL);
            if (failure == 7)
                assert(uf_gatekeeper_close(&g) == -1 && errno == EIO);
            else
                assert(!uf_gatekeeper_close(&g));
        } else {
            assert(rc == -1);
            if (failure == 3 || failure == 5)
                assert(errno == EINTR && calls == (failure == 3 ? 1U : 2U));
            if (failure == 4 || failure == 6)
                assert(errno == EREMOTEIO && g.status == -38);
        }
        assert(!allocations && !sessions && !g.buffer.data && g.tee.fd == -1);
        assert(!uf_gatekeeper_close(&g));
    }
    struct uf_tee_result result = {0};
    assert(uf_gatekeeper_open(NULL, &result) == -1 && errno == EINVAL);
    assert(uf_gatekeeper_close(NULL) == -1 && errno == EINVAL);
    unsigned char handle[UF_GATEKEEPER_HANDLE_MAX], hat[69];
    for (response_mode = 0; response_mode <= 3; response_mode++) {
        failure = calls = credential_calls = 0;
        struct uf_gatekeeper g = UF_GATEKEEPER_CLOSED;
        assert(!uf_gatekeeper_open(&g, &result));
        size_t size = sizeof(handle);
        int rc = uf_gatekeeper_enroll(&g, 0x5546002a, secret, handle, &size, &result);
        if (!response_mode) {
            assert(!rc && size == 58 && !memcmp(handle, password_handle, 58));
            assert(!uf_gatekeeper_verify(&g, 0x5546002a, UINT64_C(0x0102030405060708),
                                         secret, handle, size, hat, &result));
            static const unsigned char expected[] = {
                0, 8, 7, 6, 5, 4, 3, 2, 1, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 3, 0xe8};
            assert(!memcmp(hat, expected, sizeof(expected)));
            for (unsigned i = 0; i < 32; i++) assert(hat[37 + i] == 0xa0 + i);
            assert(credential_calls == 2);
        } else {
            assert(rc == -1 && !size && credential_calls == 1);
            assert(errno == (response_mode == 1 ? EREMOTEIO : response_mode == 2 ? EAGAIN : EINTR));
            if (response_mode == 2) assert(g.retry_ms == 5000);
            for (unsigned i = 0; i < sizeof(handle); i++) assert(!handle[i]);
        }
        for (size_t i = 0; i < g.buffer.size; i++) assert(!((unsigned char *)g.buffer.data)[i]);
        assert(!uf_gatekeeper_close(&g));
    }
    for (response_mode = 0; response_mode <= 12; response_mode++) {
        for (size_t length = 0; length <= 76; length++) {
            failure = calls = credential_calls = 0;
            truncate_response = response_mode ? SIZE_MAX : length;
            struct uf_gatekeeper g = UF_GATEKEEPER_CLOSED;
            assert(!uf_gatekeeper_open(&g, &result));
            memset(hat, 0xff, sizeof(hat));
            int rc = uf_gatekeeper_verify(&g, 0x5546002a, UINT64_C(0x0102030405060708),
                                          secret, password_handle, 58, hat, &result);
            if (!response_mode && length >= 76)
                assert(!rc);
            else {
                assert(rc == -1 && credential_calls == 1);
                for (unsigned i = 0; i < sizeof(hat); i++) assert(!hat[i]);
            }
            assert(!uf_gatekeeper_close(&g));
            if (response_mode) break;
        }
    }
    failure = calls = credential_calls = 0;
    struct uf_gatekeeper g = UF_GATEKEEPER_CLOSED;
    assert(!uf_gatekeeper_open(&g, &result));
    size_t size = sizeof(handle);
    assert(uf_gatekeeper_enroll(&g, 0, secret, handle, &size, &result) == -1 && errno == EINVAL);
    assert(!credential_calls);
    assert(!uf_gatekeeper_close(&g) && !allocations && !sessions);
    puts("Gatekeeper: OEM version exchange, framing, signed errors, no replay and cleanup passed");
}
