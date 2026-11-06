// A small window that shows a Wi-Fi QR code, so a phone can join by scanning it.
#pragma once

#include <windows.h>

// Opens the window, or updates and raises it if it is already open. It sits beside
// owner. With activate FALSE it does not take focus from owner. Returns FALSE if the
// network details cannot be turned into a QR code.
BOOL qr_window_show(HWND owner, const wchar_t *ssid, const wchar_t *password, BOOL activate);

void qr_window_close(void);
