// Starts and stops a Wi-Fi Direct legacy access point.
//
// Replaces WlanHostedNetworkHelper from Microsoft's WiFiDirectLegacyAP sample.
// All functions must be called on the UI thread. WinRT callbacks arrive on
// thread-pool threads, so they are posted to the window as WM_HOTSPOT messages
// and turned into HotspotEvents calls on the UI thread.
#pragma once

#include <windows.h>

#define WM_HOTSPOT (WM_APP + 1)

typedef enum HotspotState {
    HOTSPOT_STOPPED,
    HOTSPOT_STARTING,
    HOTSPOT_RUNNING,
    HOTSPOT_STOPPING
} HotspotState;

typedef struct HotspotEvents {
    // message is NULL for a normal transition, or explains why it stopped.
    void (*on_state)(HotspotState state, const wchar_t *message);
    void (*on_device_connected)(const wchar_t *host, const wchar_t *local_ip);
    void (*on_device_disconnected)(const wchar_t *host);
    void (*on_log)(const wchar_t *message);
} HotspotEvents;

void hotspot_init(HWND window, const HotspotEvents *events);

// Writes a readable reason to error on failure.
HRESULT hotspot_start(const wchar_t *ssid, const wchar_t *passphrase, wchar_t *error, int error_chars);
HRESULT hotspot_stop(void);

// Tears down immediately without waiting for Windows to confirm.
void hotspot_abandon(void);

// Pass WM_HOTSPOT's lParam here.
void hotspot_on_message(LPARAM lparam);

HotspotState hotspot_state(void);
int hotspot_device_count(void);
const wchar_t *hotspot_device_host(int index);
