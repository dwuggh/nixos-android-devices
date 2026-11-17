/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "ipc.h"
#include "protocol.h"
#include "tee.h"
#include "platform.h"
#include "gatekeeper.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <systemd/sd-bus.h>

static int unicorn(void)
{
    char compatible[256];
    int fd = open("/proc/device-tree/compatible", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    ssize_t n = read(fd, compatible, sizeof(compatible));
    int saved = errno;
    close(fd);
    if (n < 0) {
        errno = saved;
        return -1;
    }
    static const char board[] = "xiaomi,unicorn";
    for (size_t i = 0; i + sizeof(board) <= (size_t)n;) {
        if (!memcmp(compatible + i, board, sizeof(board)))
            return 0;
        const char *end = memchr(compatible + i, 0, n - i);
        if (!end)
            break;
        i = end - compatible + 1;
    }
    errno = ENODEV;
    return -1;
}

static int keymaster(void)
{
    struct uf_tee tee = UF_TEE_CLOSED;
    struct uf_buffer buffer = {0};
    struct uf_tee_result result = {0};
    int rc = 1;
    if (unicorn()) {
        perror("Unicorn board check");
        return 1;
    }
    struct rlimit limit = {0};
    if (setrlimit(RLIMIT_CORE, &limit) || prctl(PR_SET_DUMPABLE, 0)) {
        perror("disable core dumps");
        return 1;
    }
    if (uf_tee_open(&tee, "keymaster64", &result))
        goto out;
    if (uf_tee_alloc(&tee, &buffer, 320))
        goto out;
    uf_keymaster_request(buffer.data);
    /* OEM external 8-byte request is zero-padded to 64; the separate
     * 256-byte reply starts at +64. No Goodix/sensor command is issued. */
    struct uf_ref request = {&buffer, 0, 64}, reply = {&buffer, 64, 256};
    if (uf_tee_invoke(&tee, request, reply, NULL, 0, &result))
        goto out;
    int32_t status;
    if (uf_keymaster_status((unsigned char *)buffer.data + 64, 256, &status))
        goto out;
    printf("keymaster-status=%" PRId32 " reply-bytes=256 contents=discarded\n", status);
    if (status) {
        errno = EREMOTEIO;
        goto out;
    }
    puts("Keymaster command returned status zero; sensor and enrollment remain unqualified.");
    rc = 0;
out:
    if (rc) {
        int saved = errno;
        fprintf(stderr, "Keymaster exchange: %s; tee-code=%" PRIu32 " tee-origin=%" PRIu32 "\n",
                strerror(saved), result.code, result.origin);
    }
    uf_tee_free(&buffer);
    if (uf_tee_close(&tee)) {
        perror("close Keymaster session");
        rc = 1;
    }
    return rc;
}

static int gatekeeper_info(void)
{
    if (unicorn()) {
        perror("Unicorn board check");
        return 1;
    }
    struct rlimit limit = {0};
    if (setrlimit(RLIMIT_CORE, &limit) || prctl(PR_SET_DUMPABLE, 0)) {
        perror("disable core dumps");
        return 1;
    }
    struct uf_gatekeeper g = UF_GATEKEEPER_CLOSED;
    struct uf_tee_result result = {0};
    if (uf_gatekeeper_open(&g, &result)) {
        int saved = errno;
        fprintf(stderr, "Gatekeeper negotiation: %s; status=%" PRId32
                        " tee-code=%" PRIu32 " tee-origin=%" PRIu32 "\n",
                strerror(saved), g.status, result.code, result.origin);
        return 1;
    }
    printf("keymaster-api=%u.%u keymaster-ta=%u.%u client-negotiation=accepted\n",
           g.api_major, g.api_minor, g.ta_major, g.ta_minor);
    puts("credential-operations=not-issued signed-hat=not-tested");
    if (uf_gatekeeper_close(&g)) {
        perror("close Gatekeeper client");
        return 1;
    }
    return 0;
}

static int service(sd_bus *bus, const char *name)
{
    sd_bus_message *reply = NULL;
    const char *path = NULL;
    char *state = NULL;
    int rc = bus ? sd_bus_call_method(bus, "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
                                      "org.freedesktop.systemd1.Manager", "GetUnit", NULL,
                                      &reply, "s", name) : -ENOTCONN;
    if (rc >= 0)
        rc = sd_bus_message_read(reply, "o", &path);
    if (rc >= 0)
        rc = sd_bus_get_property_string(bus, "org.freedesktop.systemd1", path,
                                        "org.freedesktop.systemd1.Unit", "ActiveState", NULL, &state);
    printf("service=%s state=%s\n", name, rc < 0 ? "unknown-or-not-loaded" : state);
    int active = rc >= 0 && !strcmp(state, "active");
    free(state);
    sd_bus_message_unref(reply);
    return active;
}
static int status(void)
{
    char detail[192] = {0}, profile[8] = {0};
    bool board = !unicorn(), platform = !uf_platform_check(detail, sizeof(detail));
    printf("board=%s platform=%s detail=%s\n", board ? "unicorn" : "unavailable",
           platform ? "ready" : "unavailable", platform ? "CDSP-running" : detail);
    int fd = open("/run/unicorn-fingerprint/profile", O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    struct stat st;
    bool selected = false;
    if (fd >= 0) {
        if (!fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_uid == 0 && !(st.st_mode & 0022)) {
            ssize_t n = read(fd, profile, sizeof(profile));
            selected = (n == 6 && !memcmp(profile, "gfuni\n", 6)) ||
                       (n == 7 && !memcmp(profile, "gfuni6\n", 7));
            if (selected)
                profile[n - 1] = 0;
        }
        close(fd);
    }
    printf("profile=%s\n", selected ? profile : "absent-or-unreadable");
    sd_bus *system = NULL, *user = NULL;
    sd_bus_open_system(&system);
    sd_bus_open_user(&user);
    bool selector = service(system, "unicorn-fingerprint-select.service");
    bool supplicant = service(system, "qsee-supplicant.service");
    service(system, "fprintd.service");
    service(system, "unicorn-fingerprint-display.socket");
    service(system, "unicorn-fingerprint-display.service");
    service(user, "unicorn-fingerprint-hint.service");
    sd_bus_unref(system);
    sd_bus_unref(user);
    bool ready = board && platform && selected && selector && supplicant;
    printf("initialization=%s biometric-acceptance=not-recorded display-frame=unverified\n",
           ready ? "selector-qualified" : "not-ready");
    return ready ? 0 : 3;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "status")) {
        return status();
    }
    if (argc == 2 && !strcmp(argv[1], "keymaster"))
        return keymaster();
    if (argc == 2 && !strcmp(argv[1], "gatekeeper-info"))
        return gatekeeper_info();
    if (argc == 2 && !strcmp(argv[1], "worker")) {
        if (unicorn() || geteuid() != 0)
            return 1;
        struct rlimit limit = {0};
        if (setrlimit(RLIMIT_CORE, &limit) || prctl(PR_SET_DUMPABLE, 0))
            return 1;
        signal(SIGPIPE, SIG_IGN);
        pid_t parent = getppid();
        if (parent == 1 || prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent)
            return 1;
        return uf_worker_serve(3);
    }
    fputs("usage: unicorn-fingerprint-runtime {status|keymaster|gatekeeper-info|worker}\n", stderr);
    return 2;
}
