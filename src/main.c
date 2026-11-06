// Offline Hotspot: a small window around hotspot.c.

#include <winsock2.h>
#include <windows.h>
#include <commctrl.h>
#include <iphlpapi.h>
#include <bcrypt.h>
#include <roapi.h>

#include "hotspot.h"
#include "logic.h"
#include "qrwindow.h"
#include "resource.h"
#include "settings.h"

#define TIMER_START_TIMEOUT 1
#define TIMER_STOP_TIMEOUT 2
#define TIMER_FIND_IP 3

#define START_TIMEOUT_MS 20000
#define STOP_TIMEOUT_MS 5000
#define FIND_IP_INTERVAL_MS 1000

#define DEFAULT_SSID L"OfflineHotspot"
#define NO_IP L"Not available"
#define WAITING_FOR_IP L"Waiting for Windows to assign one..."

static HWND g_dialog;
static BOOL g_ip_known;

static void append_log(const wchar_t *text)
{
    HWND log = GetDlgItem(g_dialog, IDC_LOG);
    SYSTEMTIME now;
    wchar_t line[320];
    int length;

    GetLocalTime(&now);
    wsprintfW(line, L"%02d:%02d:%02d  ", now.wHour, now.wMinute, now.wSecond);
    lstrcpynW(line + lstrlenW(line), text, 280);
    lstrcatW(line, L"\r\n");

    length = GetWindowTextLengthW(log);
    SendMessageW(log, EM_SETSEL, length, length);
    SendMessageW(log, EM_REPLACESEL, FALSE, (LPARAM)line);
}

static void show_ip(const wchar_t *ip)
{
    g_ip_known = TRUE;
    KillTimer(g_dialog, TIMER_FIND_IP);
    SetDlgItemTextW(g_dialog, IDC_IP, ip);
}

// Looks for the Wi-Fi Direct virtual adapter's address, which Windows assigns a
// moment after the access point starts.
static BOOL find_hotspot_ip(wchar_t *ip)
{
    static const wchar_t adapter_name[] = L"Wi-Fi Direct";
    ULONG size = 16 * 1024;
    IP_ADAPTER_ADDRESSES *addresses = NULL;
    ULONG result = ERROR_BUFFER_OVERFLOW;
    BOOL found = FALSE;
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;

    for (int attempt = 0; attempt < 3 && result == ERROR_BUFFER_OVERFLOW; attempt++) {
        if (addresses) {
            HeapFree(GetProcessHeap(), 0, addresses);
        }
        addresses = (IP_ADAPTER_ADDRESSES *)HeapAlloc(GetProcessHeap(), 0, size);
        if (!addresses) {
            return FALSE;
        }
        result = GetAdaptersAddresses(AF_INET, flags, NULL, addresses, &size);
    }

    if (result == NO_ERROR) {
        for (IP_ADAPTER_ADDRESSES *adapter = addresses; adapter && !found; adapter = adapter->Next) {
            if (adapter->OperStatus != IfOperStatusUp || !adapter->FirstUnicastAddress ||
                !contains_ignoring_case((const utf16_t *)adapter->Description, (const utf16_t *)adapter_name)) {
                continue;
            }
            const struct sockaddr_in *address =
                (const struct sockaddr_in *)adapter->FirstUnicastAddress->Address.lpSockaddr;
            const unsigned char *octets = (const unsigned char *)&address->sin_addr;
            // 169.254.x.x means the real address has not arrived yet.
            if (octets[0] != 169 || octets[1] != 254) {
                wsprintfW(ip, L"%u.%u.%u.%u", octets[0], octets[1], octets[2], octets[3]);
                found = TRUE;
            }
        }
    }

    HeapFree(GetProcessHeap(), 0, addresses);
    return found;
}

static void refresh_devices(void)
{
    HWND list = GetDlgItem(g_dialog, IDC_DEVICES);
    wchar_t label[64];
    int count = hotspot_device_count();

    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < count; i++) {
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)hotspot_device_host(i));
    }
    wsprintfW(label, L"Connected devices: %d", count);
    SetDlgItemTextW(g_dialog, IDC_DEVICES_LABEL, label);
}

static void set_inputs_enabled(BOOL enabled)
{
    EnableWindow(GetDlgItem(g_dialog, IDC_SSID), enabled);
    EnableWindow(GetDlgItem(g_dialog, IDC_PASSWORD), enabled);
}

static void set_toggle(const wchar_t *text, BOOL enabled)
{
    SetDlgItemTextW(g_dialog, IDC_TOGGLE, text);
    EnableWindow(GetDlgItem(g_dialog, IDC_TOGGLE), enabled);
}

