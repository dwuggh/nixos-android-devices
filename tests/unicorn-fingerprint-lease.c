/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
#define to_delayed_work(p) container_of(p,struct delayed_work,work)
#define time_before(a,b) ((long)((a)-(b))<0)
#define kzalloc_obj(x) calloc(1,sizeof(x))
#define kfree(p) do { frees++;free(p); } while(0)
#define MISC_DYNAMIC_MINOR 255
struct kref { unsigned refs; };
static void kref_init(struct kref*r){r->refs=1;}
static void kref_get(struct kref*r){assert(r->refs);r->refs++;}
static void kref_put(struct kref*r,void(*destroy)(struct kref*)){assert(r->refs);if(!--r->refs)destroy(r);}
struct mutex { bool held; };
static void mutex_init(struct mutex*m){m->held=false;}
static void mutex_lock(struct mutex*m){assert(!m->held);m->held=true;}
static void mutex_unlock(struct mutex*m){assert(m->held);m->held=false;}
static void mutex_destroy(struct mutex*m){assert(!m->held);}
struct work_struct { int unused; };
struct delayed_work { struct work_struct work;void(*run)(struct work_struct*);bool pending;unsigned long delay; };
#define INIT_DELAYED_WORK(w,f) do {(w)->run=(f);(w)->pending=false;} while(0)
static void cancel_delayed_work_sync(struct delayed_work*w){w->pending=false;}
static void mod_delayed_work(int queue,struct delayed_work*w,unsigned long delay){(void)queue;w->pending=true;w->delay=delay;}
static int system_wq,registered,frees,ends;
static unsigned long jiffies;
struct miscdevice { unsigned minor;const char*name;const void*fops;unsigned mode; };
static int misc_register(struct miscdevice*m){assert(m->mode==0600);registered++;return 0;}
static void misc_deregister(struct miscdevice*m){(void)m;assert(registered);registered--;}
static const int l2s_fod_ops;
struct l2s_fod;
struct drm_device { struct { struct mutex connection_mutex; } mode_config; };
struct drm_crtc_state { bool active; };
struct drm_crtc { struct drm_crtc_state *state; struct mutex mutex; };
struct drm_connector_state { struct drm_crtc *crtc; };
struct drm_connector { struct drm_device *dev; struct drm_connector_state *state; unsigned refs; };
struct l2s_panel { struct l2s_fod*fod;struct mutex lock;bool fod_active,fod_acquiring;struct drm_connector *connector; };
static void drm_connector_get(struct drm_connector*c){c->refs++;}
static void drm_connector_put(struct drm_connector*c){assert(c->refs>0);c->refs--;}
static int lock_error,wake_error,begin_error;
static bool self_refresh;
static int wakes;
struct drm_modeset_acquire_ctx {struct mutex*held[2];unsigned count;};
static void drm_modeset_acquire_init(struct drm_modeset_acquire_ctx*a,int flags){(void)flags;memset(a,0,sizeof(*a));}
static void drm_modeset_acquire_fini(struct drm_modeset_acquire_ctx*a){assert(!a->count);}
static int drm_modeset_lock(struct mutex*m,struct drm_modeset_acquire_ctx*a){if(lock_error)return lock_error;assert(a->count<2);mutex_lock(m);a->held[a->count++]=m;return 0;}
static void drm_modeset_drop_locks(struct drm_modeset_acquire_ctx*a){while(a->count)mutex_unlock(a->held[--a->count]);}
static int drm_modeset_backoff(struct drm_modeset_acquire_ctx*a){drm_modeset_drop_locks(a);lock_error=0;return 0;}
static int l2s_backlight_wake(struct l2s_panel*p){assert(p->fod_acquiring&&!p->lock.held);wakes++;if(wake_error)return wake_error;if(self_refresh){self_refresh=false;p->connector->state->crtc->state->active=true;}return 0;}
static int l2s_fod_begin_locked(struct l2s_panel*p,struct drm_crtc*c){assert(p->lock.held&&p->connector->dev->mode_config.connection_mutex.held&&c->mutex.held&&c->state->active&&!self_refresh);if(begin_error)return begin_error;p->fod_active=true;return 0;}
static void cancel_delayed_work(struct delayed_work*w){w->pending=false;}
#define msecs_to_jiffies(x) (x)
#define __user
#define copy_from_user(d,s,n) (memcpy(d,s,n),0)
#define copy_to_user(d,s,n) (memcpy(d,s,n),0)
#define L2S_FOD_INFO 1
#define L2S_FOD_ACQUIRE 2
#define L2S_FOD_RELEASE 3
typedef uint32_t u32;
struct l2s_fod_info {u32 version,width,height,x,y,w,h,active;};
struct inode { int unused; };
struct file { void*private_data; };
static int l2s_fod_end_locked(struct l2s_panel*p){assert(p->lock.held);if(p->fod_active){ends++;p->fod_active=false;}return 0;}
/* @FOD_STRUCT@ */
/* @FOD_FUNCTIONS@ */
int main(void)
{
    struct l2s_panel panel={0};assert(!l2s_fod_init(&panel));assert(registered==1);
    struct l2s_fod*fod=panel.fod;struct file first={&fod->misc},second={&fod->misc};
    assert(!l2s_fod_open(NULL,&first)&&fod->ref.refs==2);
    assert(l2s_fod_open(NULL,&second)==-EBUSY&&fod->ref.refs==2);
    struct drm_device device={0};struct drm_crtc_state state={0};struct drm_crtc crtc={.state=&state};
    struct drm_connector_state connection={&crtc};struct drm_connector connector={&device,&connection,1};
    panel.connector=&connector;u32 timeout=1500;
    self_refresh=true;
    assert(!l2s_fod_ioctl(&first,L2S_FOD_ACQUIRE,(unsigned long)&timeout));
    assert(panel.fod_active&&!panel.fod_acquiring&&state.active&&!self_refresh&&wakes==1&&connector.refs==1&&fod->timeout.pending);
    struct l2s_fod_info info;
    assert(!l2s_fod_ioctl(&first,L2S_FOD_INFO,(unsigned long)&info)&&info.active==1);
    assert(!l2s_fod_ioctl(&first,L2S_FOD_RELEASE,0)&&!panel.fod_active&&!fod->timeout.pending);
    state.active=false;
    assert(l2s_fod_ioctl(&first,L2S_FOD_ACQUIRE,(unsigned long)&timeout)==-EHOSTDOWN&&!state.active);
    state.active=true;wake_error=-EIO;
    assert(l2s_fod_ioctl(&first,L2S_FOD_ACQUIRE,(unsigned long)&timeout)==-EIO);
    wake_error=0;lock_error=-EIO;
    assert(l2s_fod_ioctl(&first,L2S_FOD_ACQUIRE,(unsigned long)&timeout)==-EIO);
    lock_error=0;begin_error=-ETIMEDOUT;
    assert(l2s_fod_ioctl(&first,L2S_FOD_ACQUIRE,(unsigned long)&timeout)==-ETIMEDOUT&&!fod->timeout.pending);
    begin_error=0;lock_error=-EDEADLK;
    assert(!l2s_fod_ioctl(&first,L2S_FOD_ACQUIRE,(unsigned long)&timeout)&&!crtc.mutex.held);
    assert(!l2s_fod_ioctl(&first,L2S_FOD_RELEASE,0));
    panel.connector=NULL;
    assert(l2s_fod_ioctl(&first,L2S_FOD_ACQUIRE,(unsigned long)&timeout)==-ENODEV);
    assert(!panel.fod_acquiring&&!panel.lock.held&&!fod->mutex.held&&!device.mode_config.connection_mutex.held&&connector.refs==1);
    timeout=1501;
    assert(l2s_fod_ioctl(&first,L2S_FOD_ACQUIRE,(unsigned long)&timeout)==-EINVAL);
    ends=0;

    panel.fod_active=true;fod->deadline=100;jiffies=60;
    l2s_fod_timeout(&fod->timeout.work);assert(panel.fod_active&&ends==0&&fod->timeout.pending&&fod->timeout.delay==40);
    jiffies=100;l2s_fod_timeout(&fod->timeout.work);assert(!panel.fod_active&&ends==1);
    assert(!l2s_fod_close(NULL,&first)&&fod->ref.refs==1&&!fod->timeout.pending);
    first.private_data=&fod->misc;assert(!l2s_fod_open(NULL,&first));panel.fod_active=true;
    assert(!l2s_fod_close(NULL,&first)&&!panel.fod_active&&ends==2);
    first.private_data=&fod->misc;assert(!l2s_fod_open(NULL,&first));panel.fod_active=true;
    l2s_fod_destroy(&panel);assert(!panel.fod&&!registered&&!fod->ctx&&frees==0&&ends==3);
    second.private_data=&fod->misc;assert(l2s_fod_open(NULL,&second)==-ENODEV);
    assert(!l2s_fod_close(NULL,&first)&&frees==1);
    l2s_fod_destroy(&panel);assert(frees==1);
    puts("panel lease: exclusive fd, renewal deadline, timeout, close, removal with an open fd and final ownership passed");
}
