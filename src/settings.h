#pragma once

#include <windows.h>

// Remembers the network name and password so devices can reconnect automatically
// next time. They are stored per user, with the password encrypted by DPAPI.
BOOL settings_load(wchar_t *ssid, int ssid_chars, wchar_t *passphrase, int passphrase_chars);
void settings_save(const wchar_t *ssid, const wchar_t *passphrase);