// The name and password boxes are locked while the hotspot runs, so they still hold
// what the hotspot was started with.
static void show_qr_code(BOOL activate)
{
    wchar_t ssid[64];
    wchar_t password[128];

    GetDlgItemTextW(g_dialog, IDC_SSID, ssid, ARRAYSIZE(ssid));
    GetDlgItemTextW(g_dialog, IDC_PASSWORD, password, ARRAYSIZE(password));
    if (!qr_window_show(g_dialog, ssid, password, activate)) {
        append_log(L"Could not show the QR code");
    }
}

static void on_state(HotspotState state, const wchar_t *message)
{
    switch (state) {
    case HOTSPOT_STARTING:
        SetDlgItemTextW(g_dialog, IDC_STATUS, L"Starting...");
        EnableWindow(GetDlgItem(g_dialog, IDC_QR), FALSE);
        set_inputs_enabled(FALSE);
        set_toggle(L"Starting...", FALSE);
        SetTimer(g_dialog, TIMER_START_TIMEOUT, START_TIMEOUT_MS, NULL);
        break;

    case HOTSPOT_RUNNING: {
        wchar_t ssid[64];
        wchar_t status[128];

        KillTimer(g_dialog, TIMER_START_TIMEOUT);
        GetDlgItemTextW(g_dialog, IDC_SSID, ssid, ARRAYSIZE(ssid));
        wsprintfW(status, L"Running. Connect to \"%s\" with the password above.", ssid);
        SetDlgItemTextW(g_dialog, IDC_STATUS, status);
        SetDlgItemTextW(g_dialog, IDC_IP, WAITING_FOR_IP);
        set_toggle(L"&Stop hotspot", TRUE);
        EnableWindow(GetDlgItem(g_dialog, IDC_QR), TRUE);
        append_log(L"Hotspot started");
        show_qr_code(FALSE);

        // Sleep would take the access point down with it.
        SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
        g_ip_known = FALSE;
        SetTimer(g_dialog, TIMER_FIND_IP, FIND_IP_INTERVAL_MS, NULL);
        break;
    }

    case HOTSPOT_STOPPING:
        qr_window_close();
        EnableWindow(GetDlgItem(g_dialog, IDC_QR), FALSE);
        SetDlgItemTextW(g_dialog, IDC_STATUS, L"Stopping...");
        set_toggle(L"Stopping...", FALSE);
        SetTimer(g_dialog, TIMER_STOP_TIMEOUT, STOP_TIMEOUT_MS, NULL);
        break;

    case HOTSPOT_STOPPED:
        KillTimer(g_dialog, TIMER_START_TIMEOUT);
        KillTimer(g_dialog, TIMER_STOP_TIMEOUT);
        KillTimer(g_dialog, TIMER_FIND_IP);
        SetThreadExecutionState(ES_CONTINUOUS);
        qr_window_close();
        EnableWindow(GetDlgItem(g_dialog, IDC_QR), FALSE);
        SetDlgItemTextW(g_dialog, IDC_STATUS, message ? message : L"Stopped");
        SetDlgItemTextW(g_dialog, IDC_IP, NO_IP);
        set_inputs_enabled(TRUE);
        set_toggle(L"&Start hotspot", TRUE);
        refresh_devices();
        append_log(message ? message : L"Hotspot stopped");
        break;
    }
}

static void on_device_connected(const wchar_t *host, const wchar_t *local_ip)
{
    wchar_t line[128];

    wsprintfW(line, L"Connected: %s", host);
    append_log(line);
    refresh_devices();
    if (!g_ip_known && local_ip[0]) {
        show_ip(local_ip);
    }
}

static void on_device_disconnected(const wchar_t *host)
{
    wchar_t line[128];

    wsprintfW(line, L"Disconnected: %s", host);
    append_log(line);
    refresh_devices();
}

static const HotspotEvents g_events = {on_state, on_device_connected, on_device_disconnected, append_log};

static const wchar_t *validation_message(ValidationResult result)
{
    switch (result) {
    case SSID_EMPTY:
        return L"Enter a network name.";
    case SSID_TOO_LONG:
        return L"The network name is too long. Wi-Fi allows 32 bytes, which is 32 plain letters "
               L"or fewer if it has accents, non-Latin letters or emoji.";
    case PASSPHRASE_TOO_SHORT:
        return L"The password needs at least 8 characters.";
    case PASSPHRASE_TOO_LONG:
        return L"The password can be at most 63 characters.";
    case PASSPHRASE_BAD_CHARACTER:
        return L"The password can only use English letters, numbers, spaces and keyboard symbols.";
    default:
        return L"";
    }
}

