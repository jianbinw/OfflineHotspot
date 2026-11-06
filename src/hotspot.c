// Derived from WlanHostedNetworkWinRT.cpp in Microsoft's Windows-classic-samples
// (Samples/WiFiDirectLegacyAP), MIT License. See THIRD_PARTY_NOTICES.txt.
//
// Differences from the sample:
//  - Every WinRT call is checked, including add_StatusChanged.
//  - A device with no endpoint pairs is kept rather than treated as an error.
//  - State lives on the UI thread, so the device list needs no locks.
//  - Late callbacks from an earlier start are recognised and dropped.

#include "hotspot.h"
#include "wifidirect_abi.h"

#include <roapi.h>
#include <winstring.h>

#define MAX_DEVICES 32
#define MAX_DEVICE_ID 512
#define MAX_HOST 64
#define MAX_EVENT_TEXT 200

typedef enum EventKind {
    EVENT_STATUS,
    EVENT_DEVICE_CONNECTED,
    EVENT_DEVICE_DISCONNECTED,
    EVENT_LOG
} EventKind;

typedef struct Event {
    EventKind kind;
    LONG session;
    int status;
    int error;
    WfdDevice *device;
    __int64 token;
    BOOL watching; // token is registered on device
    wchar_t id[MAX_DEVICE_ID];
    wchar_t host[MAX_HOST];
    wchar_t local_ip[MAX_HOST];
    wchar_t text[MAX_EVENT_TEXT];
} Event;

typedef struct Device {
    WfdDevice *device;
    __int64 token;
    wchar_t id[MAX_DEVICE_ID];
    wchar_t host[MAX_HOST];
} Device;

static struct {
    HWND window;
    const HotspotEvents *events;
    HotspotState state;
    LONG session;
    WfdPublisher *publisher;
    WfdAdvertisement *advertisement;
    WfdLegacySettings *legacy_settings;
    WfdConnectionListener *listener;
    __int64 status_token;
    __int64 request_token;
    BOOL has_status_token;
    BOOL has_request_token;
    Device devices[MAX_DEVICES];
    int device_count;
} g;

#define RELEASE(p)                                                              \
    do {                                                                        \
        if (p) {                                                                \
            (p)->lpVtbl->Release(p);                                            \
            (p) = NULL;                                                         \
        }                                                                       \
    } while (0)

// ---- Helpers ---------------------------------------------------------------

static void copy_hstring(wchar_t *target, int target_chars, HSTRING source)
{
    lstrcpynW(target, source ? WindowsGetStringRawBuffer(source, NULL) : L"", target_chars);
}

static HRESULT make_hstring(const wchar_t *text, HSTRING *out)
{
    return WindowsCreateString(text, (UINT32)lstrlenW(text), out);
}

static BOOL guid_equal(REFIID a, const GUID *b)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    for (int i = 0; i < (int)sizeof(GUID); i++) {
        if (x[i] != y[i]) {
            return FALSE;
        }
    }
    return TRUE;
}

static Event *event_new(EventKind kind, LONG session)
{
    Event *event = (Event *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Event));
    if (event) {
        event->kind = kind;
        event->session = session;
    }
    return event;
}

static void event_free(Event *event)
{
    if (event->device && event->watching) {
        event->device->lpVtbl->remove_ConnectionStatusChanged(event->device, event->token);
    }
    RELEASE(event->device);
    HeapFree(GetProcessHeap(), 0, event);
}

// Takes ownership of event, including when the window has already gone.
static void event_post(Event *event)
{
    if (!PostMessageW(g.window, WM_HOTSPOT, 0, (LPARAM)event)) {
        event_free(event);
    }
}

static void post_log(LONG session, const wchar_t *step, HRESULT hr)
{
    Event *event = event_new(EVENT_LOG, session);
    if (event) {
        wsprintfW(event->text, L"%s failed (0x%08X)", step, (unsigned)hr);
        event_post(event);
    }
}

// ---- Delegates -------------------------------------------------------------
//
// WinRT event handlers are COM objects. Each one carries the session it was
// created for, so a callback from an earlier start can be identified.

typedef struct Delegate Delegate;

