/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "authorization.h"
#include "authorization-state.h"
#include "login.h"
#include "protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t interrupted;
static void stop(int signal)
{
    interrupted = signal;
}
static int64_t now(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t))
        return -1;
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static int directory(const char *path)
{
    if (mkdir(path, 0700) && errno != EEXIST)
        return -1;
    int fd = open(path, O_DIRECTORY | O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return -1;
    struct stat st;
    if (fstat(fd, &st) || st.st_uid != 0 || (st.st_mode & 0777) != 0700) {
        close(fd);
        errno = EACCES;
        return -1;
    }
    if (flock(fd, LOCK_EX | LOCK_NB)) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    return fd;
}
static int password(const char *prompt, char output[129])
{
    int fd = open("/dev/tty", O_RDWR | O_CLOEXEC | O_NOCTTY | O_NOFOLLOW);
    if (fd < 0)
        return -1;
    struct termios original, hidden;
    int rc = -1;
    if (tcgetattr(fd, &original))
        goto close_fd;
    hidden = original;
    hidden.c_lflag &= ~ECHO;
    if (tcsetattr(fd, TCSAFLUSH, &hidden))
        goto close_fd;
    if (write(fd, prompt, strlen(prompt)) != (ssize_t)strlen(prompt))
        goto restore;
    size_t size = 0;
    bool overlong = false;
    while (!interrupted) {
        unsigned char c;
        ssize_t n = read(fd, &c, 1);
        if (n <= 0) {
            if (!n)
                errno = ECANCELED;
            goto restore;
        }
        if (c == '\n' || c == '\r') {
            if (overlong || !size) {
                errno = EINVAL;
                goto restore;
            }
            output[size] = 0;
            rc = 0;
            goto restore;
        }
        if (!c || size == 128)
            overlong = true;
        else if (!overlong)
            output[size++] = c;
    }
    errno = ECANCELED;
restore:;
    int saved = errno;
    if (tcsetattr(fd, TCSAFLUSH, &original)) {
        saved = errno;
        rc = -1;
    }
    ssize_t newline = write(fd, "\n", 1);
    if (newline != 1 && !rc) {
        saved = errno;
        rc = -1;
    }
    errno = saved;
close_fd:
    close(fd);
    if (rc)
        uf_wipe(output, 129);
    return rc;
}
static int random_salt(unsigned char salt[16])
{
    ssize_t size = getrandom(salt, 16, 0);
    if (size != 16) {
        if (size >= 0)
            errno = EIO;
        return -1;
    }
    return 0;
}
static void gatekeeper_error(const struct uf_gatekeeper *g, const struct uf_tee_result *result)
{
    fprintf(stderr, "Gatekeeper: %s; command=0x%x request-bytes=%zu response-offset=%zu "
                    "TA status=%d retry-ms=%u TEE code=%u. No automatic retry.\n",
            strerror(errno), g->command, g->request_bytes, g->response_offset,
            g->status, g->retry_ms, result->code);
}
static int setup_available(int state, uint32_t uid)
{
    char marker[32];
    snprintf(marker, sizeof(marker), "pending-%u", uid);
    struct stat st;
    if (!fstatat(state, marker, &st, AT_SYMLINK_NOFOLLOW)) {
        fprintf(stderr, "An earlier credential setup has an unresolved result; preserving its state.\n");
        errno = EALREADY;
        return -1;
    }
    if (errno != ENOENT)
        return -1;
    return 0;
}
static int provision(int state, struct uf_credential *credential, const unsigned char secret[32])
{
    char marker[32];
    snprintf(marker, sizeof(marker), "pending-%u", credential->uid);
    if (setup_available(state, credential->uid))
        return -1;
    struct uf_gatekeeper g = UF_GATEKEEPER_CLOSED;
    struct uf_tee_result result = {0};
    if (uf_gatekeeper_open(&g, &result)) {
        gatekeeper_error(&g, &result);
        return -1;
    }
    int rc = -1;
    int fd = openat(state, marker, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        goto out;
    int synced = fsync(fd);
    int saved = errno;
    close(fd);
    errno = saved;
    if (synced || fsync(state))
        goto out;
    credential->handle_size = sizeof(credential->handle);
    if (uf_gatekeeper_enroll(&g, UF_GATEKEEPER_UID_BASE | credential->uid, secret,
                              credential->handle, &credential->handle_size, &result)) {
        int failure = errno;
        gatekeeper_error(&g, &result);
        /* The stock TA can fail after writing the user record (sync_sector
         * / copy_hmac_to_signature). A returned status does not prove that
         * enrollment left secure storage unchanged. Never allow replay. */
        fprintf(stderr, "Credential setup did not complete; keeping its pending marker. "
                        "Do not repeat setup or remove the marker until the secure result is resolved.\n");
        errno = failure;
        goto out;
    }
    if (uf_credential_create(state, credential)) {
        fprintf(stderr, "Credential handle could not be committed; preserving the pending marker.\n");
        goto out;
    }
    if (unlinkat(state, marker, 0) || fsync(state))
        goto out;
    puts("Fingerprint management credential registered. No fingerprint was captured or enrolled.");
    rc = 0;
out:;
    int error = errno;
    if (uf_gatekeeper_close(&g) && !rc) {
        error = errno;
        rc = -1;
    }
    errno = error;
    return rc;
}
static int listener(int run)
{
    struct stat st;
    if (!fstatat(run, "provider.sock", &st, AT_SYMLINK_NOFOLLOW)) {
        if (!S_ISSOCK(st.st_mode) || st.st_uid != 0 || (st.st_mode & 0777) != 0600) {
            errno = EACCES;
            return -1;
        }
        /* The directory lock excludes a live provider using this socket. */
        if (unlinkat(run, "provider.sock", 0))
            return -1;
    } else if (errno != ENOENT)
        return -1;
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0)
        return -1;
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    strcpy(address.sun_path, UF_AUTH_SOCKET);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) ||
        fchmodat(run, "provider.sock", 0600, 0) || listen(fd, 1)) {
        int saved = errno;
        close(fd);
        unlinkat(run, "provider.sock", 0);
        errno = saved;
        return -1;
    }
    return fd;
}
static int serve(int listener_fd, struct uf_gatekeeper *g, const struct uf_credential *credential,
                  const unsigned char secret[32])
{
    int client = accept4(listener_fd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (client < 0)
        return -1;
    struct uf_auth_response reply = {.magic = UF_AUTH_MAGIC, .version = 1};
    struct ucred peer;
    socklen_t size = sizeof(peer);
    char process[64], expected[PATH_MAX], executable[PATH_MAX];
    int rc = -1;
    if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &peer, &size) || peer.uid != 0)
        goto denied;
    snprintf(process, sizeof(process), "/proc/%d/exe", peer.pid);
    ssize_t n = readlink(process, executable, sizeof(executable) - 1);
    if (n < 0 || !realpath("/run/current-system/sw/bin/unicorn-fingerprint-runtime", expected))
        goto denied;
    executable[n] = 0;
    if (strcmp(expected, executable))
        goto denied;
    struct pollfd p = {client, POLLIN, 0};
    if (poll(&p, 1, 3000) <= 0) {
        reply.error = interrupted ? ECANCELED : ETIMEDOUT;
        goto send;
    }
    struct uf_auth_request request;
    if (recv(client, &request, sizeof(request), MSG_TRUNC) != sizeof(request) ||
        request.magic != UF_AUTH_MAGIC || request.version != 1 || !request.challenge) {
        reply.error = EPROTO;
        goto send;
    }
    struct uf_tee_result result = {0};
    if (uf_gatekeeper_verify(g, UF_GATEKEEPER_UID_BASE | credential->uid, request.challenge,
                              secret, credential->handle, credential->handle_size, reply.hat, &result)) {
        reply.error = errno;
        reply.retry_ms = g->retry_ms;
        gatekeeper_error(g, &result);
        goto send;
    }
    rc = 0;
    goto send;
