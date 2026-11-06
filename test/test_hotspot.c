// Runs the real hotspot.c against fake Wi-Fi Direct objects. Covers the flow
// from Microsoft's sample, every failure branch that matters, late callbacks and
// reference counts. It does not prove anything about real Wi-Fi hardware.

#include <assert.h>
#include <stdio.h>

#include "hotspot.h"
#include "wifidirect_abi.h"

// ---- Fake runtime ----------------------------------------------------------

struct FakeHString { wchar_t *text; };
static int live_strings, live_objects, live_heap;
static BOOL window_gone;
#define QUEUE_MAX 64
static LPARAM queue[QUEUE_MAX];
static int queued;

void *HeapAlloc(HANDLE heap, DWORD flags, size_t size)
{
    (void)heap;
    live_heap++;
    return flags & HEAP_ZERO_MEMORY ? calloc(1, size) : malloc(size);
}
BOOL HeapFree(HANDLE heap, DWORD flags, void *memory)
{
    (void)heap; (void)flags;
    live_heap--;
    free(memory);
    return TRUE;
}
BOOL PostMessageW(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    (void)window; (void)wparam;
    assert(message == WM_HOTSPOT);
    if (window_gone) return FALSE;
    assert(queued < QUEUE_MAX);
    queue[queued++] = lparam;
    return TRUE;
}
int lstrlenW(const wchar_t *t) { return (int)wcslen(t); }
int lstrcmpW(const wchar_t *a, const wchar_t *b) { return wcscmp(a, b); }
wchar_t *lstrcpynW(wchar_t *target, const wchar_t *source, int count)
{
    wcsncpy(target, source, (size_t)count - 1);
    target[count - 1] = 0;
    return target;
}
int wsprintfW(wchar_t *target, const wchar_t *format, ...)
{
    // Windows' %s is wide; on Linux that is %ls.
    wchar_t fixed[64];
    const wchar_t *hit = wcsstr(format, L"%s");
    assert(hit);
    swprintf(fixed, 64, L"%.*ls%%ls%ls", (int)(hit - format), format, hit + 2);
    va_list args;
    va_start(args, format);
    int n = vswprintf(target, 200, fixed, args);
    va_end(args);
    return n;
}
HRESULT WindowsCreateString(const wchar_t *text, UINT32 length, HSTRING *out)
{
    (void)length;
    *out = malloc(sizeof(**out));
    (*out)->text = wcsdup(text);
    live_strings++;
    return S_OK;
}
HRESULT WindowsDeleteString(HSTRING text)
{
    if (text) {
        free(text->text);
        free(text);
        live_strings--;
    }
    return S_OK;
}
const wchar_t *WindowsGetStringRawBuffer(HSTRING text, UINT32 *length)
{
    (void)length;
    return text->text;
}

// ---- Fake objects ----------------------------------------------------------

typedef struct Obj { const void *lpVtbl; int refs; } Obj;
static HRESULT obj_qi(Obj *o, REFIID iid, void **out) { (void)iid; o->refs++; *out = o; return S_OK; }
static ULONG obj_addref(Obj *o) { return (ULONG)++o->refs; }
static ULONG obj_release(Obj *o) { int r = --o->refs; assert(r >= 0); if (r == 0) live_objects--; return (ULONG)r; }
static void *obj_new(Obj *o, const void *vtbl) { o->lpVtbl = vtbl; o->refs = 1; live_objects++; return o; }
#define BASE (void *)obj_qi, (void *)obj_addref, (void *)obj_release, NULL, NULL, NULL

typedef struct DelegateCall DelegateCall;
struct DelegateCallVtbl {
    HRESULT (*QueryInterface)(DelegateCall *, REFIID, void **);
    ULONG (*AddRef)(DelegateCall *);
    ULONG (*Release)(DelegateCall *);
    HRESULT (*Invoke)(DelegateCall *, void *, void *);
    };
