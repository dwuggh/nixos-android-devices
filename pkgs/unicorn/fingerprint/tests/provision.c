/* SPDX-License-Identifier: MIT */
/* Exercise real provisioning and filesystem operations, replacing the TEE and
 * final credential commit boundaries. No password or credential is supplied. */
#define main authorize_unused_main
#include "../authorize.c"
#undef main
#include <assert.h>

static unsigned opens, enrolls, closes, commits;
static int open_error, enroll_error, commit_error, close_error, ta_status;
static bool broken;

int __wrap_uf_gatekeeper_open(struct uf_gatekeeper *g, struct uf_tee_result *result)
{
    (void)g; (void)result;
    opens++;
    errno = open_error;
    return open_error ? -1 : 0;
}
int __wrap_uf_gatekeeper_close(struct uf_gatekeeper *g)
{
    (void)g;
    closes++;
    errno = close_error;
    return close_error ? -1 : 0;
}
int __wrap_uf_gatekeeper_enroll(struct uf_gatekeeper *g, uint32_t uid,
                               const unsigned char password[32], unsigned char *handle,
                               size_t *size, struct uf_tee_result *result)
{
    (void)password; (void)result;
    assert(uid == (UF_GATEKEEPER_UID_BASE | 1000));
    enrolls++;
    g->status = ta_status;
    g->broken = broken;
    *size = 9;
    memset(handle, 0, *size);
    errno = enroll_error;
    return enroll_error ? -1 : 0;
}
int __wrap_uf_credential_create(int state, const struct uf_credential *credential)
{
    struct stat st;
    assert(credential->uid == 1000);
    assert(!fstatat(state, "pending-1000", &st, AT_SYMLINK_NOFOLLOW));
    assert(S_ISREG(st.st_mode) && (st.st_mode & 0777) == 0600);
    commits++;
    errno = commit_error;
    return commit_error ? -1 : 0;
}

static void scenario(int state, int status, bool unknown, int commit_failure)
{
    struct uf_credential credential = {.uid = 1000};
    const unsigned char secret[32] = {0};
    unsigned old_opens = opens, old_enrolls = enrolls, old_closes = closes, old_commits = commits;
    ta_status = status;
    broken = unknown;
    enroll_error = status ? EREMOTEIO : unknown ? EIO : 0;
    commit_error = commit_failure;
    bool failure = status || unknown || commit_failure;
    assert(provision(state, &credential, secret) == (failure ? -1 : 0));
    assert(opens == old_opens + 1 && enrolls == old_enrolls + 1 && closes == old_closes + 1);
    assert(commits == old_commits + (!status && !unknown));
    struct stat st;
    if (failure) {
        assert(!fstatat(state, "pending-1000", &st, AT_SYMLINK_NOFOLLOW));
        assert(S_ISREG(st.st_mode) && st.st_size == 0 && (st.st_mode & 0777) == 0600);
        /* A second explicit setup must not even open the TEE or create data. */
        assert(provision(state, &credential, secret) == -1 && errno == EALREADY);
        assert(opens == old_opens + 1 && enrolls == old_enrolls + 1 && closes == old_closes + 1);
        assert(!unlinkat(state, "pending-1000", 0)); /* Test directory only. */
    } else {
        assert(fstatat(state, "pending-1000", &st, AT_SYMLINK_NOFOLLOW) == -1 && errno == ENOENT);
    }
}
int main(void)
{
    char name[] = "/tmp/unicorn-provision-test-XXXXXX";
    assert(mkdtemp(name));
    int state = open(name, O_DIRECTORY | O_RDONLY | O_CLOEXEC);
    assert(state >= 0);
    struct uf_credential credential = {.uid = 1000};
    const unsigned char secret[32] = {0};
    open_error = ENODEV;
    assert(provision(state, &credential, secret) == -1);
    assert(!enrolls && !closes && !commits);
    struct stat st;
    assert(fstatat(state, "pending-1000", &st, AT_SYMLINK_NOFOLLOW) == -1 && errno == ENOENT);
    open_error = 0;
    scenario(state, -30, false, 0);
    scenario(state, -49, false, 0);
    scenario(state, 16, false, 0);
    scenario(state, 0, true, 0);
    scenario(state, 0, false, ENOSPC);
    scenario(state, 0, false, 0);
    close_error = EIO;
    assert(provision(state, &credential, secret) == -1 && errno == EIO);
    assert(fstatat(state, "pending-1000", &st, AT_SYMLINK_NOFOLLOW) == -1 && errno == ENOENT);
    close(state);
    assert(!rmdir(name));
    puts("provision: TA failure, uncertain transport, failed commit, no replay, success and close error passed");
    return 0;
}