typedef struct DelegateVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(Delegate *, REFIID, void **);
    ULONG(STDMETHODCALLTYPE *AddRef)(Delegate *);
    ULONG(STDMETHODCALLTYPE *Release)(Delegate *);
    HRESULT(STDMETHODCALLTYPE *Invoke)(Delegate *, void *, void *);
} DelegateVtbl;

// IAsyncOperationCompletedHandler passes an AsyncStatus value, not a pointer.
typedef struct CompletedDelegateVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(Delegate *, REFIID, void **);
    ULONG(STDMETHODCALLTYPE *AddRef)(Delegate *);
    ULONG(STDMETHODCALLTYPE *Release)(Delegate *);
    HRESULT(STDMETHODCALLTYPE *Invoke)(Delegate *, WfdAsyncOperationDevice *, int);
} CompletedDelegateVtbl;

struct Delegate {
    const void *lpVtbl;
    volatile LONG refs;
    const GUID *iid;
    LONG session;
};

static ULONG STDMETHODCALLTYPE delegate_add_ref(Delegate *self)
{
    return (ULONG)InterlockedIncrement(&self->refs);
}

static ULONG STDMETHODCALLTYPE delegate_release(Delegate *self)
{
    LONG refs = InterlockedDecrement(&self->refs);
    if (refs == 0) {
        HeapFree(GetProcessHeap(), 0, self);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE delegate_query_interface(Delegate *self, REFIID riid, void **out)
{
    if (!out) {
        return E_POINTER;
    }
    if (guid_equal(riid, self->iid) || guid_equal(riid, &IID_WfdIUnknown) ||
        guid_equal(riid, &IID_WfdIAgileObject)) {
        delegate_add_ref(self);
        *out = self;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static Delegate *delegate_new(const void *vtbl, const GUID *iid, LONG session)
{
    Delegate *self = (Delegate *)HeapAlloc(GetProcessHeap(), 0, sizeof(Delegate));
    if (self) {
        self->lpVtbl = vtbl;
        self->refs = 1;
        self->iid = iid;
        self->session = session;
    }
    return self;
}

// TypedEventHandler<WiFiDirectAdvertisementPublisher, ...StatusChangedEventArgs>
static HRESULT STDMETHODCALLTYPE on_status_changed(Delegate *self, void *sender, void *raw_args)
{
    WfdPublisherStatusChangedEventArgs *args = (WfdPublisherStatusChangedEventArgs *)raw_args;
    int status = WFD_STATUS_CREATED;
    int error = WFD_ERROR_SUCCESS;
    (void)sender;

    HRESULT hr = args->lpVtbl->get_Status(args, &status);
    if (SUCCEEDED(hr)) {
        hr = args->lpVtbl->get_Error(args, &error);
    }
    if (FAILED(hr)) {
        post_log(self->session, L"Reading hotspot status", hr);
        return hr;
    }

    Event *event = event_new(EVENT_STATUS, self->session);
    if (event) {
        event->status = status;
        event->error = error;
        event_post(event);
    }
    return S_OK;
}

static const DelegateVtbl status_changed_vtbl = {
    delegate_query_interface, delegate_add_ref, delegate_release, on_status_changed};

// TypedEventHandler<WiFiDirectDevice, Object>
static HRESULT STDMETHODCALLTYPE on_connection_status_changed(Delegate *self, void *raw_sender, void *args)
{
    WfdDevice *sender = (WfdDevice *)raw_sender;
    int status = WFD_CONNECTION_CONNECTED;
    HSTRING id = NULL;
    (void)args;

    HRESULT hr = sender->lpVtbl->get_ConnectionStatus(sender, &status);
    if (FAILED(hr)) {
        post_log(self->session, L"Reading device status", hr);
        return hr;
    }
    if (status != WFD_CONNECTION_DISCONNECTED) {
        return S_OK;
    }

    hr = sender->lpVtbl->get_DeviceId(sender, &id);
    if (FAILED(hr)) {
        post_log(self->session, L"Reading device ID", hr);
        return hr;
    }
    Event *event = event_new(EVENT_DEVICE_DISCONNECTED, self->session);
    if (event) {
        copy_hstring(event->id, MAX_DEVICE_ID, id);
        event_post(event);
    }
    WindowsDeleteString(id);
    return S_OK;
}

static const DelegateVtbl connection_status_changed_vtbl = {
    delegate_query_interface, delegate_add_ref, delegate_release, on_connection_status_changed};

// Fills in the device's IP addresses. A peer can connect before they are known,
// so a failure here only leaves the text empty.
static void read_endpoints(WfdDevice *device, Event *event)
{
    WfdEndpointPairVector *pairs = NULL;
    WfdEndpointPair *pair = NULL;
    WfdHostName *name = NULL;
    HSTRING text = NULL;
    UINT32 size = 0;

    if (SUCCEEDED(device->lpVtbl->GetConnectionEndpointPairs(device, &pairs)) &&
        SUCCEEDED(pairs->lpVtbl->get_Size(pairs, &size)) && size > 0 &&
        SUCCEEDED(pairs->lpVtbl->GetAt(pairs, 0, &pair))) {
        if (SUCCEEDED(pair->lpVtbl->get_RemoteHostName(pair, &name)) && name &&
            SUCCEEDED(name->lpVtbl->get_DisplayName(name, &text))) {
            copy_hstring(event->host, MAX_HOST, text);
            WindowsDeleteString(text);
            text = NULL;
        }
        RELEASE(name);
        if (SUCCEEDED(pair->lpVtbl->get_LocalHostName(pair, &name)) && name &&
            SUCCEEDED(name->lpVtbl->get_DisplayName(name, &text))) {
            copy_hstring(event->local_ip, MAX_HOST, text);
            WindowsDeleteString(text);
        }
        RELEASE(name);
    }
    RELEASE(pair);
    RELEASE(pairs);
}

// IAsyncOperationCompletedHandler<WiFiDirectDevice>
static HRESULT STDMETHODCALLTYPE on_device_ready(Delegate *self, WfdAsyncOperationDevice *operation, int status)
{
    WfdDevice *device = NULL;
    HSTRING id = NULL;

    if (status != WFD_ASYNC_COMPLETED) {
        post_log(self->session, L"Accepting a device", (HRESULT)status);
        return S_OK;
    }

    HRESULT hr = operation->lpVtbl->GetResults(operation, &device);
    if (FAILED(hr) || !device) {
        post_log(self->session, L"Accepting a device", hr);
        return S_OK;
    }

    Event *event = event_new(EVENT_DEVICE_CONNECTED, self->session);
    if (!event) {
        RELEASE(device);
        return E_OUTOFMEMORY;
    }
    event->device = device; // event now owns the reference

    hr = device->lpVtbl->get_DeviceId(device, &id);
    if (FAILED(hr)) {
        post_log(self->session, L"Reading device ID", hr);
        event_free(event);
        return S_OK;
    }
    copy_hstring(event->id, MAX_DEVICE_ID, id);
    WindowsDeleteString(id);

    read_endpoints(device, event);

    Delegate *handler = delegate_new(&connection_status_changed_vtbl, &IID_ConnectionStatusChangedHandler, self->session);
    if (!handler) {
        event_free(event);
        return E_OUTOFMEMORY;
    }
    hr = device->lpVtbl->add_ConnectionStatusChanged(device, handler, &event->token);
    delegate_release(handler);
    if (FAILED(hr)) {
        post_log(self->session, L"Watching a device", hr);
        event_free(event);
        return S_OK;
    }
    event->watching = TRUE;

    event_post(event);
    return S_OK;
}

static const CompletedDelegateVtbl device_ready_vtbl = {
    delegate_query_interface, delegate_add_ref, delegate_release, on_device_ready};

// Wi-Fi security has already checked the password, so every request is accepted,
// matching the sample's default autoaccept behaviour.
static HRESULT accept_connection(LONG session, WfdConnectionRequestedEventArgs *args)
{
    WfdConnectionRequest *request = NULL;
    WfdDeviceInformation *information = NULL;
    WfdDeviceStatics *statics = NULL;
    WfdAsyncOperationDevice *operation = NULL;
    Delegate *handler = NULL;
    HSTRING id = NULL;
    HSTRING class_name = NULL;

    HRESULT hr = args->lpVtbl->GetConnectionRequest(args, &request);
    if (SUCCEEDED(hr)) {
        hr = request->lpVtbl->get_DeviceInformation(request, &information);
    }
    if (SUCCEEDED(hr)) {
        hr = information->lpVtbl->get_Id(information, &id);
    }
    if (SUCCEEDED(hr)) {
        hr = make_hstring(WFD_CLASS_DEVICE, &class_name);
    }
    if (SUCCEEDED(hr)) {
        hr = RoGetActivationFactory(class_name, &IID_WfdDeviceStatics, (void **)&statics);
    }
    if (SUCCEEDED(hr)) {
        hr = statics->lpVtbl->FromIdAsync(statics, id, &operation);
    }
    if (SUCCEEDED(hr)) {
        handler = delegate_new(&device_ready_vtbl, &IID_FromIdCompletedHandler, session);
        hr = handler ? S_OK : E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hr)) {
        hr = operation->lpVtbl->put_Completed(operation, handler);
    }

    if (handler) {
        delegate_release(handler);
    }
    RELEASE(operation);
    RELEASE(statics);
    RELEASE(information);
    RELEASE(request);
    WindowsDeleteString(class_name);
    WindowsDeleteString(id);
    return hr;
}

// TypedEventHandler<WiFiDirectConnectionListener, ...ConnectionRequestedEventArgs>
static HRESULT STDMETHODCALLTYPE on_connection_requested(Delegate *self, void *sender, void *raw_args)
{
    (void)sender;
    HRESULT hr = accept_connection(self->session, (WfdConnectionRequestedEventArgs *)raw_args);
    if (FAILED(hr)) {
        post_log(self->session, L"Accepting a device", hr);
    }
    return S_OK;
}

static const DelegateVtbl connection_requested_vtbl = {
    delegate_query_interface, delegate_add_ref, delegate_release, on_connection_requested};

// ---- UI-thread state -------------------------------------------------------

static HRESULT activate(const wchar_t *class_name, REFIID iid, void **out)
{
    HSTRING name = NULL;
    IInspectable *instance = NULL;

    HRESULT hr = make_hstring(class_name, &name);
    if (SUCCEEDED(hr)) {
        hr = RoActivateInstance(name, &instance);
    }
    if (SUCCEEDED(hr)) {
        hr = instance->lpVtbl->QueryInterface(instance, iid, out);
        instance->lpVtbl->Release(instance);
    }
    WindowsDeleteString(name);
    return hr;
}

static void set_state(HotspotState state, const wchar_t *message)
{
    g.state = state;
    if (g.events->on_state) {
        g.events->on_state(state, message);
    }
}

static void log_text(const wchar_t *text)
{
    if (g.events->on_log) {
        g.events->on_log(text);
    }
}

static void drop_device(int index)
{
    Device *device = &g.devices[index];
    device->device->lpVtbl->remove_ConnectionStatusChanged(device->device, device->token);
    RELEASE(device->device);
    for (int i = index; i < g.device_count - 1; i++) {
        g.devices[i] = g.devices[i + 1];
    }
    g.device_count--;
}

// Bumping the session makes any callback still in flight harmless.
static void cleanup(void)
{
    g.session++;

    while (g.device_count > 0) {
        drop_device(g.device_count - 1);
    }
    if (g.listener) {
        if (g.has_request_token) {
            g.listener->lpVtbl->remove_ConnectionRequested(g.listener, g.request_token);
            g.has_request_token = FALSE;
        }
        RELEASE(g.listener);
    }
    if (g.publisher && g.has_status_token) {
        g.publisher->lpVtbl->remove_StatusChanged(g.publisher, g.status_token);
        g.has_status_token = FALSE;
    }
    RELEASE(g.legacy_settings);
    RELEASE(g.advertisement);
    RELEASE(g.publisher);
}

static void start_listener(void)
{
    HRESULT hr = activate(WFD_CLASS_LISTENER, &IID_WfdConnectionListener, (void **)&g.listener);
    if (FAILED(hr)) {
        post_log(g.session, L"Creating the connection listener", hr);
        return;
    }

    Delegate *handler = delegate_new(&connection_requested_vtbl, &IID_ConnectionRequestedHandler, g.session);
    if (!handler) {
        post_log(g.session, L"Creating the connection handler", E_OUTOFMEMORY);
        return;
    }
    hr = g.listener->lpVtbl->add_ConnectionRequested(g.listener, handler, &g.request_token);
    delegate_release(handler);
    if (FAILED(hr)) {
        post_log(g.session, L"Listening for connections", hr);
        return;
    }
    g.has_request_token = TRUE;
    log_text(L"Listening for devices");
}

static const wchar_t *abort_reason(int error)
{
    switch (error) {
    case WFD_ERROR_RADIO_NOT_AVAILABLE:
        return L"Wi-Fi is turned off. Turn it on, then try again.";
    case WFD_ERROR_RESOURCE_IN_USE:
        return L"Wi-Fi Direct is in use. Turn off Windows Mobile Hotspot and close other "
               L"hotspot apps, then try again.";
    default:
        return L"Windows stopped the hotspot without giving a reason. Your Wi-Fi driver may "
               L"not support Wi-Fi Direct.";
    }
}

static void handle_status(int status, int error)
{
    switch (status) {
    case WFD_STATUS_STARTED:
        if (g.state == HOTSPOT_STARTING) {
            set_state(HOTSPOT_RUNNING, NULL);
            start_listener();
        }
        break;
    case WFD_STATUS_STOPPED:
        cleanup();
        set_state(HOTSPOT_STOPPED, NULL);
        break;
    case WFD_STATUS_ABORTED:
        cleanup();
        set_state(HOTSPOT_STOPPED, abort_reason(error));
        break;
    default:
        break;
    }
}

static void handle_device_connected(Event *event)
{
    int status = WFD_CONNECTION_DISCONNECTED;
    WfdDevice *device = event->device;

    // It may have left before this message arrived. Its disconnect event would
    // have found nothing to remove, so check again before adding it.
    if (FAILED(device->lpVtbl->get_ConnectionStatus(device, &status)) ||
        status != WFD_CONNECTION_CONNECTED || g.device_count >= MAX_DEVICES) {
        return;
    }

    // A device that rejoins can do so without a disconnect event for its old entry.
    for (int i = 0; i < g.device_count; i++) {
        if (lstrcmpW(g.devices[i].id, event->id) == 0) {
            drop_device(i);
            break;
        }
    }

    Device *slot = &g.devices[g.device_count++];
    slot->device = device;
    event->device = NULL; // ownership moves to the list
    slot->token = event->token;
    lstrcpynW(slot->id, event->id, MAX_DEVICE_ID);
    lstrcpynW(slot->host, event->host[0] ? event->host : L"Device (address pending)", MAX_HOST);

    if (g.events->on_device_connected) {
        g.events->on_device_connected(slot->host, event->local_ip);
    }
}

static void handle_device_disconnected(const Event *event)
{
    for (int i = 0; i < g.device_count; i++) {
        if (lstrcmpW(g.devices[i].id, event->id) == 0) {
            wchar_t host[MAX_HOST];
            lstrcpynW(host, g.devices[i].host, MAX_HOST);
            drop_device(i);
            if (g.events->on_device_disconnected) {
                g.events->on_device_disconnected(host);
            }
            return;
        }
    }
}

// ---- Public API ------------------------------------------------------------

void hotspot_init(HWND window, const HotspotEvents *events)
{
    g.window = window;
    g.events = events;
    g.state = HOTSPOT_STOPPED;
}

static void describe_failure(wchar_t *error, int error_chars, const wchar_t *step, HRESULT hr)
{
    wchar_t text[160];
    if (hr == REGDB_E_CLASSNOTREG) {
        lstrcpynW(error, L"This version of Windows does not provide the Wi-Fi Direct API.", error_chars);
        return;
    }
    wsprintfW(text, L"%s failed (0x%08X).", step, (unsigned)hr);
    lstrcpynW(error, text, error_chars);
}

HRESULT hotspot_start(const wchar_t *ssid, const wchar_t *passphrase, wchar_t *error, int error_chars)
{
    WfdPasswordCredential *credential = NULL;
    HSTRING ssid_text = NULL;
    HSTRING passphrase_text = NULL;
    const wchar_t *step = L"Creating the hotspot";

    if (g.state != HOTSPOT_STOPPED) {
        lstrcpynW(error, L"The hotspot is already running.", error_chars);
        return E_FAIL;
    }

    cleanup();

    HRESULT hr = activate(WFD_CLASS_PUBLISHER, &IID_WfdPublisher, (void **)&g.publisher);
    if (SUCCEEDED(hr)) {
        Delegate *handler = delegate_new(&status_changed_vtbl, &IID_StatusChangedHandler, g.session);
        hr = handler ? g.publisher->lpVtbl->add_StatusChanged(g.publisher, handler, &g.status_token) : E_OUTOFMEMORY;
        if (handler) {
            delegate_release(handler);
        }
        g.has_status_token = SUCCEEDED(hr);
    }

    // The access point is a Wi-Fi Direct group owner that also accepts ordinary
    // Wi-Fi clients, so both of these flags are required.
    if (SUCCEEDED(hr)) {
        step = L"Configuring the hotspot";
        hr = g.publisher->lpVtbl->get_Advertisement(g.publisher, &g.advertisement);
    }
    if (SUCCEEDED(hr)) {
        hr = g.advertisement->lpVtbl->put_IsAutonomousGroupOwnerEnabled(g.advertisement, 1);
    }
    if (SUCCEEDED(hr)) {
        hr = g.advertisement->lpVtbl->get_LegacySettings(g.advertisement, &g.legacy_settings);
    }
    if (SUCCEEDED(hr)) {
        hr = g.legacy_settings->lpVtbl->put_IsEnabled(g.legacy_settings, 1);
    }

    if (SUCCEEDED(hr)) {
        step = L"Setting the network name";
        hr = make_hstring(ssid, &ssid_text);
    }
    if (SUCCEEDED(hr)) {
        hr = g.legacy_settings->lpVtbl->put_Ssid(g.legacy_settings, ssid_text);
    }

    if (SUCCEEDED(hr)) {
        step = L"Setting the password";
        hr = g.legacy_settings->lpVtbl->get_Passphrase(g.legacy_settings, &credential);
    }
    if (SUCCEEDED(hr)) {
        hr = make_hstring(passphrase, &passphrase_text);
    }
    if (SUCCEEDED(hr)) {
        hr = credential->lpVtbl->put_Password(credential, passphrase_text);
    }

    if (SUCCEEDED(hr)) {
        step = L"Starting the hotspot";
        g.state = HOTSPOT_STARTING;
        hr = g.publisher->lpVtbl->Start(g.publisher);
    }

    RELEASE(credential);
    WindowsDeleteString(ssid_text);
    WindowsDeleteString(passphrase_text);

    if (FAILED(hr)) {
        g.state = HOTSPOT_STOPPED;
        cleanup();
        describe_failure(error, error_chars, step, hr);
        return hr;
    }

    set_state(HOTSPOT_STARTING, NULL);
    return S_OK;
}

HRESULT hotspot_stop(void)
{
    if (!g.publisher) {
        return S_FALSE;
    }
    HRESULT hr = g.publisher->lpVtbl->Stop(g.publisher);
    if (SUCCEEDED(hr)) {
        set_state(HOTSPOT_STOPPING, NULL);
    }
    return hr;
}

void hotspot_abandon(void)
{
    if (g.publisher) {
        g.publisher->lpVtbl->Stop(g.publisher);
    }
    cleanup();
    g.state = HOTSPOT_STOPPED;
}

void hotspot_on_message(LPARAM lparam)
{
    Event *event = (Event *)lparam;

    if (event->session == g.session) {
        // A stale event is simply freed, which also detaches its device.
        switch (event->kind) {
        case EVENT_STATUS:
            handle_status(event->status, event->error);
            break;
        case EVENT_DEVICE_CONNECTED:
            handle_device_connected(event);
            break;
        case EVENT_DEVICE_DISCONNECTED:
            handle_device_disconnected(event);
            break;
        case EVENT_LOG:
            log_text(event->text);
            break;
        }
    }

    event_free(event);
}

HotspotState hotspot_state(void)
{
    return g.state;
}

int hotspot_device_count(void)
{
    return g.device_count;
}

const wchar_t *hotspot_device_host(int index)
{
    return g.devices[index].host;
}