struct DelegateCall { const struct DelegateCallVtbl *lpVtbl; };
struct CompletedCallVtbl {
    void *q, *a;
    ULONG (*Release)(DelegateCall *);
    HRESULT (*Invoke)(DelegateCall *, void *, int);
};

static struct Scenario {
    HRESULT activate_publisher, add_status, start, activate_listener, stop;
    BOOL no_endpoints, device_left;
} scenario;

static struct {
    Obj publisher, advertisement, legacy, credential, listener;
    DelegateCall *status_handler, *request_handler;
    int autonomous, legacy_enabled, started, stopped;
    wchar_t ssid[64], password[64];
} f;

static HRESULT cred_put(Obj *o, HSTRING s) { (void)o; wcscpy(f.password, s->text); return S_OK; }
static const void *credential_vtbl[] = {BASE, 0, 0, 0, 0, 0, (void *)cred_put, 0, 0};

static HRESULT legacy_put_enabled(Obj *o, unsigned char v) { (void)o; f.legacy_enabled = v; return S_OK; }
static HRESULT legacy_put_ssid(Obj *o, HSTRING s) { (void)o; wcscpy(f.ssid, s->text); return S_OK; }
static HRESULT legacy_get_pass(Obj *o, Obj **out) { (void)o; *out = obj_new(&f.credential, credential_vtbl); return S_OK; }
static const void *legacy_vtbl[] = {BASE, 0, (void *)legacy_put_enabled, 0, (void *)legacy_put_ssid, (void *)legacy_get_pass, 0};

static HRESULT adv_put_auto(Obj *o, unsigned char v) { (void)o; f.autonomous = v; return S_OK; }
static HRESULT adv_get_legacy(Obj *o, Obj **out) { (void)o; *out = obj_new(&f.legacy, legacy_vtbl); return S_OK; }
static const void *advertisement_vtbl[] = {BASE, 0, 0, 0, 0, 0, (void *)adv_put_auto, (void *)adv_get_legacy};

static HRESULT pub_get_adv(Obj *o, Obj **out) { (void)o; *out = obj_new(&f.advertisement, advertisement_vtbl); return S_OK; }
static HRESULT pub_add(Obj *o, DelegateCall *d, __int64 *t)
{
    (void)o;
    if (FAILED(scenario.add_status)) return scenario.add_status;
    d->lpVtbl->AddRef(d);
    f.status_handler = d;
    *t = 11;
    return S_OK;
}
static HRESULT pub_remove(Obj *o, __int64 t)
{
    (void)o;
    assert(t == 11);
    f.status_handler->lpVtbl->Release(f.status_handler);
    f.status_handler = NULL;
    return S_OK;
}
static HRESULT pub_start(Obj *o) { (void)o; f.started++; return scenario.start; }
static HRESULT pub_stop(Obj *o) { (void)o; f.stopped++; return scenario.stop; }
static const void *publisher_vtbl[] = {BASE, (void *)pub_get_adv, 0, (void *)pub_add, (void *)pub_remove, (void *)pub_start, (void *)pub_stop};

static HRESULT lis_add(Obj *o, DelegateCall *d, __int64 *t)
{
    (void)o;
    d->lpVtbl->AddRef(d);
    f.request_handler = d;
    *t = 22;
    return S_OK;
}
static HRESULT lis_remove(Obj *o, __int64 t)
{
    (void)o;
    assert(t == 22);
    f.request_handler->lpVtbl->Release(f.request_handler);
    f.request_handler = NULL;
    return S_OK;
}
static const void *listener_vtbl[] = {BASE, (void *)lis_add, (void *)lis_remove};

HRESULT RoActivateInstance(HSTRING name, IInspectable **out)
{
    if (wcscmp(name->text, WFD_CLASS_PUBLISHER) == 0) {
        if (FAILED(scenario.activate_publisher)) return scenario.activate_publisher;
        *out = (IInspectable *)obj_new(&f.publisher, publisher_vtbl);
    } else {
        assert(wcscmp(name->text, WFD_CLASS_LISTENER) == 0);
        if (FAILED(scenario.activate_listener)) return scenario.activate_listener;
        *out = (IInspectable *)obj_new(&f.listener, listener_vtbl);
    }
    return S_OK;
}

