/* Production TEE invoke with only allocation/session/SCM boundaries replaced. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t u32;
typedef uint64_t u64;
typedef uintptr_t phys_addr_t;
#define SZ_16M (16UL << 20)
#define PAGE_SIZE 4096UL
#define PAGE_ALIGN(n) (((n) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define QCOM_TZMEM_POLICY_STATIC 0
#define GFP_KERNEL 0
#define TEE_SHM_POOL 1
#define TEE_IOCTL_PARAM_ATTR_TYPE_MASK 255
#define TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT 1
#define TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT 5
#define TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT 6
#define TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT 7
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR(p) ((void)(p), -EINVAL)
#define ERR_PTR(n) ((void *)(intptr_t)(n))
#define min(a,b) ((a) < (b) ? (a) : (b))
#define max(a,b) ((a) > (b) ? (a) : (b))
#define upper_32_bits(n) ((u32)((uint64_t)(n) >> 32))
#define lower_32_bits(n) ((u32)(n))
#define mutex_lock(p) ((void)(p))
#define mutex_unlock(p) ((void)(p))
struct tee_shm { unsigned char *data; size_t size; unsigned flags; };
struct tee_param { u64 attr; union {
    struct { struct tee_shm *shm; size_t shm_offs, size; } memref;
    struct { u64 a,b,c; } value;
} u; };
struct tee_ioctl_invoke_arg { u32 func, session, num_params, ret, ret_origin; };
struct dma_buf { void *priv; };
struct qseecom_private_dma { phys_addr_t phys; };
struct qseecom_tee_context { int mutex; void *qtee; struct dma_buf *private_dma; };
struct tee_context { struct qseecom_tee_context *data; };
struct qseecom_tee_session { u32 app_id, app_gen; };
struct qcom_tzmem_pool_config { int policy; size_t initial_size, max_size; };
struct qcom_tzmem_pool { size_t size; };
/* @LIMITS@ */
static struct qseecom_tee_session session = {7, 9};
static bool stale, fail_send, normal_patch, private_patch;
static size_t expected_length, expected_response, expected_extent;
static unsigned sends, pools, allocations;
static unsigned char *staging;
static struct qseecom_tee_session *qseecom_tee_session_find(struct qseecom_tee_context *c, u32 id)
{ (void)c; return id == 3 ? &session : NULL; }
static bool qseecom_tee_app_current(void *qtee, u32 id, u32 generation)
{ (void)qtee; assert(id == 7 && generation == 9); return !stale; }
static int qseecom_private_alloc(struct qseecom_tee_context *c)
{ (void)c; return -ENODEV; }
static size_t tee_shm_get_size(struct tee_shm *s) { return s->size; }
static void *tee_shm_get_va(struct tee_shm *s, size_t offset)
{ return offset <= s->size ? s->data + offset : ERR_PTR(-EINVAL); }
static struct qcom_tzmem_pool *qcom_tzmem_pool_new(const struct qcom_tzmem_pool_config *c)
{
    assert(c->policy == 0 && c->initial_size == c->max_size);
    struct qcom_tzmem_pool *p = malloc(sizeof(*p));
    assert(p); p->size = c->initial_size; pools++; return p;
}
static void *qcom_tzmem_alloc(struct qcom_tzmem_pool *p, size_t n, int flags)
{
    assert(!flags && n == p->size && !staging);
    staging = malloc(n); assert(staging); allocations++; return staging;
}
static void qcom_tzmem_pool_free(struct qcom_tzmem_pool *p) { free(p); pools--; }
static void qcom_tzmem_free(void *p) { assert(p == staging); free(p); staging = NULL; allocations--; }
static phys_addr_t qcom_tzmem_to_phys(void *p) { return 0x100000 + ((unsigned char *)p - staging); }
static void put_unaligned_le32(u32 n, void *p) { memcpy(p, &n, 4); }
static void put_unaligned_le64(u64 n, void *p) { memcpy(p, &n, 8); }
static int qcom_scm_qseecom_app_send(u32 id, void *req, size_t qlen, void *rsp, size_t rlen)
{
    assert(id == 7 && req == staging && qlen == expected_length);
    assert((unsigned char *)rsp - (unsigned char *)req == (ptrdiff_t)expected_response);
    assert(expected_extent >= qlen && expected_extent >= expected_response + rlen);
    for (size_t i = qlen; i < expected_response; i++) assert(!staging[i]);
    if (normal_patch || private_patch) {
        u32 address; memcpy(&address, staging + 12, 4);
        assert(address == (private_patch ? 0x23450000 : 0x100000 + PAGE_ALIGN(expected_extent)));
        if (normal_patch) {
            assert(staging[PAGE_ALIGN(expected_extent)] == 0xa3);
            staging[PAGE_ALIGN(expected_extent)] = 0xb4;
        }
    }
    sends++;
    if (fail_send) return -EIO;
    memset(rsp, 0x6b, rlen);
    return 0;
}
/* @FUNCTIONS@ */
static struct tee_param memref(struct tee_shm *s, size_t offset, size_t size, u64 attr)
{ return (struct tee_param){.attr=attr,.u.memref={s,offset,size}}; }
static void trial(size_t qlen, size_t offset, size_t rlen, bool separate)
{
    unsigned char data[40960], other[40960], extra[16];
    memset(data, 0x4a, sizeof(data)); memset(other, 0x5a, sizeof(other)); memset(extra, 0xa3, sizeof(extra));
    struct tee_shm shared = {data,sizeof(data),TEE_SHM_POOL}, apart = {other,sizeof(other),TEE_SHM_POOL};
    struct tee_shm patch = {extra,sizeof(extra),TEE_SHM_POOL};
    struct qseecom_private_dma private = {.phys=0x23450000};
    struct dma_buf dma = {&private};
    struct qseecom_tee_context c = {.private_dma=&dma}; struct tee_context context = {&c};
    struct tee_ioctl_invoke_arg arg = {.session=3,.num_params=2};
    struct tee_param p[2 + 2 * QSEECOM_TEE_MAX_PATCH] = {
        memref(&shared,0,qlen,5),memref(separate ? &apart : &shared,offset,rlen,6)};
    if (normal_patch || private_patch) {
        arg.num_params=4;
        p[2]=(struct tee_param){.attr=1,.u.value={12,4,private_patch ? 1 : 0}};
        p[3]=private_patch ? (struct tee_param){.attr=1} : memref(&patch,0,sizeof(extra),7);
    }
    expected_length=qlen; expected_response=separate ? qlen : offset;
    expected_extent=max(qlen,expected_response+rlen);
    unsigned before=sends;
    int rc=qseecom_tee_invoke_func(&context,&arg,p);
    assert(sends == before + 1 && !pools && !allocations);
    if (fail_send) {
        assert(rc == -EIO && data[0] == 0x4a && other[0] == 0x5a);
        return;
    }
    assert(!rc && !arg.ret && !arg.ret_origin);
    unsigned char *response=(separate ? other : data)+offset;
    for (size_t i=0;i<rlen;i++)
        assert(response[i] == (!offset && !separate && (normal_patch || private_patch) && i>=12 && i<16 ? 0 : 0x6b));
    if (normal_patch) assert(extra[0] == 0xb4);
}
int main(void)
{
    trial(4,4,40956,false); trial(24,24,40936,false);
    trial(46,48,40912,false); trial(117,120,40840,false);
    trial(108,0,108,false); trial(8,64,256,true);
    normal_patch=true;
    trial(46,48,40912,false); trial(108,0,108,false);
    normal_patch=false; private_patch=true;
    trial(108,0,108,false); private_patch=false;
    fail_send=true; trial(46,48,40912,false); fail_send=false;
    struct tee_shm a={0}, b={0};
    struct tee_param p[2]={memref(&a,0,46,5),memref(&a,48,32,6)};
    size_t offset,extent;
    assert(!qseecom_tee_staging_layout(p,46,32,&offset,&extent) && offset==48 && extent==80);
    p[1].u.memref.shm_offs=40;
    assert(qseecom_tee_staging_layout(p,46,32,&offset,&extent)==-EINVAL);
    p[0].u.memref.shm_offs=100; p[1].u.memref.shm_offs=0;
    assert(qseecom_tee_staging_layout(p,46,32,&offset,&extent)==-EINVAL);
    p[0].u.memref.shm_offs=0; p[1].u.memref.shm_offs=SIZE_MAX;
    assert(qseecom_tee_staging_layout(p,46,32,&offset,&extent)==-EINVAL);
    p[1].u.memref.shm=&b;
    assert(qseecom_tee_staging_layout(p,QSEECOM_TEE_MAX_XFER,1,&offset,&extent)==-EINVAL);
    assert(qseecom_tee_staging_layout(p,0,1,&offset,&extent)==-EINVAL);
    assert(!pools && !allocations);
    puts("QSEECOM production invoke: unpadded CBOR, aligned response, aliases, patched buffers, bounds and cleanup passed");
}
