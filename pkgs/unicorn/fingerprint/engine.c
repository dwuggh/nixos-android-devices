/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "display.h"
#include "authorization.h"
#include "dsp.h"
#include "ipc.h"
#include "sensor.h"
#include "tee.h"
#include "touch.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <systemd/sd-id128.h>
#include <time.h>
#include <unistd.h>
#define PRIVATE_SIZE (4U * 1024 * 1024)
struct engine {
    struct uf_tee tee;
    struct uf_buffer header, body;
    struct uf_sensor sensor;
    struct uf_dsp *dsp;
    struct uf_touch touch;
    struct uf_display display;
    enum uf_profile profile;
    unsigned char config[440];
    unsigned char sensor_info[16];
    char identity[80];
    uint32_t sequence, stages, cali, kind;
    int dma, channel, selector;
    int32_t code;
    bool awake, initialized, enroll_fake_check, auth_no_retry, broken;
};
static uint32_t get(const void *b, size_t offset)
{
    uint32_t value = 0;
    uf_wire_get32(b, 1024, offset, &value);
    return value;
}
static void put(void *b, size_t offset, uint32_t value)
{
    uf_wire_put32(b, 1024, offset, value);
}
static void put64(void *b, size_t offset, uint64_t value)
{
    uf_wire_put64(b, 1024, offset, value);
}
static int64_t monotonic(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t))
        return -1;
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static int prepared(struct engine *e, enum uf_command command)
{
    if (e->broken) {
        errno = EIO;
        return -1;
    }
    struct timespec t;
    struct tm tm;
    if (clock_gettime(CLOCK_REALTIME, &t) || !localtime_r(&t.tv_sec, &tm))
        return -1;
    uint64_t ms = (tm.tm_hour * 3600U + tm.tm_min * 60U + tm.tm_sec) * 1000U + t.tv_nsec / 1000000;
    if (e->sequence == UINT32_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    return uf_profile_prepare(e->profile, command, e->header.data, e->body.data,
                              uf_profile_size(e->profile, command), ++e->sequence, ms,
                              e->dma >= 0 ? PRIVATE_SIZE : 0);
}
static int invoke(struct engine *e, enum uf_command command)
{
    size_t length = uf_profile_size(e->profile, command);
    struct uf_ref header = {&e->header, 0, 64};
    struct uf_patch body = {0, 4, {&e->body, 0, length}};
    struct uf_tee_result tr = {0};
    e->code = 0;
    int64_t start = monotonic();
    fprintf(stderr, "event=ta_begin profile=%u command=%u\n", e->profile, command);
    int rc = e->dma >= 0 ? uf_tee_invoke_private(&e->tee, header, header, &body, 1, 32, &tr)
                         : uf_tee_invoke(&e->tee, header, header, &body, 1, &tr);
    int transport_errno = rc ? errno : 0;
    fprintf(stderr, "event=ta_end profile=%u command=%u errno=%d tee_code=%u elapsed_ms=%lld\n",
            e->profile, command, transport_errno, tr.code, (long long)(monotonic() - start));
    if (rc) {
        errno = transport_errno;
        e->broken = true;
        return -1;
    }
    struct uf_status s;
    if (uf_status_decode(e->header.data, 64, e->body.data, length, &s)) {
        e->broken = true;
        return -1;
    }
    e->code = s.header ? s.header : s.command;
    fprintf(stderr, "event=ta_status profile=%u command=%u code=%d reset=%u\n",
            e->profile, command, e->code, s.reset_requested);
    if (s.reset_requested) {
        /* A reset flag may accompany a retry/no-match status. Do not let that
         * status turn a broken secure session into another capture attempt. */
        e->code = 0;
        e->broken = true;
        errno = EIO;
        return -1;
    } /* Never retry a mutating request. */
    if (e->code) {
        errno = EREMOTEIO;
        return -1;
    }
    return 0;
}
static int simple(struct engine *e, enum uf_command command)
{
    if (prepared(e, command))
        return -1;
    return invoke(e, command);
}
static int wake(struct engine *e)
{
    if (e->awake)
        return 0;
    if (e->kind == 0x10001 && ioctl(e->sensor.fd, UF_RESET))
        return -1;
    if (simple(e, UF_WAKE))
        return -1;
    e->awake = true;
    return 0;
}
static int sleep_sensor(struct engine *e)
{
    if (!e->awake)
        return 0;
    int rc = simple(e, UF_SLEEP);
    if (!rc)
        e->awake = false;
    return rc;
}
static int key(struct engine *e)
{
    struct uf_tee km = UF_TEE_CLOSED;
    struct uf_buffer b = {0};
    struct uf_tee_result r;
    int rc = -1;
    if (uf_tee_open(&km, "keymaster64", &r) || uf_tee_alloc(&km, &b, 320))
        goto out;
    uf_keymaster_request(b.data);
    if (uf_tee_invoke(&km, (struct uf_ref){&b, 0, 64}, (struct uf_ref){&b, 64, 256}, NULL, 0, &r))
        goto out;
    int32_t status;
    if (uf_keymaster_status((char *)b.data + 64, 256, &status))
        goto out;
    if (status) {
        e->code = status;
        errno = EKEYREJECTED;
        goto out;
    }
    if (prepared(e, UF_AUTHKEY))
        goto out;
    memcpy((char *)e->body.data + 28, (char *)b.data + 64, 256);
    rc = invoke(e, UF_AUTHKEY);
out:;
    int saved = errno;
    uf_tee_free(&b);
    uf_tee_close(&km);
    errno = saved;
    return rc;
}
static struct engine empty(void)
{
    return (struct engine){.tee = UF_TEE_CLOSED,
                           .sensor = UF_SENSOR_CLOSED,
                           .touch = UF_TOUCH_CLOSED,
                           .display = UF_DISPLAY_CLOSED,
                           .dma = -1,
                           .channel = -1,
                           .selector = -1};
}
static int finish(struct engine *e)
{
    int saved = errno, rc = 0;
    uf_display_close(&e->display);
    uf_touch_close(&e->touch);
    if (e->initialized && !e->broken) {
        if (simple(e, UF_CANCEL))
            rc = -1;
        if (simple(e, UF_POST_ENROLL))
            rc = -1;
        if (sleep_sensor(e))
            rc = -1;
    }
    if (uf_dsp_close(e->dsp))
        rc = -1;
    e->dsp = NULL;
    uf_tee_free(&e->body);
    uf_tee_free(&e->header);
    if (uf_tee_close(&e->tee)) /* TA context drops its DMA reference after sessions. */
        rc = -1;
    if (e->dma >= 0)
        close(e->dma);
    uf_sensor_close(&e->sensor);
    if (e->selector >= 0)
        close(e->selector);
    fprintf(stderr, "event=engine_release profile=%u result=%d\n", e->profile, rc);
    uf_wipe(e, sizeof(*e));
    *e = empty();
    errno = saved;
    return rc;
}
static int detector(struct engine *e, const char *name)
{
    if (!strcmp(name, "gfuni"))
        e->profile = UF_PROFILE_GFUNI;
    else if (!strcmp(name, "gfuni6"))
        e->profile = UF_PROFILE_GFUNI6;
    else {
        errno = EINVAL;
        return -1;
    }
    struct uf_tee_result r;
    if (uf_sensor_open(&e->sensor) || ioctl(e->sensor.fd, UF_RESET) ||
        uf_tee_open(&e->tee, name, &r) || uf_tee_alloc(&e->tee, &e->header, 64) ||
        uf_tee_alloc(&e->tee, &e->body, 1024) || key(e))
        return -1;
    e->dma = uf_tee_private_alloc(&e->tee);
    if (e->dma < 0)
        return -1;
    if (wake(e))
        return -1;
    const enum uf_command commands[] = {UF_DETECTOR_INIT, UF_DETECTOR_INFO};
    for (unsigned i = 0; i < 2; i++) {
        unsigned tries = 0;
        while (simple(e, commands[i])) {
            if (e->code != 1112 || tries++ >= 3)
                return -1;
            if (ioctl(e->sensor.fd, UF_RESET))
                return -1;
        }
    }
    memcpy(e->sensor_info, (char *)e->body.data + 28, 16);
    uint32_t type = get(e->sensor_info, 0), chip = get(e->sensor_info, 4) & 0xfff;
    /* This port implements the Delmar optical protocol in the pinned OEM HALs.
     * A zero/sentinel or unknown sensor response must never select a profile. */
    if (type != 3 || (chip != 1 && chip != 3 && chip != 0x103)) {
        errno = ENOTSUP;
        return -1;
    }
    return 0;
}
int uf_probe(const char *name, struct uf_message *result)
{
    struct engine e = empty();
    int rc = detector(&e, name), saved = errno;
    if (result) {
        memset(result, 0, sizeof(*result));
        result->profile = e.profile;
        result->code = e.code;
        result->error = rc ? saved : 0;
        for (unsigned i = 0; i < 16; i++)
            snprintf(result->device + 2 * i, 3, "%02x", e.sensor_info[i]);
    }
    sleep_sensor(&e);
    finish(&e);
    errno = saved;
    return rc;
}
static int read_private(const char *name, void *data, size_t maximum, size_t *size)
{
    *size = 0;
    int root = open("/var/lib/unicorn-fingerprint/device",
                    O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (root < 0) {
        if (errno == ENOENT)
            return 0;
        return -1;
    }
    int fd = openat(root, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    close(root);
    if (fd < 0) {
        if (errno == ENOENT)
            return 0;
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != 0 || (st.st_mode & 0077) ||
        st.st_size < 0 || (uint64_t)st.st_size > maximum) {
        close(fd);
        errno = EACCES;
        return -1;
    }
    ssize_t n = read(fd, data, maximum);
    int saved = errno;
    close(fd);
    if (n < 0) {
        errno = saved;
        return -1;
    }
    if (n != st.st_size) {
        errno = EIO;
        return -1;
    }
    *size = n;
    return 0;
}
static int loader_lease(int cancel)
{
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    strcpy(address.sun_path, "/run/unicorn-fingerprint/control");
    struct ucred peer;
    socklen_t length = sizeof(peer);
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) ||
        getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &length) || peer.uid != 0)
        goto fail;
    struct pollfd p[2] = {{fd, POLLIN, 0}, {cancel, POLLIN, 0}};
    int rc;
    do {
        rc = poll(p, 2, 5000);
    } while (rc < 0 && errno == EINTR);
    if (p[1].revents) {
        errno = ECANCELED;
        goto fail;
    }
    if (rc <= 0) {
        if (!rc)
            errno = EBUSY;
        goto fail;
    }
    char ready;
    if (recv(fd, &ready, 1, MSG_TRUNC) != 1 || ready != 'R') {
        errno = EPROTO;
        goto fail;
    }
    return fd;
fail:;
    int saved = errno;
    close(fd);
    errno = saved;
    return -1;
}
static void format_identity(char out[80], sd_id128_t machine,
                            const unsigned char *serial, unsigned size)
{
    sd_id128_to_string(machine, out);
    /* Include the length as well as all 18 bytes: a trailing zero must not
     * alias a shorter serial, and the last two bytes are not disposable. */
    snprintf(out + 32, 3, "%02x", size);
    for (unsigned i = 0; i < 18; i++)
        snprintf(out + 34 + 2 * i, 3, "%02x", i < size ? serial[i] : 0);
}
static int initialize(struct engine *e, const char *name, bool take_lease)
{
    if (take_lease) {
        e->selector = loader_lease(e->channel);
        if (e->selector < 0)
            return -1;
    }
    if (detector(e, name))
        return -1;
    uint32_t chip = get(e->sensor_info, 4) & 0xfff;
    e->dsp = uf_dsp_start(e->dma, chip == 1 ? 1 : chip == 3 ? 2 : 3, e->channel);
    if (!e->dsp || ioctl(e->sensor.fd, UF_ENABLE_IRQ))
        return -1;
    if (prepared(e, UF_SENSOR_INIT))
        return -1;
    size_t extension = e->profile == UF_PROFILE_GFUNI6 ? 624 : 600;
    size_t otp = extension - 107, otp_size = extension - 40;
    unsigned char private[512] = {0};
    size_t length;
    if (read_private("gf_secure_obj_1.so_0", private, 64, &length))
        return -1;
    if (length && length != 64) {
        errno = EPROTO;
        return -1;
    }
    memcpy((char *)e->body.data + otp, private, length);
    put(e->body.data, otp_size, length);
    uf_wipe(private, sizeof(private));
    put(e->body.data, extension, 14);
    put(e->body.data, extension + 4, 8);
    ((unsigned char *)e->body.data)[extension + 12] = 3;
    if (invoke(e, UF_SENSOR_INIT))
        return -1;
    size_t cfg_size = e->profile == UF_PROFILE_GFUNI6 ? 440 : 416;
    memcpy(e->config, (char *)e->body.data + 32, cfg_size);
    e->kind = get(e->body.data, 32 + cfg_size);
    e->enroll_fake_check =
        ((((unsigned char *)e->body.data)[extension - 137] | 1) == 15) || e->config[0x11c] != 0;
    e->cali = get(e->body.data, 36 + cfg_size);
    fprintf(stderr, "event=calibration profile=%u phase=sensor flags=0x%x\n", e->profile, e->cali);
    if (get(e->body.data, 40 + cfg_size) != 1) {
        errno = ENOTSUP;
        return -1;
    }
    uint32_t serial_size = get(e->body.data, extension - 116);
    if (serial_size == 0 || serial_size > 18) {
        errno = serial_size == 0 ? ENODATA : EPROTO;
        return -1;
    }
    sd_id128_t machine;
    int identity_error = sd_id128_get_machine_app_specific(
        SD_ID128_MAKE(85, 2d, 30, 98, ee, 9d, 4c, 7a, bc, e1, 40, 28, 4e, b1, 66, 34), &machine);
    if (identity_error < 0) {
        errno = -identity_error;
        return -1;
    }
    const unsigned char *serial = (unsigned char *)e->body.data + extension - 136;
    format_identity(e->identity, machine, serial, serial_size);
    e->stages = get(e->config, 8);
    if (e->stages == 0 || e->stages > 128) {
        errno = EPROTO;
        return -1;
    }
    if (prepared(e, UF_ALGO_INIT) || read_private("oled_wp", private, 511, &length))
        return -1;
    memcpy((char *)e->body.data + 44, private, length);
    put(e->body.data, 556, length);
    uf_wipe(private, sizeof(private));
    ((unsigned char *)e->body.data)[40] = 3;
    if (invoke(e, UF_ALGO_INIT))
        return -1;
    e->cali |= get(e->body.data, 36);
    fprintf(stderr, "event=calibration profile=%u phase=algo flags=0x%x required=0x13\n",
            e->profile, e->cali);
    if (prepared(e, UF_SCREEN))
        return -1;
    put(e->body.data, 28, 1);
    if (uf_dsp_ready(e->dsp, e->channel, 5000) ||
        invoke(e, UF_SCREEN) || simple(e, UF_CANCEL) || sleep_sensor(e))
        return -1;
    if ((e->cali & 0x13) != 0x13) {
        /* Both OEM HALs query the cached calibration load error here. This
         * reads the failure reason; it neither calibrates nor bypasses it. */
        if (simple(e, UF_CALI_ERROR) && e->broken)
            return -1;
        fprintf(stderr, "event=calibration_rejected profile=%u flags=0x%x code=%d name=%s\n",
                e->profile, e->cali, e->code, uf_error_name(e->code));
        errno = ENODATA;
        return -1;
    }
    e->initialized = true;
    return 0;
}
int uf_qualify(const char *name)
{
    struct engine e = empty();
    int rc = initialize(&e, name, false);
    int saved = errno;
    if (finish(&e) && !rc) {
        rc = -1;
        saved = EIO;
    }
    errno = saved;
    return rc;
}
static int group(struct engine *e, uint32_t gid)
{
    if (gid != 0) {
        errno = EINVAL;
        return -1;
    }
    if (prepared(e, UF_SET_GROUP) ||
        uf_group_encode(e->body.data, 288, gid, "/data/vendor/fingerprint/linux-v1"))
        return -1;
    return invoke(e, UF_SET_GROUP);
}
static int send_event(struct engine *e, const struct uf_message *request, unsigned event,
                      unsigned remaining, struct uf_template print, unsigned flags)
{
    struct uf_message reply = {.magic = UF_IPC_MAGIC,
                               .version = UF_IPC_VERSION,
                               .operation = event,
                               .request = request->request,
                               .profile = e->profile,
                               .stages = remaining,
                               .code = e->code,
                               .flags = flags,
                               .count = print.finger ? 1 : 0};
    reply.prints[0] = print;
    if (send(e->channel, &reply, sizeof(reply), MSG_NOSIGNAL) != sizeof(reply))
        return -1;
    return 0;
}
static int touch_wait(struct engine *e, bool down, int64_t deadline)
{
    if (e->touch.down == down)
        return 0;
    struct pollfd p[3] = {
        {e->touch.fd, POLLIN, 0}, {e->sensor.fd, POLLIN, 0}, {e->channel, POLLIN, 0}};
    while (monotonic() < deadline) {
        int64_t left = deadline - monotonic();
        int rc = poll(p, 3, left > 1000 ? 1000 : left);
        if (rc < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (p[2].revents) {
            errno = ECANCELED;
            return -1;
        }
        if ((p[0].revents | p[1].revents) & (POLLERR | POLLHUP | POLLNVAL)) {
            errno = ENODEV;
            return -1;
        }
        if (p[0].revents & POLLIN) {
            if (uf_touch_read(&e->touch) < 0)
                return -1;
            if (e->touch.down == down)
                return 0;
        }
        if (p[1].revents & POLLIN) {
            struct uf_irq irq;
            if (uf_sensor_wait(&e->sensor, e->channel, 0, &irq) || simple(e, UF_IRQ))
                return -1;
            /* One kernel notification drains the merged TA interrupt state.
             * IRQ count is never interpreted as enrollment progress. */
            uint32_t event = get(e->body.data, 28);
            if (event != 2 && event != 4 && event != (uint32_t)-1000 && event != 0 && event != 8) {
                errno = EPROTO;
                return -1;
            }
        }
    }
    errno = ETIMEDOUT;
    return -1;
}
static int ids(struct engine *e, uint64_t matched, uint64_t study)
{
    if (prepared(e, UF_SENSOR_IDS))
        return -1;
    put64(e->body.data, 32, matched);
    put64(e->body.data, 40, study);
    return invoke(e, UF_SENSOR_IDS);
}
static int finger_command(struct engine *e, enum uf_command command, struct uf_template print)
{
    if (prepared(e, command))
        return -1;
    put(e->body.data, 28, print.group);
    put(e->body.data, 32, print.finger);
    return invoke(e, command);
}
static int image_algo(struct engine *e, bool enroll, unsigned retry, unsigned char result[240],
                      bool continuation, bool simple_enroll)
{
    enum uf_command command = enroll ? UF_ENROLL_IMAGE : UF_AUTH_IMAGE;
    size_t size = uf_profile_size(e->profile, command);
    if (prepared(e, command))
        return -1;
    unsigned char *b = e->body.data;
    if (continuation) {
        memcpy(b, result, size);
        b[0] = 0;
        put(b, 12, 0);
    }
    if (enroll) {
        put64(b, 48, 1);
        b[56] = 1;
        if (simple_enroll)
            b[57] = 1;
        if (!continuation)
            put(b, 40, e->stages);
        if (e->enroll_fake_check)
            put(b, 60, 20);
    } else {
        put(b, 28, retry);
        put(b, 36, 0);
        b[60] = 0;
        b[85] = b[86] = b[87] = 0;
        b[88] = 2;
        put64(b, 144, 1);
        b[152] = 1;
        put(b, 156, 20);
    }
    if (uf_dsp_begin(e->dsp, 6))
        return -1;
    int rc = invoke(e, command), saved = errno;
    memcpy(result, b, size);
    if (result[enroll ? 173 : 234] == 1 && uf_dsp_speed(e->dsp, false, e->channel)) {
        rc = -1;
        saved = errno;
        e->code = 0;
    }
    /* Consume DSP completion even when the TA reports a retry or no match. */
    if (uf_dsp_wait(e->dsp, e->channel, 3000)) {
        rc = -1;
        saved = errno;
        e->code = 0;
    }
    errno = saved;
    return rc;
}
static int finger_still_down(struct engine *e)
{
    int rc;
    /* read stops at a transition. Drain queued transitions before exposing:
     * an old down event must not hide a newer finger-up already in evdev. */
    do {
        rc = uf_touch_read(&e->touch);
    } while (rc > 0);
    if (rc < 0)
        return -1;
    if (!e->touch.down) {
        e->code = 1096;
        errno = EREMOTEIO;
        return -1;
    }
    return 0;
}
static int frame(struct engine *e, bool enroll, unsigned retry, unsigned sample,
                 unsigned char result[240])
{
    e->code = 0;
    bool reuse = !enroll && retry == 1 && e->config[0xf0] && !result[169];
    if (finger_still_down(e))
        return -1;
    if (ids(e, 0, 0) || wake(e) || (!reuse && uf_display_begin(&e->display)) ||
        uf_dsp_speed(e->dsp, true, e->channel))
        return -1;
    int rc = -1, saved = 0;
    int32_t code = 0;
    unsigned char *b = e->body.data;
    if (!reuse) {
        if (prepared(e, UF_CAPTURE))
            goto out;
        put(b, 32, retry);
        put(b, 36, enroll ? 1 : 2);
        put64(b, 44, 1);
        unsigned cfg = e->config[0xac];
        put(b, 56, enroll ? ((cfg >> 1) & 1) : (e->kind == 0x10001 ? 1 : (retry ? cfg & 1 : 0)));
        put(b, 64, 1);
        put(b, 72, enroll ? sample : 0);
        if (invoke(e, UF_CAPTURE) || uf_display_ready(&e->display) || finger_still_down(e))
            goto out;
        if (!enroll && !retry && e->profile == UF_PROFILE_GFUNI6)
            e->auth_no_retry = b[85] != 0;
        if (prepared(e, UF_READ_IMAGE))
            goto out;
        put64(b, 32, 1);
        b[40] = e->config[0xb0] != 0;
        put(b, 44, e->touch.x);
        put(b, 48, e->touch.y);
        if (invoke(e, UF_READ_IMAGE) || uf_display_ready(&e->display) || finger_still_down(e))
            goto out;
    }
    rc = image_algo(e, enroll, retry, result, retry != 0, enroll && retry != 0);
    saved = errno;
out:
    code = e->code;
    if (!saved)
        saved = errno;
    int off = uf_display_end(&e->display);
    if (off) {
        rc = -1;
        saved = errno;
        code = 0;
    }
    if (uf_dsp_speed(e->dsp, false, e->channel)) {
        rc = -1;
        saved = errno;
        code = 0;
    }
    e->code = code;
    errno = saved;
    return rc;
}
static int post_auth(struct engine *e, const unsigned char result[240], bool provisional)
{
    struct uf_template print = {get(result, 44), get(result, 48)};
    if (ids(e, provisional ? 0 : 1, provisional ? 0 : 1))
        return -1;
    for (unsigned phase = provisional ? 4 : 1; phase <= 4; phase++) {
        if (prepared(e, UF_POST_AUTH))
            return -1;
        put(e->body.data, 28, get(result, 28));
        put(e->body.data, 32, get(result, 40));
        put(e->body.data, 36, print.group);
        put(e->body.data, 40, print.finger);
        ((unsigned char *)e->body.data)[48] = phase;
        ((unsigned char *)e->body.data)[49] = result[153];
        ((unsigned char *)e->body.data)[50] = provisional ? 0 : 1;
        if (invoke(e, UF_POST_AUTH))
            return -1;
    }
    return 0;
}
static int auth_frames(struct engine *e, unsigned sample, unsigned char result[240],
                       int64_t deadline)
{
    uint32_t configured = get(e->config, 0x20);
    if (configured > 256) {
        errno = EPROTO;
        return -1;
    }
    unsigned retry = 0;
    e->auth_no_retry = false;
    for (unsigned attempt = 0;; attempt++) {
        if (monotonic() >= deadline) {
            e->code = 0;
            errno = ETIMEDOUT;
            return -1;
        }
        int rc = frame(e, false, retry, sample, result);
        int32_t code = e->code;
        if (!rc && !result[169])
            return 0;
        bool quality = code == 1008 || code == 1014 || code == 1015 || code == 1137 ||
                       code == 1147 || code == 1148 || code == 1164;
        bool provisional = result[169] != 0 && (!rc || quality);
        uint32_t limit = result[168] == 1 && e->config[0x110] ? e->config[0x110] : configured;
        if (attempt >= limit || result[235] || (!provisional && (!quality || e->auth_no_retry))) {
            if (provisional) {
                e->code = 1066;
                errno = EREMOTEIO;
            }
            return -1;
        }
        if (provisional && post_auth(e, result, true))
            return -1;
        retry++;
        if (retry == 1 && provisional && e->config[0xf0])
            retry = 2;
    }
}
static bool acquisition_retry(int32_t code)
{
    switch (code) {
    case 1013:
    case 1014:
    case 1015:
    case 1047:
    case 1054:
    case 1066:
    case 1067:
    case 1096:
    case 1147:
    case 1148:
    case 1164:
    case 1101:
    case 1103:
    case 1104:
    case 1105:
    case 1112:
    case 1135:
    case 1137:
    case 1138:
        return true;
    default:
        return false;
    }
}
static int capture(struct engine *e, const struct uf_message *request, struct uf_message *reply)
{
    bool enroll = request->operation == UF_OP_ENROLL;
    if ((e->cali & 0x13) != 0x13) {
        errno = ENODATA;
        return -1;
    }
    if (simple(e, UF_CANCEL) || group(e, 0))
        return -1;
    int rc = -1, saved = 0;
    if (enroll) {
        if (simple(e, UF_PRE_ENROLL))
            goto out;
        uint64_t challenge;
        if (uf_wire_get64(e->body.data, uf_profile_size(e->profile, UF_PRE_ENROLL),
                          32, &challenge))
            goto out;
        /* Log presence only. A separate credential authority receives the
         * challenge and returns a signed HAT; worker IPC never carries it. */
        fprintf(stderr, "event=enroll_authorization profile=%u phase=request "
                        "challenge_present=%u token=requested\n", e->profile, challenge != 0);
        unsigned char hat[UF_HAT_SIZE] = {0};
        if (uf_authorization_request(challenge, e->channel, hat)) {
            uf_wipe(hat, sizeof(hat));
            goto out;
        }
        int encoded = prepared(e, UF_ENROLL) ||
            uf_enroll_encode(e->body.data, 108, 0, hat, sizeof(hat));
        uf_wipe(hat, sizeof(hat));
        int started = encoded ? -1 : invoke(e, UF_ENROLL);
        uf_wipe((unsigned char *)e->body.data + 33, UF_HAT_SIZE);
        if (started) {
            fprintf(stderr, "event=enroll_authorization profile=%u phase=rejected errno=%d "
                            "code=%d name=%s token=gatekeeper\n",
                    e->profile, errno, e->code, uf_error_name(e->code));
            goto out;
        }
        fprintf(stderr, "event=enroll_authorization profile=%u phase=accepted token=gatekeeper\n",
                e->profile);
    } else {
        if (!request->count || request->count > UF_TEMPLATE_MAX) {
            errno = EINVAL;
            return -1;
        }
        if (prepared(e, UF_AUTHENTICATE) || invoke(e, UF_AUTHENTICATE))
            goto out;
    }
    if (uf_touch_open(&e->touch) || uf_display_open(&e->display, e->channel))
        goto out;
    int64_t deadline = monotonic() + 120000;
    unsigned last = e->stages, sample = 0;
    while (monotonic() < deadline) {
        unsigned char result[240] = {0};
        if (touch_wait(e, true, deadline))
            goto out;
        if (send_event(e, request, UF_EVENT_FINGER, 0, (struct uf_template){0}, 1))
            goto out;
        sample++;
        int attempt =
            enroll ? frame(e, true, 0, sample, result) : auth_frames(e, sample, result, deadline);
        if (enroll && attempt && e->code == 1513) {
            attempt = frame(e, true, 1, sample, result);
        }
        if (enroll && !attempt && result[172] && (result[40] || result[41])) {
            if (uf_dsp_speed(e->dsp, true, e->channel))
                goto out;
            attempt = image_algo(e, true, 0, result, true, false);
            int32_t original = e->code;
            int clock = uf_dsp_speed(e->dsp, false, e->channel);
            e->code = original;
            if (clock)
                goto out;
        }
        int32_t code = e->code;
        if (attempt) {
            if (enroll && code == 1015) /* Existing finger, not a poor sample. */
                goto out;
            if (code && acquisition_retry(code)) {
                if (!enroll) {
                    reply->flags = 2;
                    reply->code = code;
                    rc = 0;
                    goto out;
                }
                if (send_event(e, request, UF_EVENT_RETRY, last, (struct uf_template){0}, 0))
                    goto out;
            } else if (!enroll && (code == 1008 || code == 1141)) {
                reply->flags = 0;
                rc = 0;
                goto out;
            } else
                goto out;
        } else if (enroll) {
            struct uf_template print = {get(result, 28), get(result, 32)};
            unsigned remaining = result[40] | (unsigned)result[41] << 8;
            if (print.group != 0 || !print.finger || remaining > e->stages) {
                errno = EPROTO;
                goto out;
            }
            if (sample >= 2 && result[136]) {
                if (prepared(e, UF_ENROLL_FINISH))
                    goto out;
                put(e->body.data, 28, print.group);
                put(e->body.data, 32, print.finger);
                ((unsigned char *)e->body.data)[36] = remaining;
                ((unsigned char *)e->body.data)[37] = remaining >> 8;
                if (invoke(e, UF_ENROLL_FINISH))
                    goto out;
                remaining = 0;
            }
            if (remaining == 0) {
                if (finger_command(e, UF_SAVE, print))
                    goto out;
                /* A persisted template must be visible to a fresh enumeration. */
                if (simple(e, UF_ENUMERATE))
                    goto out;
                struct uf_template prints[10];
                size_t count;
                if (uf_enumerate_decode(e->body.data, 112, prints, 10, &count) ||
                    !uf_template_in_set(print, prints, count)) {
                    errno = EIO;
                    goto out;
                }
                reply->count = 1;
                reply->prints[0] = print;
            } else if (remaining < last) {
                if (send_event(e, request, UF_EVENT_PROGRESS, remaining, print, 0))
                    goto out;
                last = remaining;
            } else if (send_event(e, request, UF_EVENT_RETRY, last, (struct uf_template){0}, 0))
                goto out;
            if (prepared(e, UF_ENROLL_END))
                goto out;
            put(e->body.data, 28, print.group);
            put(e->body.data, 32, print.finger);
            put(e->body.data, 36, 20);
            ((unsigned char *)e->body.data)[38] = remaining;
            ((unsigned char *)e->body.data)[39] = remaining >> 8;
            if (invoke(e, UF_ENROLL_END))
                goto out;
            if (!remaining) {
                rc = 0;
                goto out;
            }
        } else {
            struct uf_template print = {get(result, 44), get(result, 48)};
            if (print.group != 0 || !print.finger) {
                errno = EPROTO;
                goto out;
            }
            if (!uf_template_in_set(print, request->prints, request->count)) {
                reply->flags = 0;
                rc = 0;
                goto out;
            }
            if (post_auth(e, result, false))
                goto out;
            reply->count = 1;
            reply->prints[0] = print;
            reply->flags = 1;
            rc = 0;
            goto out;
        }
        if (sleep_sensor(e))
            goto out;
        if (touch_wait(e, false, deadline))
            goto out;
        if (send_event(e, request, UF_EVENT_FINGER, 0, (struct uf_template){0}, 0))
            goto out;
    }
    errno = ETIMEDOUT;
out:
    saved = errno;
    int32_t code = e->code;
    uf_display_close(&e->display);
    uf_touch_close(&e->touch);
    if (simple(e, UF_CANCEL) && !rc) {
        rc = -1;
        saved = errno;
        code = e->code;
    }
    if (enroll && simple(e, UF_POST_ENROLL) && !rc) {
        rc = -1;
        saved = errno;
        code = e->code;
    }
    if (sleep_sensor(e) && !rc) {
        rc = -1;
        saved = errno;
    }
    e->code = code;
    errno = saved;
    return rc;
}
static int selected(char name[8])
{
    int fd = open("/run/unicorn-fingerprint/profile", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return -1;
    struct stat st;
    char data[8] = {0};
    if (fstat(fd, &st) || st.st_uid != 0 || (st.st_mode & 0022) || !S_ISREG(st.st_mode)) {
        close(fd);
        errno = EACCES;
        return -1;
    }
    ssize_t n = read(fd, data, sizeof(data));
    close(fd);
    if (n == 6 && !memcmp(data, "gfuni\n", 6))
        strcpy(name, "gfuni");
    else if (n == 7 && !memcmp(data, "gfuni6\n", 7))
        strcpy(name, "gfuni6");
    else {
        errno = EPROTO;
        return -1;
    }
    return 0;
}
int uf_worker_serve(int fd)
{
    struct ucred peer;
    socklen_t size = sizeof(peer);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &size) || peer.uid != 0)
        return 1;
    struct engine e = empty();
    e.channel = fd;
    uint32_t previous = 0;
    for (;;) {
        struct uf_message request,
            reply = {.magic = UF_IPC_MAGIC, .version = UF_IPC_VERSION, .operation = UF_EVENT_DONE};
        ssize_t n = recv(fd, &request, sizeof(request), MSG_TRUNC);
        if (n < 0 && errno == EINTR)
            continue;
        if (n != sizeof(request) || request.magic != UF_IPC_MAGIC ||
            request.version != UF_IPC_VERSION || !request.request || request.request <= previous)
            break;
        previous = request.request;
        reply.request = request.request;
        int rc = -1;
        e.code = 0;
        if (request.operation == UF_OP_INIT && !e.initialized) {
            char name[8];
            if (!selected(name))
                rc = initialize(&e, name, true);
            if (!rc) {
                reply.profile = e.profile;
                reply.stages = e.stages;
                memcpy(reply.device, e.identity, sizeof(reply.device));
            }
        } else if (e.initialized) {
            if (request.operation == UF_OP_LIST) {
                if (!group(&e, 0) && !simple(&e, UF_ENUMERATE)) {
                    size_t count;
                    rc = uf_enumerate_decode(e.body.data, 112, reply.prints, 10, &count);
                    reply.count = count;
                }
            } else if (request.operation == UF_OP_DELETE && request.count == 1 &&
                       request.prints[0].finger) {
                if (!group(&e, request.prints[0].group))
                    rc = finger_command(&e, UF_REMOVE, request.prints[0]);
                if (!rc) {
                    if (simple(&e, UF_ENUMERATE))
                        rc = -1;
                    else {
                        struct uf_template p[10];
                        size_t count;
                        if (uf_enumerate_decode(e.body.data, 112, p, 10, &count) ||
                            uf_template_in_set(request.prints[0], p, count)) {
                            rc = -1;
                            errno = EIO;
                        }
                    }
                }
            } else if (request.operation == UF_OP_ENROLL || request.operation == UF_OP_VERIFY ||
                       request.operation == UF_OP_IDENTIFY)
                rc = capture(&e, &request, &reply);
            else if (request.operation == UF_OP_CLOSE)
                rc = 0;
            else
                errno = EINVAL;
        } else
            errno = ENODEV;
        if (rc) {
            reply.error = errno ? errno : EIO;
            reply.code = e.code;
        }
        reply.profile = e.profile;
        if (send(fd, &reply, sizeof(reply), MSG_NOSIGNAL) != sizeof(reply) ||
            request.operation == UF_OP_CLOSE || rc)
            break;
    }
    finish(&e);
    return 0;
}