// Device side
typedef struct FakeDevice {
    Obj base;
    int status;
    wchar_t id[32];
    DelegateCall *handler;
} FakeDevice;

static HRESULT dev_status(FakeDevice *d, int *out) { *out = d->status; return S_OK; }
static HRESULT dev_id(FakeDevice *d, HSTRING *out) { return WindowsCreateString(d->id, 0, out); }
static HRESULT dev_add(FakeDevice *d, DelegateCall *h, __int64 *t) { h->lpVtbl->AddRef(h); d->handler = h; *t = 33; return S_OK; }
static HRESULT dev_remove(FakeDevice *d, __int64 t)
{
    assert(t == 33 && d->handler);
    d->handler->lpVtbl->Release(d->handler);
    d->handler = NULL;
    return S_OK;
}
static Obj host_remote, host_local, pair_obj, vector_obj;
static HRESULT host_name(Obj *o, HSTRING *out)
{
    return WindowsCreateString(o == &host_remote ? L"192.168.137.20" : L"192.168.137.1", 0, out);
}
static const void *host_vtbl[] = {BASE, 0, 0, (void *)host_name};
static HRESULT pair_local(Obj *o, Obj **out) { (void)o; *out = obj_new(&host_local, host_vtbl); return S_OK; }
static HRESULT pair_remote(Obj *o, Obj **out) { (void)o; *out = obj_new(&host_remote, host_vtbl); return S_OK; }
static const void *pair_vtbl[] = {BASE, (void *)pair_local, 0, 0, 0, (void *)pair_remote};
static HRESULT vec_at(Obj *o, UINT32 i, Obj **out) { (void)o; assert(i == 0); *out = obj_new(&pair_obj, pair_vtbl); return S_OK; }
static HRESULT vec_size(Obj *o, UINT32 *out) { (void)o; *out = scenario.no_endpoints ? 0 : 1; return S_OK; }
static const void *vector_vtbl[] = {BASE, (void *)vec_at, (void *)vec_size};
static HRESULT dev_pairs(FakeDevice *d, Obj **out) { (void)d; *out = obj_new(&vector_obj, vector_vtbl); return S_OK; }
static const void *device_vtbl[] = {BASE, (void *)dev_status, (void *)dev_id, (void *)dev_add, (void *)dev_remove, (void *)dev_pairs};

static FakeDevice device;
static Obj operation_obj, statics_obj, information_obj, request_obj, request_args_obj, status_args_obj;

static HRESULT op_results(Obj *o, FakeDevice **out)
{
    (void)o;
    obj_new(&device.base, device_vtbl);
    device.status = scenario.device_left ? WFD_CONNECTION_DISCONNECTED : WFD_CONNECTION_CONNECTED;
    wcscpy(device.id, L"device-1");
    *out = &device;
    return S_OK;
}
static HRESULT op_put(Obj *o, DelegateCall *handler)
{
    assert(handler);
    ((const struct CompletedCallVtbl *)handler->lpVtbl)->Invoke(handler, o, WFD_ASYNC_COMPLETED);
    return S_OK;
}
static const void *operation_vtbl[] = {BASE, (void *)op_put, 0, (void *)op_results};
static HRESULT statics_from_id(Obj *o, HSTRING id, Obj **out)
{
    (void)o;
    assert(wcscmp(id->text, L"requested-id") == 0);
    *out = obj_new(&operation_obj, operation_vtbl);
    return S_OK;
}
static const void *statics_vtbl[] = {BASE, 0, (void *)statics_from_id};
HRESULT RoGetActivationFactory(HSTRING name, REFIID iid, void **out)
{
    assert(wcscmp(name->text, WFD_CLASS_DEVICE) == 0);
    assert(memcmp(iid, &IID_WfdDeviceStatics, sizeof(GUID)) == 0);
    *out = obj_new(&statics_obj, statics_vtbl);
    return S_OK;
}
static HRESULT info_id(Obj *o, HSTRING *out) { (void)o; return WindowsCreateString(L"requested-id", 0, out); }
static const void *information_vtbl[] = {BASE, (void *)info_id};
static HRESULT req_info(Obj *o, Obj **out) { (void)o; *out = obj_new(&information_obj, information_vtbl); return S_OK; }
static const void *request_vtbl[] = {BASE, (void *)req_info};
static HRESULT args_request(Obj *o, Obj **out) { (void)o; *out = obj_new(&request_obj, request_vtbl); return S_OK; }
static const void *request_args_vtbl[] = {BASE, (void *)args_request};