denied:
    reply.error = EACCES;
send:
    if (send(client, &reply, sizeof(reply), MSG_NOSIGNAL) != sizeof(reply))
        rc = -1;
    uf_wipe(&reply, sizeof(reply));
    close(client);
    return rc;
}
static bool finger_name(const char *name)
{
    static const char *names[] = {
        "left-thumb", "left-index-finger", "left-middle-finger", "left-ring-finger", "left-little-finger",
        "right-thumb", "right-index-finger", "right-middle-finger", "right-ring-finger", "right-little-finger"};
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (!strcmp(name, names[i]))
            return true;
    return false;
}
static int enroll(int run, const char *username, const char *finger,
                   const struct uf_credential *credential, unsigned char secret[32])
{
    struct uf_gatekeeper g = UF_GATEKEEPER_CLOSED;
    struct uf_tee_result result = {0};
    if (uf_gatekeeper_open(&g, &result)) {
        gatekeeper_error(&g, &result);
        return -1;
    }
    int socket_fd = listener(run), rc = -1;
    if (socket_fd < 0)
        goto out;
    pid_t child = fork();
    if (child < 0)
        goto out;
    if (!child) {
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        if (prctl(PR_SET_PDEATHSIG, SIGTERM) || getppid() == 1)
            _exit(1);
        uf_wipe(secret, 32);
        /* The inherited TEE fd refers to the parent's open file description.
         * Explicit CLOSE_SESSION here would close its session too. exec
         * removes the child's mappings; the parent owns secure teardown. */
        close(g.tee.fd);
        close(socket_fd);
        execl("/run/current-system/sw/bin/fprintd-enroll", "fprintd-enroll", "-f", finger, username, NULL);
        _exit(127);
    }
    int64_t deadline = now() + 180000;
    bool consumed = false, provided = false, reaped = false;
    int status = 0;
    while (!interrupted && now() < deadline) {
        pid_t done = waitpid(child, &status, WNOHANG);
        if (done == child) {
            reaped = true;
            break;
        }
        if (done < 0)
            break;
        struct pollfd p = {socket_fd, consumed ? 0 : POLLIN, 0};
        int ready = poll(&p, 1, 200);
        if (ready < 0 && errno != EINTR)
            break;
        if (!consumed && (p.revents & POLLIN)) {
            consumed = true;
            provided = !serve(socket_fd, &g, credential, secret);
            uf_wipe(secret, 32);
            close(socket_fd);
            socket_fd = -1;
            unlinkat(run, "provider.sock", 0);
        }
    }
    if (!reaped) {
        kill(child, SIGINT);
        int64_t stop_deadline = now() + 3000;
        do {
            if (waitpid(child, &status, WNOHANG) == child) {
                reaped = true;
                break;
            }
            struct timespec pause = {0, 20000000};
            nanosleep(&pause, NULL);
        } while (now() < stop_deadline);
        if (!reaped) {
            kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
        }
        errno = interrupted ? ECANCELED : ETIMEDOUT;
    } else if (provided && WIFEXITED(status) && WEXITSTATUS(status) == 0)
        rc = 0;
    else
        errno = EIO;
out:;
    int saved = errno;
    if (socket_fd >= 0)
        close(socket_fd);
    unlinkat(run, "provider.sock", 0);
    if (uf_gatekeeper_close(&g) && !rc) {
        rc = -1;
        saved = errno;
    }
    errno = saved;
    return rc;
}
int main(int argc, char **argv)
{
    bool setup = argc >= 2 && !strcmp(argv[1], "setup");
    bool enrolling = argc >= 2 && !strcmp(argv[1], "enroll");
    if ((!setup && !enrolling) || argc < 3 || argc > (setup ? 3 : 4) ||
        (argc == 4 && !finger_name(argv[3]))) {
        fputs("usage: unicorn-fingerprint-authorize setup USER\n"
              "       unicorn-fingerprint-authorize enroll USER [FINGER]\n", stderr);
        return 2;
    }
    struct rlimit limit = {0};
    if (geteuid() || setrlimit(RLIMIT_CORE, &limit) || prctl(PR_SET_DUMPABLE, 0)) {
        fputs("Run this tool with sudo in a local terminal.\n", stderr);
        return 1;
    }
    char compatible[256] = {0};
    int board = open("/proc/device-tree/compatible", O_RDONLY | O_CLOEXEC);
    ssize_t n = board >= 0 ? read(board, compatible, sizeof(compatible)) : -1;
    if (board >= 0)
        close(board);
    if (n < 15 || memcmp(compatible, "xiaomi,unicorn\0", 15)) {
        fputs("This authorization provider is qualified only for Unicorn.\n", stderr);
        return 1;
    }
    struct passwd *user = getpwnam(argv[2]);
    if (!user || user->pw_uid > UINT16_MAX) {
        fputs("Unknown or unsupported local user.\n", stderr);
        return 1;
    }
    uint32_t uid = user->pw_uid;
    umask(0077);
    struct sigaction action = {.sa_handler = stop};
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    struct {
        char password[129];
        unsigned char derived[32];
    } secret = {0};
    if (mlock(&secret, sizeof(secret))) {
        perror("lock credential memory");
        return 1;
    }
    int state = directory(UF_CREDENTIAL_DIRECTORY), run = -1, rc = 1;
    struct uf_credential credential = {0};
    if (state < 0)
        goto out;
    int loaded = uf_credential_load(state, uid, &credential);
    if (setup) {
        if (!loaded) {
            fputs("A management credential already exists; it will not be replaced.\n", stderr);
            errno = EEXIST;
            goto out;
        }
        if (errno != ENOENT)
            goto out;
        /* Refuse an unresolved setup before asking for another password. */
        if (setup_available(state, uid))
            goto out;
        credential.uid = uid;
        if (random_salt(credential.salt))
            goto out;
    } else {
        if (loaded) {
            if (errno == ENOENT) {
                if (setup_available(state, uid))
                    goto out;
                fputs("Run 'unicorn-fingerprint-authorize setup USER' locally first.\n", stderr);
            }
            goto out;
        }
    }
    puts("Use your current Linux login password. It stays on this phone.\n"
         "This does not change your login password or existing Android credentials.");
    if (password("Current Linux login password: ", secret.password) ||
        uf_login_authenticate(argv[2], secret.password))
        goto out;
    if (interrupted) {
        errno = ECANCELED;
        goto out;
    }
    if (uf_credential_derive(secret.password, strlen(secret.password), credential.salt, secret.derived))
        goto out;
    uf_wipe(secret.password, sizeof(secret.password));
    if (interrupted) {
        errno = ECANCELED;
        goto out;
    }
    if (setup)
        rc = provision(state, &credential, secret.derived) ? 1 : 0;
    else {
        run = directory(UF_AUTH_DIRECTORY);
        if (run < 0)
            goto out;
        rc = enroll(run, argv[2], argc == 4 ? argv[3] : "right-index-finger", &credential, secret.derived) ? 1 : 0;
    }
out:
    if (rc)
        fprintf(stderr, "Fingerprint authorization: %s\n", strerror(errno));
    uf_wipe(&secret, sizeof(secret));
    munlock(&secret, sizeof(secret));
    uf_wipe(&credential, sizeof(credential));
    if (run >= 0)
        close(run);
    if (state >= 0)
        close(state);
    return rc;
}
