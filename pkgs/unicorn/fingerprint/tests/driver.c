/* SPDX-License-Identifier: MIT
 * Real libfprint public API + Unicorn driver, mock native worker boundary. */
#include <assert.h>
#include <fprint.h>
#include <gio/gio.h>
#include <stdio.h>
#include <string.h>
static GObject *created;
static GError *creation_error;
static GThread *main_thread;
static void create_done(GObject *o, GAsyncResult *r, gpointer p)
{
    (void)p;
    created = g_async_initable_new_finish(G_ASYNC_INITABLE(o), r, &creation_error);
}
static void progress(FpDevice *d, gint stage, FpPrint *p, gpointer data, GError *error)
{
    (void)d;
    (void)p;
    assert(g_thread_self() == main_thread);
    assert(g_main_context_is_owner(g_main_context_default()));
    assert(!error);
    assert(stage == 2);
    (*(unsigned *)data)++;
}
static gboolean cancel_later(gpointer p)
{
    g_cancellable_cancel(p);
    return G_SOURCE_REMOVE;
}
static FpDevice *new_device(void)
{
    GType type = g_type_from_name("FpiDeviceUnicorn");
    assert(type);
    created = NULL;
    creation_error = NULL;
    g_async_initable_new_async(type, G_PRIORITY_DEFAULT, NULL, create_done, NULL,
                               "fpi-udev-data-misc", "/dev/mock-unicorn", NULL);
    while (!created && !creation_error)
        g_main_context_iteration(NULL, TRUE);
    assert(!creation_error);
    return FP_DEVICE(created);
}
static void verification(FpDevice *d, FpPrint *print, const char *mode, gboolean expected,
                         GQuark domain)
{
    g_autoptr(GError) error = NULL;
    gboolean matched = FALSE;
    /* The worker inherits this only when a new device is opened. */
    (void)mode;
    gboolean ok = fp_device_verify_sync(d, print, NULL, NULL, NULL, &matched, NULL, &error);
    if (domain) {
        assert(!ok && error && error->domain == domain);
    } else {
        assert(ok && !error && matched == expected);
    }
}
int main(void)
{
    main_thread = g_thread_self();
    g_autoptr(FpContext) context = fp_context_new();
    g_autoptr(GError) error = NULL;
    g_autoptr(FpDevice) device = new_device();
    assert(fp_device_open_sync(device, NULL, &error));
    assert(!error);
    g_autoptr(GPtrArray) prints = fp_device_list_prints_sync(device, NULL, &error);
    assert(prints && prints->len == 1 && !error);
    g_autoptr(FpPrint) template = g_object_ref_sink(fp_print_new(device));
    unsigned events = 0;
    g_autoptr(FpPrint) enrolled =
        fp_device_enroll_sync(device, template, NULL, progress, &events, &error);
    assert(enrolled && !error && events == 1 && fp_print_get_device_stored(enrolled));
    guchar *serialized = NULL;
    gsize length = 0;
    assert(fp_print_serialize(enrolled, &serialized, &length, &error));
    g_autoptr(FpPrint) restored = fp_print_deserialize(serialized, length, &error);
    g_free(serialized);
    assert(restored && !error && fp_print_equal(enrolled, restored));
    verification(device, restored, "yes", TRUE, 0);
    g_autoptr(GPtrArray) gallery = g_ptr_array_new_with_free_func(g_object_unref);
    FpPrint *empty_match = NULL, *empty_scan = NULL;
    assert(fp_device_identify_sync(device, gallery, NULL, NULL, NULL,
                                   &empty_match, &empty_scan, &error));
    assert(!error && !empty_match && !empty_scan);
    g_ptr_array_add(gallery, g_object_ref(restored));
    FpPrint *identified = NULL, *scan = NULL;
    assert(fp_device_identify_sync(device, gallery, NULL, NULL, NULL, &identified, &scan, &error));
    assert(!error && identified == restored && scan);
    g_clear_object(&identified);
    g_clear_object(&scan);
    assert(fp_device_delete_print_sync(device, restored, NULL, &error) && !error);
    g_clear_pointer(&prints, g_ptr_array_unref);
    prints = fp_device_list_prints_sync(device, NULL, &error);
    assert(prints && prints->len == 0 && !error);
    assert(fp_device_close_sync(device, NULL, &error) && !error);
    const char *auth_codes[] = {"1058", "1059", "1142"};
    for (unsigned i = 0; i < G_N_ELEMENTS(auth_codes); i++) {
        g_setenv("UF_TEST_ENROLL_CODE", auth_codes[i], TRUE);
        assert(fp_device_open_sync(device, NULL, &error) && !error);
        g_autoptr(FpPrint) rejected_template = g_object_ref_sink(fp_print_new(device));
        unsigned progress_count = 0;
        g_autoptr(FpPrint) rejected = fp_device_enroll_sync(
            device, rejected_template, NULL, progress, &progress_count, &error);
        assert(!rejected && !progress_count);
        assert(g_error_matches(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED));
        assert(strstr(error->message, "authorization") && strstr(error->message, "signed"));
        g_clear_error(&error);
        assert(fp_device_close_sync(device, NULL, &error) && !error);
    }
    g_unsetenv("UF_TEST_ENROLL_CODE");
    const char *modes[] = {"none", "outside", "retry", "flags", "zero", "profile"};
    for (unsigned i = 0; i < G_N_ELEMENTS(modes); i++) {
        g_setenv("UF_TEST_MATCH", modes[i], TRUE);
        assert(fp_device_open_sync(device, NULL, &error) && !error);
        verification(device, restored, modes[i], FALSE,
                     i == 1   ? FP_DEVICE_ERROR
                     : i == 2 ? FP_DEVICE_RETRY
                     : i > 2 ? G_IO_ERROR : 0);
        assert(fp_device_close_sync(device, NULL, &error) && !error);
    }
    g_setenv("UF_TEST_MATCH", "wait", TRUE);
    assert(fp_device_open_sync(device, NULL, &error));
    g_autoptr(GCancellable) cancel = g_cancellable_new();
    g_timeout_add(30, cancel_later, cancel);
    gboolean matched = FALSE;
    assert(!fp_device_verify_sync(device, restored, cancel, NULL, NULL, &matched, NULL, &error));
    assert(g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED));
    g_clear_error(&error);
    g_setenv("UF_TEST_MATCH", "yes", TRUE);
    verification(device, restored, "yes", TRUE, 0); /* Fresh worker after cancellation. */
    assert(fp_device_close_sync(device, NULL, &error) && !error);
    puts("libfprint: real async open/list/enroll/verify/delete, locator serialization, gallery "
         "rejection, retry and cancellation recovery passed");
}
