/* SPDX-License-Identifier: MIT */
#include "gatekeeper.h"
#include "protocol.h"
#include <errno.h>
#include <string.h>

#define GK_SHARED_SIZE 0xa000U
static int exchange(struct uf_gatekeeper *g, size_t length, size_t response_offset,
                     struct uf_tee_result *result)
{
    struct uf_ref request = {&g->buffer, 0, length};
    struct uf_ref response = {&g->buffer, response_offset, GK_SHARED_SIZE - response_offset};
    uf_wire_get32(g->buffer.data, GK_SHARED_SIZE, 0, &g->command);
    g->request_bytes = length;
    g->response_offset = response_offset;
    g->status = 0;
    g->retry_ms = 0;
    if (g->broken) {
        errno = EIO;
        return -1;
    }
    if (uf_tee_invoke(&g->tee, request, response, NULL, 0, result)) {
        g->broken = true;
        return -1;
    }
    uint32_t status;
    if (uf_wire_get32(g->buffer.data, GK_SHARED_SIZE, response_offset, &status))
        return -1;
    g->status = status <= INT32_MAX ? (int32_t)status : -1 - (int32_t)(UINT32_MAX - status);
    if (g->status) {
        g->retry_ms = g->status > 0 ? (uint32_t)g->status : 0;
        errno = g->retry_ms ? EAGAIN : EREMOTEIO;
        return -1;
    }
    return 0;
}
int uf_gatekeeper_close(struct uf_gatekeeper *g)
{
    if (!g) {
        errno = EINVAL;
        return -1;
    }
    uf_tee_free(&g->buffer);
    return uf_tee_close(&g->tee);
}
int uf_gatekeeper_open(struct uf_gatekeeper *g, struct uf_tee_result *result)
{
    if (!g || !result || g->tee.fd >= 0 || g->buffer.data) {
        errno = EINVAL;
        return -1;
    }
    *g = (struct uf_gatekeeper)UF_GATEKEEPER_CLOSED;
    if (uf_tee_open(&g->tee, "keymaster64", result) ||
        uf_tee_alloc(&g->tee, &g->buffer, GK_SHARED_SIZE))
        goto fail;
    memset(g->buffer.data, 0, GK_SHARED_SIZE);
    uf_wire_put32(g->buffer.data, GK_SHARED_SIZE, 0, 0x200);
    /* libkeymasterdeviceutils.so 0x5350: response follows the four-byte
     * request, not the 64-byte boundary used for the Goodix auth-key call. */
    if (exchange(g, 4, 4, result) ||
        uf_wire_get32(g->buffer.data, GK_SHARED_SIZE, 8, &g->api_major) ||
        uf_wire_get32(g->buffer.data, GK_SHARED_SIZE, 12, &g->api_minor) ||
        uf_wire_get32(g->buffer.data, GK_SHARED_SIZE, 16, &g->ta_major) ||
        uf_wire_get32(g->buffer.data, GK_SHARED_SIZE, 20, &g->ta_minor))
        goto fail;
    /* Exact six-word client negotiation at OEM constructor 0x5458.
     * The first four words are literal rodata at file offset 0x2e00. */
    static const uint32_t version[] = {0x207, 4, 5, 4, 5, 0};
    memset(g->buffer.data, 0, GK_SHARED_SIZE);
    for (unsigned i = 0; i < sizeof(version) / sizeof(version[0]); i++)
        uf_wire_put32(g->buffer.data, GK_SHARED_SIZE, 4 * i, version[i]);
    if (exchange(g, sizeof(version), sizeof(version), result))
        goto fail;
    return 0;
fail:;
    int saved = errno;
    uf_gatekeeper_close(g);
    errno = saved;
    return -1;
}

