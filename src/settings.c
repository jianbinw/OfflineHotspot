#include "settings.h"

#include <wincrypt.h>
#include <dpapi.h>

#define KEY_PATH L"Software\\OfflineHotspot"
#define MAX_BLOB 2048

BOOL settings_load(wchar_t *ssid, int ssid_chars, wchar_t *passphrase, int passphrase_chars)
{
    HKEY key;
    BYTE blob[MAX_BLOB];
    DWORD blob_size = sizeof(blob);
    DWORD ssid_size = (DWORD)ssid_chars * sizeof(wchar_t);
    DWORD type = 0;
    BOOL loaded = FALSE;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, KEY_PATH, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return FALSE;
    }

    if (RegQueryValueExW(key, L"Ssid", NULL, &type, (BYTE *)ssid, &ssid_size) == ERROR_SUCCESS && type == REG_SZ &&
        ssid_size >= sizeof(wchar_t) &&
        RegQueryValueExW(key, L"Password", NULL, &type, blob, &blob_size) == ERROR_SUCCESS && type == REG_BINARY) {
        DATA_BLOB input = {blob_size, blob};
        DATA_BLOB output = {0, NULL};

        ssid[ssid_size / sizeof(wchar_t) - 1] = 0;

        if (CryptUnprotectData(&input, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
            lstrcpynW(passphrase, (const wchar_t *)output.pbData, passphrase_chars);
            SecureZeroMemory(output.pbData, output.cbData);
            LocalFree(output.pbData);
            loaded = TRUE;
        }
    }

    RegCloseKey(key);
    return loaded;
}

void settings_save(const wchar_t *ssid, const wchar_t *passphrase)
{
    HKEY key;
    DATA_BLOB input;
    DATA_BLOB output = {0, NULL};

    if (RegCreateKeyExW(HKEY_CURRENT_USER, KEY_PATH, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS) {
        return;
    }

    input.pbData = (BYTE *)passphrase;
    input.cbData = (DWORD)(lstrlenW(passphrase) + 1) * sizeof(wchar_t);

    if (CryptProtectData(&input, L"Offline Hotspot password", NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        RegSetValueExW(key, L"Ssid", 0, REG_SZ, (const BYTE *)ssid, (DWORD)(lstrlenW(ssid) + 1) * sizeof(wchar_t));
        RegSetValueExW(key, L"Password", 0, REG_BINARY, output.pbData, output.cbData);
        LocalFree(output.pbData);
    }

    RegCloseKey(key);
}