static int fire_status_value, fire_error_value;
static HRESULT sargs_status(Obj *o, int *out) { (void)o; *out = fire_status_value; return S_OK; }
static HRESULT sargs_error(Obj *o, int *out) { (void)o; *out = fire_error_value; return S_OK; }
static const void *status_args_vtbl[] = {BASE, (void *)sargs_status, (void *)sargs_error};

// ---- Harness ---------------------------------------------------------------

static int state_changes;
static HotspotState last_state;
static wchar_t last_message[256], last_log[256], connected_host[64], connected_ip[64], gone_host[64];

static void ev_state(HotspotState s, const wchar_t *m)
{
    state_changes++;
    last_state = s;
    wcscpy(last_message, m ? m : L"");
}
static void ev_connected(const wchar_t *h, const wchar_t *ip) { wcscpy(connected_host, h); wcscpy(connected_ip, ip); }
static void ev_gone(const wchar_t *h) { wcscpy(gone_host, h); }
static void ev_log(const wchar_t *m) { wcscpy(last_log, m); }
static const HotspotEvents events = {ev_state, ev_connected, ev_gone, ev_log};

static void pump(void)
{
    for (int i = 0; i < queued; i++) hotspot_on_message(queue[i]);
    queued = 0;
}
static void fire_status(int status, int error)
{
    fire_status_value = status;
    fire_error_value = error;
    obj_new(&status_args_obj, status_args_vtbl);
    f.status_handler->lpVtbl->Invoke(f.status_handler, &f.publisher, &status_args_obj);
    obj_release(&status_args_obj);
}
static void fire_request(void)
{
    obj_new(&request_args_obj, request_args_vtbl);
    f.request_handler->lpVtbl->Invoke(f.request_handler, &f.listener, &request_args_obj);
    obj_release(&request_args_obj);
}
static void fire_device_status(int status)
{
    device.status = status;
    device.handler->lpVtbl->Invoke(device.handler, &device, NULL);
}
static void assert_clean(void)
{
    assert(live_objects == 0);
    assert(live_strings == 0);
    assert(live_heap == 0);
    assert(hotspot_device_count() == 0);
}
static void reset(void)
{
    memset(&scenario, 0, sizeof(scenario));
    memset(&f, 0, sizeof(f));
    memset(&device, 0, sizeof(device));
    queued = 0;
    state_changes = 0;
    window_gone = FALSE;
    last_message[0] = last_log[0] = connected_host[0] = connected_ip[0] = gone_host[0] = 0;
}
static void start_running(void)
{
    wchar_t error[200];
    assert(hotspot_start(L"MyNet", L"password1", error, 200) == S_OK);
    assert(hotspot_state() == HOTSPOT_STARTING);
    fire_status(WFD_STATUS_STARTED, 0);
    pump();
    assert(hotspot_state() == HOTSPOT_RUNNING);
}
static void stop_cleanly(void)
{
    assert(hotspot_stop() == S_OK);
    assert(hotspot_state() == HOTSPOT_STOPPING);
    fire_status(WFD_STATUS_STOPPED, 0);
    pump();
    assert(hotspot_state() == HOTSPOT_STOPPED);
    assert_clean();
}