struct encoder {
    unsigned char *data;
    size_t size, offset;
};
static int encode(struct encoder *e, unsigned major, uint64_t value)
{
    unsigned count = value < 24 ? 0 : value <= UINT8_MAX ? 1 : value <= UINT16_MAX ? 2
                                       : value <= UINT32_MAX ? 4 : 8;
    if (e->offset > e->size || 1U + count > e->size - e->offset) {
        errno = EOVERFLOW;
        return -1;
    }
    e->data[e->offset++] = (major << 5) |
        (count == 0 ? (unsigned)value : count == 1 ? 24 : count == 2 ? 25 : count == 4 ? 26 : 27);
    for (unsigned i = count; i; i--)
        e->data[e->offset++] = value >> ((i - 1) * 8);
    return 0;
}
static int bytes(struct encoder *e, const unsigned char *data, size_t size)
{
    if (encode(e, 2, size))
        return -1;
    if (size > e->size - e->offset) {
        errno = EOVERFLOW;
        return -1;
    }
    memcpy(e->data + e->offset, data, size);
    e->offset += size;
    return 0;
}
struct decoder {
    const unsigned char *data;
    size_t size, offset;
};
static int decode(struct decoder *d, unsigned major, uint64_t *value)
{
    if (d->offset >= d->size)
        goto invalid;
    unsigned head = d->data[d->offset++], ai = head & 31;
    if (head >> 5 != major || ai > 27)
        goto invalid;
    if (ai < 24) {
        *value = ai;
        return 0;
    }
    unsigned count = 1U << (ai - 24);
    if (count > d->size - d->offset)
        goto invalid;
    uint64_t n = 0;
    for (unsigned i = 0; i < count; i++)
        n = n << 8 | d->data[d->offset++];
    *value = n;
    return 0;
invalid:
    errno = EPROTO;
    return -1;
}
static int response(struct uf_gatekeeper *g, struct encoder *e,
                     struct decoder *d, struct uf_tee_result *result)
{
    size_t length = 4 + e->offset;
    size_t aligned = (length + 3) & ~(size_t)3;
    if (aligned > GK_SHARED_SIZE - 8) {
        errno = EOVERFLOW;
        return -1;
    }
    /* OEM sendCmd at 0x583c sends the exact encoded length in x2; only
     * the response pointer in x3 is aligned. Padding is not CBOR input. */
    if (exchange(g, length, aligned, result))
        return -1;
    uint32_t size;
    if (uf_wire_get32(g->buffer.data, GK_SHARED_SIZE, aligned + 4, &size))
        return -1;
    if (!size || size > GK_SHARED_SIZE - aligned - 8) {
        g->broken = true;
        errno = EPROTO;
        return -1;
    }
    *d = (struct decoder){(unsigned char *)g->buffer.data + aligned + 8, size, 0};
    return 0;
}
static int credential_request(struct uf_gatekeeper *g, uint32_t uid,
                               const unsigned char *password, struct uf_tee_result *result)
{
    if (!g || !password || !result || g->tee.fd < 0 || !g->buffer.data ||
        g->buffer.size != GK_SHARED_SIZE || (uid & UINT32_C(0xffff0000)) != UF_GATEKEEPER_UID_BASE) {
        errno = EINVAL;
        return -1;
    }
    /* The observed CBOR client is selected by the OEM only for nonlegacy
     * Keymaster API versions, and uses the new HAT parser from TA 4.540. */
    if (g->api_major != 4 || g->api_minor != 1 || g->ta_major != 4 || g->ta_minor < 540) {
        errno = EPROTONOSUPPORT;
        return -1;
    }
    if (g->broken) {
        errno = EIO;
        return -1;
    }
    memset(g->buffer.data, 0, g->buffer.size);
    return 0;
}
int uf_gatekeeper_enroll(struct uf_gatekeeper *g, uint32_t uid,
                         const unsigned char password[32], unsigned char *handle,
                         size_t *handle_size, struct uf_tee_result *result)
{
    if (!handle_size || !handle || *handle_size < UF_GATEKEEPER_HANDLE_MAX) {
        errno = EINVAL;
        return -1;
    }
    *handle_size = 0;
    memset(handle, 0, UF_GATEKEEPER_HANDLE_MAX);
    if (credential_request(g, uid, password, result))
        return -1;
    uf_wire_put32(g->buffer.data, GK_SHARED_SIZE, 0, 0x21001);
    struct encoder e = {(unsigned char *)g->buffer.data + 4, GK_SHARED_SIZE - 12, 0};
    struct decoder d;
    uint64_t count, key, size;
    int rc = -1;
    if (encode(&e, 5, 2) || encode(&e, 0, 3) || encode(&e, 0, uid) ||
        encode(&e, 0, 8) || bytes(&e, password, 32) || response(g, &e, &d, result))
        goto out;
    if (decode(&d, 5, &count) || count != 1 || decode(&d, 0, &key) || key != 6 ||
        decode(&d, 2, &size) || size < 9 || size > UF_GATEKEEPER_HANDLE_MAX ||
        size != d.size - d.offset) {
        g->broken = true;
        errno = EPROTO;
        goto out;
    }
    memcpy(handle, d.data + d.offset, size);
    *handle_size = size;
    rc = 0;
out:
    uf_wipe(g->buffer.data, g->buffer.size);
    return rc;
}
int uf_gatekeeper_verify(struct uf_gatekeeper *g, uint32_t uid, uint64_t challenge,
                         const unsigned char password[32], const unsigned char *handle,
                         size_t handle_size, unsigned char hat[69], struct uf_tee_result *result)
{
    if (!hat) {
        errno = EINVAL;
        return -1;
    }
    memset(hat, 0, 69);
    if (!challenge || !handle || handle_size < 9 || handle_size > UF_GATEKEEPER_HANDLE_MAX) {
        errno = EINVAL;
        return -1;
    }
    if (credential_request(g, uid, password, result))
        return -1;
    uf_wire_put32(g->buffer.data, GK_SHARED_SIZE, 0, 0x21002);
    struct encoder e = {(unsigned char *)g->buffer.data + 4, GK_SHARED_SIZE - 12, 0};
    struct decoder d;
    uint64_t count, key, fields[17] = {0};
    const unsigned char *mac = NULL;
    unsigned seen = 0;
    int rc = -1;
    if (encode(&e, 5, 4) || encode(&e, 0, 3) || encode(&e, 0, uid) ||
        encode(&e, 0, 4) || encode(&e, 0, challenge) || encode(&e, 0, 6) ||
        bytes(&e, handle, handle_size) || encode(&e, 0, 9) || bytes(&e, password, 32) ||
        response(g, &e, &d, result))
        goto out;
    if (decode(&d, 5, &count) || count != 7)
        goto invalid;
    for (unsigned i = 0; i < 7; i++) {
        if (decode(&d, 0, &key) || (key != 4 && (key < 11 || key > 16)) ||
            (seen & (1U << key)))
            goto invalid;
        seen |= 1U << key;
        if (key == 16) {
            uint64_t size;
            if (decode(&d, 2, &size) || size != 32 || size > d.size - d.offset)
                goto invalid;
            mac = d.data + d.offset;
            d.offset += size;
        } else if (decode(&d, 0, &fields[key]))
            goto invalid;
    }
    uint64_t sid;
    if (uf_wire_get64(handle, handle_size, 1, &sid) || !sid || fields[12] != sid ||
        fields[11] != 0 || fields[4] != challenge || fields[14] != UINT32_C(0x01000000) ||
        !fields[15] || !mac || d.offset != d.size)
        goto invalid;
    /* OEM deserializeClientVerify copies integer storage directly. Type and
     * timestamp are already represented as the packed HAT's network fields. */
    uf_wire_put64(hat, 69, 1, fields[4]);
    uf_wire_put64(hat, 69, 9, fields[12]);
    uf_wire_put64(hat, 69, 17, fields[13]);
    uf_wire_put32(hat, 69, 25, (uint32_t)fields[14]);
    uf_wire_put64(hat, 69, 29, fields[15]);
    memcpy(hat + 37, mac, 32);
    rc = 0;
    goto out;
invalid:
    g->broken = true;
    errno = EPROTO;
out:
    uf_wipe(g->buffer.data, g->buffer.size);
    return rc;
}
