/* Test the extracted production listener transaction with only SCM/service fakes. */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define QSEECOM_RESULT_INCOMPLETE 1
#define QSEECOM_RESULT_BLOCKED_ON_LISTENER 2
#define QSEECOM_SCM_RES_QSEOS_LISTENER_ID 3
#define QSEECOM_LISTENER_SUCCESS 0
#define QSEECOM_LISTENER_FAILURE 1
#define U32_MAX UINT32_MAX
#define lockdep_assert_held(x) ((void)0)
#define dev_err(...) ((void)0)
#define dev_warn_once(...) ((void)0)
#define dev_warn_ratelimited(...) ((void)0)
#define dev_info_ratelimited(...) ((void)0)
#define secs_to_jiffies(s) ((s) * 1000UL)
#define time_after_eq(a,b) ((long)((a) - (b)) >= 0)
typedef uint32_t u32;
struct qcom_scm_qseecom_resp { uint64_t result, resp_type, data; };
struct qcom_scm_qseecom_listener { u32 (*service)(struct qcom_scm_qseecom_listener *); };
static unsigned long jiffies;
static unsigned calls, responses, needed, service_ms, last_status;
static bool fail_service, alternate, unknown, fail_response, blocked;
static u32 fake_service(struct qcom_scm_qseecom_listener *l)
{
    (void)l;
    ++calls;
    jiffies += service_ms;
    return fail_service || (alternate && calls % 2) ? QSEECOM_LISTENER_FAILURE : 0;
}
static struct qcom_scm_qseecom_listener listener = {fake_service};
static struct qcom_scm_qseecom_listener *qcom_scm_qseecom_listener_find(u32 id)
{
    (void)id;
    return unknown ? NULL : &listener;
}
static int qcom_scm_qseecom_listener_respond(u32 id, u32 status,
                                           struct qcom_scm_qseecom_resp *res)
{
    (void)id;
    last_status = status;
    ++responses;
    if (fail_response)
        return -EIO;
    if (responses >= needed)
        res->result = blocked ? QSEECOM_RESULT_BLOCKED_ON_LISTENER : 0;
    return 0;
}
/* @LIMITS@ */
/* @FUNCTION@ */
static struct qcom_scm_qseecom_resp reset(unsigned n)
{
    jiffies = calls = responses = last_status = 0;
    needed = n;
    service_ms = 1;
    fail_service = alternate = unknown = fail_response = blocked = false;
    return (struct qcom_scm_qseecom_resp){QSEECOM_RESULT_INCOMPLETE,
                                         QSEECOM_SCM_RES_QSEOS_LISTENER_ID, 28672};
}
int main(void)
{
    struct qcom_scm_qseecom_resp r = reset(600);
    assert(qcom_scm_qseecom_service_listeners(&r) == 0 && calls == 600);
    r = reset(600);
    alternate = true;
    assert(qcom_scm_qseecom_service_listeners(&r) == 0 && calls == 600);
    r = reset(600);
    jiffies = ULONG_MAX - 100;
    assert(qcom_scm_qseecom_service_listeners(&r) == 0 && calls == 600);
    r = reset(UINT_MAX);
    assert(qcom_scm_qseecom_service_listeners(&r) == -ELOOP);
    assert(calls == 4096 && responses == 4097 && last_status == QSEECOM_LISTENER_FAILURE);
    r = reset(UINT_MAX);
    service_ms = 10000;
    assert(qcom_scm_qseecom_service_listeners(&r) == -ELOOP);
    assert(calls == 3 && responses == 4 && last_status == QSEECOM_LISTENER_FAILURE);
    r = reset(UINT_MAX);
    fail_service = true;
    assert(qcom_scm_qseecom_service_listeners(&r) == -ELOOP);
    assert(calls == 8 && responses == 9);
    r = reset(UINT_MAX);
    unknown = true;
    assert(qcom_scm_qseecom_service_listeners(&r) == -ELOOP);
    assert(calls == 0 && responses == 9);
    r = reset(1);
    fail_response = true;
    assert(qcom_scm_qseecom_service_listeners(&r) == -EIO && calls == 1);
    r = reset(1);
    blocked = true;
    assert(qcom_scm_qseecom_service_listeners(&r) == -EBUSY);
    r = reset(1);
    r.resp_type = 0;
    assert(qcom_scm_qseecom_service_listeners(&r) == -EPROTO && calls == 0);
    r = reset(1);
    r.data = (uint64_t)UINT32_MAX + 1;
    assert(qcom_scm_qseecom_service_listeners(&r) == -EPROTO && calls == 0);
    puts("QSEECOM: long successful transactions, failure/round/time bounds and malformed replies passed");
}