static void start(void)
{
    wchar_t ssid[64];
    wchar_t password[128];
    wchar_t error[200];
    ValidationResult result;

    GetDlgItemTextW(g_dialog, IDC_SSID, ssid, ARRAYSIZE(ssid));
    GetDlgItemTextW(g_dialog, IDC_PASSWORD, password, ARRAYSIZE(password));

    result = validate_ssid((const utf16_t *)ssid);
    if (result == VALID) {
        result = validate_passphrase((const utf16_t *)password);
    }
    if (result != VALID) {
        MessageBoxW(g_dialog, validation_message(result), L"Offline Hotspot", MB_OK | MB_ICONWARNING);
        return;
    }

    settings_save(ssid, password);

    if (FAILED(hotspot_start(ssid, password, error, ARRAYSIZE(error)))) {
        SetDlgItemTextW(g_dialog, IDC_STATUS, error);
        append_log(error);
    }
}

static void stop(void)
{
    HRESULT hr = hotspot_stop();
    if (FAILED(hr)) {
        wchar_t error[64];
        wsprintfW(error, L"Stopping failed (0x%08X)", (unsigned)hr);
        append_log(error);
        hotspot_abandon();
        on_state(HOTSPOT_STOPPED, NULL);
    }
}

static void on_timer(UINT_PTR timer)
{
    switch (timer) {
    case TIMER_START_TIMEOUT:
        hotspot_abandon();
        on_state(HOTSPOT_STOPPED, L"Windows did not start the hotspot. Your Wi-Fi adapter or driver "
                                  L"may not support Wi-Fi Direct.");
        break;

    case TIMER_STOP_TIMEOUT:
        hotspot_abandon();
        on_state(HOTSPOT_STOPPED, NULL);
        break;

    case TIMER_FIND_IP: {
        wchar_t ip[32];
        if (find_hotspot_ip(ip)) {
            show_ip(ip);
        }
        break;
    }
    }
}

static void load_initial_settings(void)
{
    wchar_t ssid[64] = DEFAULT_SSID;
    wchar_t password[128] = L"";

    if (!settings_load(ssid, ARRAYSIZE(ssid), password, ARRAYSIZE(password))) {
        unsigned char random_bytes[64];

        lstrcpyW(ssid, DEFAULT_SSID);
        if (BCRYPT_SUCCESS(BCryptGenRandom(NULL, random_bytes, sizeof(random_bytes),
                                           BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
            generate_passphrase((utf16_t *)password, GENERATED_PASSPHRASE_LENGTH, random_bytes, sizeof(random_bytes));
        }
    }

    SetDlgItemTextW(g_dialog, IDC_SSID, ssid);
    SetDlgItemTextW(g_dialog, IDC_PASSWORD, password);
}

static INT_PTR CALLBACK dialog_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_INITDIALOG: {
        HICON icon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_APP));

        g_dialog = dialog;
        SendMessageW(dialog, WM_SETICON, ICON_BIG, (LPARAM)icon);
        SendMessageW(dialog, WM_SETICON, ICON_SMALL, (LPARAM)icon);
        SendDlgItemMessageW(dialog, IDC_SSID, EM_LIMITTEXT, 63, 0);
        SendDlgItemMessageW(dialog, IDC_PASSWORD, EM_LIMITTEXT, 127, 0);
        SetDlgItemTextW(dialog, IDC_IP, NO_IP);
        load_initial_settings();
        hotspot_init(dialog, &g_events);
        return TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wparam) == IDC_TOGGLE) {
            if (hotspot_state() == HOTSPOT_STOPPED) {
                start();
            } else if (hotspot_state() == HOTSPOT_RUNNING) {
                stop();
            }
            return TRUE;
        }
        if (LOWORD(wparam) == IDC_QR) {
            if (hotspot_state() == HOTSPOT_RUNNING) {
                show_qr_code(TRUE);
            }
            return TRUE;
        }
        // Escape must not close the window and drop every connected device.
        return LOWORD(wparam) == IDCANCEL;

    case WM_TIMER:
        on_timer(wparam);
        return TRUE;

    case WM_HOTSPOT:
        hotspot_on_message(lparam);
        return TRUE;

    case WM_CLOSE:
        hotspot_abandon();
        SetThreadExecutionState(ES_CONTINUOUS);
        EndDialog(dialog, 0);
        return TRUE;
    }
    return FALSE;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show)
{
    (void)previous;
    (void)command_line;
    (void)show;

    // WinRT callbacks run on the thread pool in the multithreaded apartment, as
    // in the sample, so the UI thread is never blocked waiting for them.
    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(hr)) {
        MessageBoxW(NULL, L"Could not initialise the Windows Runtime.", L"Offline Hotspot", MB_OK | MB_ICONERROR);
        return 1;
    }

    InitCommonControls();
    DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_MAIN), NULL, dialog_proc, 0);

    RoUninitialize();
    return 0;
}
