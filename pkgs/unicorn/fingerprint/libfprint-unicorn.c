/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define FP_COMPONENT "unicorn"
#include "drivers_api.h"
#include "ipc.h"
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#ifndef UF_RUNTIME
#error UF_RUNTIME must name the packaged native worker
#endif
#ifndef UF_DSP_PATH
#error UF_DSP_PATH must name the packaged DSP firmware and native callback paths
#endif
#define FPI_TYPE_DEVICE_UNICORN (fpi_device_unicorn_get_type())
G_DECLARE_FINAL_TYPE(FpiDeviceUnicorn, fpi_device_unicorn, FPI, DEVICE_UNICORN, FpDevice)
struct _FpiDeviceUnicorn {
    FpDevice parent;
    GMutex mutex;
    GSubprocess *worker;
    int channel;
    guint32 sequence, profile, stages, action_request;
    char identity[80];
    gboolean busy;
};
G_DEFINE_TYPE(FpiDeviceUnicorn, fpi_device_unicorn, FP_TYPE_DEVICE)
struct work {
    struct uf_message request;
    GCancellable *cancel;
    GMainContext *context;
};
struct progress {
    FpiDeviceUnicorn *self;
    struct uf_message message;
};
static void work_free(gpointer data)
{
    struct work *w = data;
    g_clear_object(&w->cancel);
    g_clear_pointer(&w->context, g_main_context_unref);
    g_free(w);
}
static void stop_worker(FpiDeviceUnicorn *self)
{
    g_mutex_lock(&self->mutex);
    if (self->worker)
        g_subprocess_force_exit(self->worker);
    g_clear_object(&self->worker);
    if (self->channel >= 0)
        close(self->channel);
    self->channel = -1;
    g_mutex_unlock(&self->mutex);
}
static GError *native_error(const struct uf_message *m)
{
    if (m->code == 1058 || m->code == 1059 || m->code == 1142)
        return g_error_new(G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                           "TA rejected enrollment authorization (%s); a valid challenge-bound "
                           "signed hardware authentication token is required",
                           uf_error_name(m->code));
    if (m->error == ENOKEY)
        return g_error_new_literal(G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                                  "No enrollment authorization provider is running; use "
                                  "unicorn-fingerprint-authorize enroll USER in a local terminal");
    if (m->error == ENODATA)
        return g_error_new(
            FP_DEVICE_ERROR, FP_DEVICE_ERROR_GENERAL,
            "Device calibration or OLED identity is unavailable or rejected by the TA");
    if (m->error == ENXIO)
        return g_error_new(FP_DEVICE_ERROR, FP_DEVICE_ERROR_GENERAL,
                           "No active local Wayland fingerprint hint session is ready");
    if (m->code == 1015)
        return fpi_device_error_new(FP_DEVICE_ERROR_DATA_DUPLICATE);
    if (m->code == 1007)
        return fpi_device_error_new(FP_DEVICE_ERROR_DATA_FULL);
    if (m->code == 1049)
        return fpi_device_error_new(FP_DEVICE_ERROR_DATA_NOT_FOUND);
    return g_error_new(G_IO_ERROR, g_io_error_from_errno(m->error),
                       "Unicorn native action: %s; TA %s (%d)", g_strerror(m->error),
                       uf_error_name(m->code), m->code);
}
static GError *retry_error(gint32 code)
{
    FpDeviceRetry retry = FP_DEVICE_RETRY_GENERAL;
    if (code == 1013 || code == 1054)
        retry = FP_DEVICE_RETRY_CENTER_FINGER;
    if (code == 1047 || code == 1067)
        retry = FP_DEVICE_RETRY_REMOVE_FINGER;
    return fpi_device_retry_new_msg(retry, "%s (%d)", uf_error_name(code), code);
}
static gboolean progress_main(gpointer data)
{
    struct progress *p = data;
    FpiDeviceUnicorn *self = p->self;
    FpDevice *device = FP_DEVICE(self);
    if (self->busy &&
        (guint32)g_atomic_int_get((gint *)&self->action_request) == p->message.request) {
        FpiDeviceAction action = fpi_device_get_current_action(device);
        if (p->message.operation == UF_EVENT_FINGER)
            fpi_device_report_finger_status_changes(
                device, p->message.flags ? FP_FINGER_STATUS_PRESENT : 0,
                p->message.flags ? 0 : FP_FINGER_STATUS_PRESENT);
        else if (action == FPI_DEVICE_ACTION_ENROLL) {
            if (p->message.operation == UF_EVENT_RETRY)
                fpi_device_enroll_progress(device,
                                           self->stages - MIN(self->stages, p->message.stages),
                                           NULL, retry_error(p->message.code));
            else if (p->message.operation == UF_EVENT_PROGRESS)
                fpi_device_enroll_progress(
                    device, self->stages - MIN(self->stages, p->message.stages), NULL, NULL);
        }
    }
    g_object_unref(self);
    g_free(p);
    return G_SOURCE_REMOVE;
}
static gboolean valid_reply(FpiDeviceUnicorn *self, const struct uf_message *q,
                            const struct uf_message *r)
{
    if (r->error < 0 || r->error > 4095 ||
        (q->operation != UF_OP_INIT && r->profile != self->profile))
        return FALSE;
    for (unsigned i = 0; i < r->count; i++)
        if (r->prints[i].group || !r->prints[i].finger ||
            uf_template_in_set(r->prints[i], r->prints, i))
            return FALSE;
    gboolean capture = q->operation == UF_OP_ENROLL || q->operation == UF_OP_VERIFY ||
                       q->operation == UF_OP_IDENTIFY;
    if (r->operation == UF_EVENT_FINGER)
        return capture && !r->error && !r->count && r->flags <= 1;
    if (r->operation == UF_EVENT_PROGRESS || r->operation == UF_EVENT_RETRY)
        return q->operation == UF_OP_ENROLL && !r->error && !r->flags &&
               r->stages <= self->stages;
    if (r->operation != UF_EVENT_DONE)
        return FALSE;
    if (r->error)
        return TRUE;
    if (q->operation == UF_OP_VERIFY || q->operation == UF_OP_IDENTIFY)
        return (r->flags == 0 && !r->count && !r->code) ||
               (r->flags == 1 && r->count == 1 && !r->code) ||
               (r->flags == 2 && !r->count && r->code);
    return !r->flags && !r->code &&
           (q->operation == UF_OP_LIST || (q->operation == UF_OP_ENROLL ? r->count == 1 : !r->count));
}
static gboolean exchange(FpiDeviceUnicorn *self, struct work *w, struct uf_message *reply,
                         GError **error)
{
    struct uf_message request = w->request;
    request.magic = UF_IPC_MAGIC;
    request.version = UF_IPC_VERSION;
    request.request = ++self->sequence;
    if (!request.request) {
        g_set_error_literal(error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO,
                            "Native request counter exhausted");
        return FALSE;
    }
    if (w->request.operation != UF_OP_INIT)
        g_atomic_int_set((gint *)&self->action_request, request.request);
    if (g_cancellable_set_error_if_cancelled(w->cancel, error))
        return FALSE;
    if (send(self->channel, &request, sizeof(request), MSG_NOSIGNAL) != sizeof(request))
        goto io_error;
    gint64 deadline = g_get_monotonic_time() + 130 * G_TIME_SPAN_SECOND;
    for (;;) {
        if (g_cancellable_set_error_if_cancelled(w->cancel, error))
            return FALSE;
        int left = (deadline - g_get_monotonic_time()) / 1000;
        if (left <= 0) {
            errno = ETIMEDOUT;
            goto io_error;
        }
        struct pollfd p = {self->channel, POLLIN, 0};
        int rc = poll(&p, 1, MIN(left, 100));
        if (rc < 0) {
            if (errno == EINTR)
                continue;
            goto io_error;
        }
        if (!rc)
            continue;
        ssize_t n = recv(self->channel, reply, sizeof(*reply), MSG_TRUNC);
        if (n != sizeof(*reply)) {
            errno = EPIPE;
            goto io_error;
        }
        if (reply->magic != UF_IPC_MAGIC || reply->version != UF_IPC_VERSION ||
            reply->request != request.request || reply->count > UF_TEMPLATE_MAX ||
            !valid_reply(self, &request, reply)) {
            errno = EPROTO;
            goto io_error;
        }
        if (reply->operation == UF_EVENT_DONE) {
            if (reply->error) {
                g_propagate_error(error, native_error(reply));
                return FALSE;
            }
            return TRUE;
        }
        if (reply->operation != UF_EVENT_PROGRESS && reply->operation != UF_EVENT_RETRY &&
            reply->operation != UF_EVENT_FINGER) {
            errno = EPROTO;
            goto io_error;
        }
        struct progress *progress = g_new0(struct progress, 1);
        progress->self = g_object_ref(self);
        progress->message = *reply;
        /* invoke_full may call inline if this worker can acquire the context.
         * libfprint state belongs exclusively to the caller's main context. */
        GSource *source = g_idle_source_new();
        g_source_set_priority(source, G_PRIORITY_DEFAULT);
        g_source_set_callback(source, progress_main, progress, NULL);
        g_source_attach(source, w->context);
        g_source_unref(source);
    }
io_error:
    if (g_cancellable_set_error_if_cancelled(w->cancel, error))
        return FALSE;
    g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "Native worker transport: %s",
                g_strerror(errno));
    return FALSE;
}
static gboolean ensure_worker(FpiDeviceUnicorn *self, struct work *w, GError **error)
{
    if (self->worker)
        return TRUE;
    if (g_cancellable_set_error_if_cancelled(w->cancel, error))
        return FALSE;
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair)) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "Native socket: %s",
                    g_strerror(errno));
        return FALSE;
    }
    g_autoptr(GSubprocessLauncher) launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_NONE);
    g_subprocess_launcher_take_fd(launcher, pair[1], 3);
    g_subprocess_launcher_setenv(launcher, "ADSP_LIBRARY_PATH", UF_DSP_PATH, TRUE);
    g_subprocess_launcher_setenv(launcher, "LD_LIBRARY_PATH", UF_CALLBACK_PATH, TRUE);
    g_mutex_lock(&self->mutex);
    self->worker = g_subprocess_launcher_spawn(launcher, error, UF_RUNTIME, "worker", NULL);
    self->channel = pair[0];
    g_mutex_unlock(&self->mutex);
    if (!self->worker) {
        close(self->channel);
        self->channel = -1;
        return FALSE;
    }
    struct work init = {
        .request = {.operation = UF_OP_INIT}, .cancel = w->cancel, .context = w->context};
    struct uf_message reply;
    if (!exchange(self, &init, &reply, error))
        return FALSE;
    if (!reply.stages || reply.stages > 128 || !memchr(reply.device, 0, sizeof(reply.device)) ||
        strlen(reply.device) != 70 || strspn(reply.device, "0123456789abcdef") != 70 ||
        (reply.profile != UF_PROFILE_GFUNI && reply.profile != UF_PROFILE_GFUNI6)) {
        g_set_error_literal(error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_PROTO,
                            "Invalid native sensor identity");
        return FALSE;
    }
    if (self->identity[0] && (strcmp(self->identity, reply.device) ||
                              self->profile != reply.profile || self->stages != reply.stages)) {
        g_set_error_literal(error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_REMOVED,
                            "Sensor identity changed while open");
        return FALSE;
    }
    if (!self->identity[0]) {
        self->profile = reply.profile;
        self->stages = reply.stages;
        g_strlcpy(self->identity, reply.device, sizeof(self->identity));
    }
    return TRUE;
}
static void worker_thread(GTask *task, gpointer object, gpointer data, GCancellable *unused)
{
    (void)unused;
    FpiDeviceUnicorn *self = object;
    struct work *w = data;
    g_autoptr(GError) error = NULL;
    struct uf_message *reply = g_new0(struct uf_message, 1);
    gboolean ok;
    if (w->request.operation == UF_OP_CLOSE) {
        stop_worker(self);
        ok = TRUE;
    } else {
        ok = ensure_worker(self, w, &error);
        if (ok && w->request.operation != UF_OP_INIT)
            ok = exchange(self, w, reply, &error);
        if (ok && w->request.operation == UF_OP_INIT) {
            reply->profile = self->profile;
            reply->stages = self->stages;
        }
    }
    if (!ok) {
        stop_worker(self);
        g_free(reply);
        g_task_return_error(task, g_steal_pointer(&error));
    } else
        g_task_return_pointer(task, reply, g_free);
}
static gboolean parse_print(FpiDeviceUnicorn *self, FpPrint *print, struct uf_template *locator)
{
    g_autoptr(GVariant) data = NULL;
    guint32 version, profile;
    const char *identity;
    g_object_get(print, "fpi-data", &data, NULL);
    if (!data || !g_variant_is_of_type(data, G_VARIANT_TYPE("(uusuu)")))
        return FALSE;
    g_variant_get(data, "(uu&suu)", &version, &profile, &identity, &locator->group,
                  &locator->finger);
    return version == 2 && profile == self->profile && !strcmp(identity, self->identity) &&
           locator->group == 0 && locator->finger != 0;
}
static void set_print(FpiDeviceUnicorn *self, FpPrint *print, struct uf_template locator)
{
    fpi_print_set_type(print, FPI_PRINT_RAW);
    fpi_print_set_device_stored(print, TRUE);
    g_object_set(
        print, "fpi-data",
        g_variant_new("(uusuu)", 2, self->profile, self->identity, locator.group, locator.finger),
        NULL);
}
static void done(GObject *object, GAsyncResult *result, gpointer unused)
{
    (void)unused;
    FpiDeviceUnicorn *self = FPI_DEVICE_UNICORN(object);
    FpDevice *device = FP_DEVICE(self);
    struct work *w = g_task_get_task_data(G_TASK(result));
    g_autoptr(GError) error = NULL;
    g_autofree struct uf_message *reply = g_task_propagate_pointer(G_TASK(result), &error);
    self->busy = FALSE;
    fpi_device_report_finger_status_changes(device, 0,
                                            FP_FINGER_STATUS_PRESENT | FP_FINGER_STATUS_NEEDED);
    switch (w->request.operation) {
    case UF_OP_INIT:
        if (reply)
            fpi_device_set_nr_enroll_stages(device, self->stages);
        fpi_device_open_complete(device, g_steal_pointer(&error));
        break;
    case UF_OP_CLOSE:
        fpi_device_close_complete(device, g_steal_pointer(&error));
        break;
    case UF_OP_DELETE:
        fpi_device_delete_complete(device, g_steal_pointer(&error));
        break;
    case UF_OP_LIST: {
        GPtrArray *prints = NULL;
        if (reply) {
            prints = g_ptr_array_new_with_free_func(g_object_unref);
            for (unsigned i = 0; i < reply->count; i++) {
                FpPrint *print = g_object_ref_sink(fp_print_new(device));
                set_print(self, print, reply->prints[i]);
                g_ptr_array_add(prints, print);
            }
        }
        fpi_device_list_complete(device, prints, g_steal_pointer(&error));
        break;
    }
    case UF_OP_ENROLL: {
        FpPrint *print = NULL;
        if (reply && reply->count == 1) {
            fpi_device_get_enroll_data(device, &print);
            set_print(self, print, reply->prints[0]);
        } else if (reply)
            error = fpi_device_error_new(FP_DEVICE_ERROR_PROTO);
        fpi_device_enroll_complete(device, print ? g_object_ref(print) : NULL,
                                   g_steal_pointer(&error));
        break;
    }
    case UF_OP_VERIFY:
    case UF_OP_IDENTIFY: {
        FpPrint *match = NULL, *scan = NULL;
        if (reply && (reply->flags & 1)) {
            if (reply->count != 1 ||
                !uf_template_in_set(reply->prints[0], w->request.prints, w->request.count))
                error = fpi_device_error_new(FP_DEVICE_ERROR_PROTO);
            else {
                scan = g_object_ref_sink(fp_print_new(device));
                set_print(self, scan, reply->prints[0]);
                if (w->request.operation == UF_OP_VERIFY)
                    fpi_device_get_verify_data(device, &match);
                else {
                    GPtrArray *gallery;
                    fpi_device_get_identify_data(device, &gallery);
                    for (unsigned i = 0; i < gallery->len; i++) {
                        struct uf_template locator;
                        if (parse_print(self, gallery->pdata[i], &locator) &&
                            locator.group == reply->prints[0].group &&
                            locator.finger == reply->prints[0].finger) {
                            match = gallery->pdata[i];
                            break;
                        }
                    }
                }
                if (!match)
                    error = fpi_device_error_new(FP_DEVICE_ERROR_PROTO);
            }
        }
        if (reply && (reply->flags & 2)) {
            if (w->request.operation == UF_OP_VERIFY)
                fpi_device_verify_report(device, FPI_MATCH_ERROR, NULL, retry_error(reply->code));
            else
                fpi_device_identify_report(device, NULL, NULL, retry_error(reply->code));
        } else if (!error) {
            if (w->request.operation == UF_OP_VERIFY)
                fpi_device_verify_report(device, match ? FPI_MATCH_SUCCESS : FPI_MATCH_FAIL, scan,
                                         NULL);
            else
                fpi_device_identify_report(device, match, scan, NULL);
        }
        if (w->request.operation == UF_OP_VERIFY)
            fpi_device_verify_complete(device, g_steal_pointer(&error));
        else
            fpi_device_identify_complete(device, g_steal_pointer(&error));
        g_clear_object(&scan);
        break;
    }
    default:
        g_assert_not_reached();
    }
}
static void start(FpDevice *device, unsigned operation)
{
    FpiDeviceUnicorn *self = FPI_DEVICE_UNICORN(device);
    struct work *w = g_new0(struct work, 1);
    w->request.operation = operation;
    GCancellable *cancel = fpi_device_get_cancellable(device);
    if (cancel)
        w->cancel = g_object_ref(cancel);
    w->context = g_main_context_ref_thread_default();
    g_atomic_int_set((gint *)&self->action_request, 0);
    self->busy = TRUE;
    g_autoptr(GTask) task = g_task_new(self, NULL, done, NULL);
    g_task_set_task_data(task, w, work_free);
    FpPrint *print = NULL;
    GPtrArray *gallery = NULL;
    if (operation == UF_OP_VERIFY || operation == UF_OP_DELETE) {
        if (operation == UF_OP_VERIFY)
            fpi_device_get_verify_data(device, &print);
        else
            fpi_device_get_delete_data(device, &print);
        w->request.count = 1;
        if (!parse_print(self, print, &w->request.prints[0]))
            goto invalid;
    } else if (operation == UF_OP_IDENTIFY) {
        fpi_device_get_identify_data(device, &gallery);
        if (!gallery->len) {
            /* fprintd probes duplicates even before the first enrollment.
             * An empty permitted gallery cannot yield an authorized match. */
            g_task_return_pointer(task, g_new0(struct uf_message, 1), g_free);
            return;
        }
        if (gallery->len > UF_TEMPLATE_MAX)
            goto invalid;
        w->request.count = gallery->len;
        for (unsigned i = 0; i < gallery->len; i++)
            if (!parse_print(self, gallery->pdata[i], &w->request.prints[i]))
                goto invalid;
    }
    if (operation == UF_OP_ENROLL || operation == UF_OP_VERIFY || operation == UF_OP_IDENTIFY)
        fpi_device_report_finger_status_changes(device, FP_FINGER_STATUS_NEEDED, 0);
    g_task_run_in_thread(task, worker_thread);
    return;
invalid:
    g_task_return_new_error(task, FP_DEVICE_ERROR, FP_DEVICE_ERROR_DATA_INVALID,
                            "Invalid Unicorn version, sensor identity or template locator");
}
static void unicorn_open(FpDevice *d)
{
    start(d, UF_OP_INIT);
}
static void unicorn_close(FpDevice *d)
{
    start(d, UF_OP_CLOSE);
}
static void unicorn_list(FpDevice *d)
{
    start(d, UF_OP_LIST);
}
static void unicorn_enroll(FpDevice *d)
{
    start(d, UF_OP_ENROLL);
}
static void unicorn_verify(FpDevice *d)
{
    start(d, UF_OP_VERIFY);
}
static void unicorn_identify(FpDevice *d)
{
    start(d, UF_OP_IDENTIFY);
}
static void unicorn_delete(FpDevice *d)
{
    start(d, UF_OP_DELETE);
}
static void unicorn_cancel(FpDevice *d)
{
    FpiDeviceUnicorn *self = FPI_DEVICE_UNICORN(d);
    g_mutex_lock(&self->mutex);
    if (self->worker)
        g_subprocess_force_exit(self->worker);
    g_mutex_unlock(&self->mutex);
}
static void unicorn_probe(FpDevice *d)
{
    fpi_device_probe_complete(d, "unicorn-native-v1", NULL, NULL);
}
static void finalize(GObject *object)
{
    FpiDeviceUnicorn *self = FPI_DEVICE_UNICORN(object);
    stop_worker(self);
    g_mutex_clear(&self->mutex);
    G_OBJECT_CLASS(fpi_device_unicorn_parent_class)->finalize(object);
}
static void fpi_device_unicorn_init(FpiDeviceUnicorn *self)
{
    self->channel = -1;
    g_mutex_init(&self->mutex);
}
static const FpIdEntry ids[] = {{.udev_types = FPI_DEVICE_UDEV_SUBTYPE_MISC,
                                 .misc_name = "unicorn-fingerprint",
                                 .misc_compatible = "xiaomi,unicorn-fingerprint"},
                                {.udev_types = 0}};
static void fpi_device_unicorn_class_init(FpiDeviceUnicornClass *klass)
{
    G_OBJECT_CLASS(klass)->finalize = finalize;
    FpDeviceClass *c = FP_DEVICE_CLASS(klass);
    c->id = FP_COMPONENT;
    c->full_name = "Unicorn native Goodix optical fingerprint";
    c->type = FP_DEVICE_TYPE_UDEV;
    c->id_table = ids;
    c->scan_type = FP_SCAN_TYPE_PRESS;
    c->nr_enroll_stages = 20;
    c->temp_hot_seconds = -1;
    c->probe = unicorn_probe;
    c->open = unicorn_open;
    c->close = unicorn_close;
    c->list = unicorn_list;
    c->delete = unicorn_delete;
    c->enroll = unicorn_enroll;
    c->verify = unicorn_verify;
    c->identify = unicorn_identify;
    c->cancel = unicorn_cancel;
    fpi_device_class_auto_initialize_features(c);
}