// ---- Tests -----------------------------------------------------------------

static void test_configures_ap_like_the_sample(void)
{
    reset();
    start_running();
    assert(f.autonomous == 1);
    assert(f.legacy_enabled == 1);
    assert(wcscmp(f.ssid, L"MyNet") == 0);
    assert(wcscmp(f.password, L"password1") == 0);
    assert(f.started == 1);
    assert(f.request_handler != NULL);
    stop_cleanly();
    assert(f.stopped == 1);
}

static void test_delegates_answer_expected_interfaces(void)
{
    static const GUID other = {1, 2, 3, {4}};
    void *out;
    reset();
    start_running();
    DelegateCall *h = f.status_handler;
    assert(h->lpVtbl->QueryInterface(h, &IID_StatusChangedHandler, &out) == S_OK);
    h->lpVtbl->Release(h);
    assert(h->lpVtbl->QueryInterface(h, &IID_WfdIAgileObject, &out) == S_OK);
    h->lpVtbl->Release(h);
    assert(h->lpVtbl->QueryInterface(h, &IID_WfdIUnknown, &out) == S_OK);
    h->lpVtbl->Release(h);
    assert(h->lpVtbl->QueryInterface(h, &other, &out) == E_NOINTERFACE && out == NULL);
    assert(h->lpVtbl->QueryInterface(h, &IID_ConnectionRequestedHandler, &out) == E_NOINTERFACE);
    stop_cleanly();
}

static void test_device_connects_and_disconnects(void)
{
    reset();
    start_running();
    fire_request();
    pump();
    assert(hotspot_device_count() == 1);
    assert(wcscmp(hotspot_device_host(0), L"192.168.137.20") == 0);
    assert(wcscmp(connected_ip, L"192.168.137.1") == 0);
    fire_device_status(WFD_CONNECTION_DISCONNECTED);
    pump();
    assert(hotspot_device_count() == 0);
    assert(wcscmp(gone_host, L"192.168.137.20") == 0);
    assert(device.handler == NULL && device.base.refs == 0);
    stop_cleanly();
}

static void test_stop_with_device_connected_releases_it(void)
{
    reset();
    start_running();
    fire_request();
    pump();
    stop_cleanly();
}

static void test_device_without_endpoints_is_kept(void)
{
    reset();
    start_running();
    scenario.no_endpoints = TRUE;
    fire_request();
    pump();
    assert(hotspot_device_count() == 1);  // the sample throws here and loses it
    assert(wcscmp(hotspot_device_host(0), L"Device (address pending)") == 0);
    stop_cleanly();
}

static void test_device_gone_before_ui_thread_sees_it(void)
{
    reset();
    start_running();
    scenario.device_left = TRUE;
    fire_request();
    pump();
    assert(hotspot_device_count() == 0);
    assert(device.handler == NULL && device.base.refs == 0);
    stop_cleanly();
}

static void test_abort_reports_reason(void)
{
    reset();
    start_running();
    fire_request();
    pump();
    fire_status(WFD_STATUS_ABORTED, WFD_ERROR_RESOURCE_IN_USE);
    pump();
    assert(hotspot_state() == HOTSPOT_STOPPED);
    assert(wcsstr(last_message, L"Mobile Hotspot"));
    assert_clean();

    reset();
    start_running();
    fire_status(WFD_STATUS_ABORTED, WFD_ERROR_RADIO_NOT_AVAILABLE);
    pump();
    assert(wcsstr(last_message, L"Wi-Fi is turned off"));
    assert_clean();
}

static void test_start_failures_leave_nothing_behind(void)
{
    wchar_t error[200];

    reset();
    scenario.activate_publisher = REGDB_E_CLASSNOTREG;
    assert(hotspot_start(L"a", L"password1", error, 200) == REGDB_E_CLASSNOTREG);
    assert(wcsstr(error, L"Wi-Fi Direct API"));
    assert(hotspot_state() == HOTSPOT_STOPPED);
    assert_clean();

    reset();
    scenario.add_status = E_FAIL;  // the sample does not check this call
    assert(FAILED(hotspot_start(L"a", L"password1", error, 200)));
    assert(wcsstr(error, L"Creating the hotspot failed (0x80004005)"));
    assert(f.started == 0);
    assert_clean();

    reset();
    scenario.start = E_FAIL;
    assert(FAILED(hotspot_start(L"a", L"password1", error, 200)));
    assert(wcsstr(error, L"Starting the hotspot failed"));
    assert(hotspot_state() == HOTSPOT_STOPPED);
    assert_clean();
}

static void test_listener_failure_keeps_hotspot_up(void)
{
    reset();
    scenario.activate_listener = E_FAIL;
    start_running();
    assert(f.request_handler == NULL);
    pump();
    assert(wcsstr(last_log, L"connection listener"));
    stop_cleanly();
}

static void test_late_callbacks_from_old_session_are_ignored(void)
{
    wchar_t error[200];
    reset();
    start_running();
    fire_request();  // result is queued, not yet seen by the UI thread
    assert(queued == 1);
    LPARAM stale_device = queue[0];
    queued = 0;
    hotspot_abandon();
    assert(hotspot_state() == HOTSPOT_STOPPED);

    int before = state_changes;
    hotspot_on_message(stale_device);
    assert(hotspot_device_count() == 0);
    assert(state_changes == before);
    assert_clean();

    // A fresh start works afterwards.
    assert(hotspot_start(L"Again", L"password1", error, 200) == S_OK);
    fire_status(WFD_STATUS_STARTED, 0);
    pump();
    assert(hotspot_state() == HOTSPOT_RUNNING);
    stop_cleanly();
}

static void test_start_while_running_is_refused(void)
{
    wchar_t error[200];
    reset();
    start_running();
    assert(FAILED(hotspot_start(L"b", L"password1", error, 200)));
    assert(hotspot_state() == HOTSPOT_RUNNING);
    stop_cleanly();
}

static void test_started_event_after_stop_requested(void)
{
    wchar_t error[200];
    reset();
    assert(hotspot_start(L"a", L"password1", error, 200) == S_OK);
    assert(hotspot_stop() == S_OK);
    fire_status(WFD_STATUS_STARTED, 0);
    fire_status(WFD_STATUS_STOPPED, 0);
    pump();
    assert(hotspot_state() == HOTSPOT_STOPPED);
    assert(f.listener.refs == 0);
    assert_clean();
}

static void test_event_is_freed_if_window_is_gone(void)
{
    reset();
    start_running();
    window_gone = TRUE;
    fire_request();
    assert(queued == 0);
    window_gone = FALSE;
    assert(device.base.refs == 0);
    stop_cleanly();
}

static void test_failed_stop_can_be_abandoned(void)
{
    reset();
    start_running();
    scenario.stop = E_FAIL;
    assert(FAILED(hotspot_stop()));
    assert(hotspot_state() == HOTSPOT_RUNNING);
    hotspot_abandon();
    assert(hotspot_state() == HOTSPOT_STOPPED);
    assert_clean();
}

int main(void)
{
    hotspot_init((HWND)1, &events);
    test_configures_ap_like_the_sample();
    test_delegates_answer_expected_interfaces();
    test_device_connects_and_disconnects();
    test_stop_with_device_connected_releases_it();
    test_device_without_endpoints_is_kept();
    test_device_gone_before_ui_thread_sees_it();
    test_abort_reports_reason();
    test_start_failures_leave_nothing_behind();
    test_listener_failure_keeps_hotspot_up();
    test_late_callbacks_from_old_session_are_ignored();
    test_start_while_running_is_refused();
    test_started_event_after_stop_requested();
    test_event_is_freed_if_window_is_gone();
    test_failed_stop_can_be_abandoned();
    puts("hotspot lifecycle tests passed (14)");
    return 0;
}
