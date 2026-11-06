#include <windows.h>

#include "qr.h"
#include "qrwindow.h"

#define WINDOW_CLASS L"OfflineHotspotQr"
#define QUIET_ZONE 4 // modules of white on each side, as the QR standard requires
#define QR_DIP 300   // size of the code, including its quiet zone, at 96 DPI
#define MARGIN_DIP 12
#define CAPTION_DIP 52
#define GAP_DIP 8

#define PAYLOAD_MAX 512

static const DWORD WINDOW_STYLE = WS_CAPTION | WS_SYSMENU;

static HWND g_window;
static unsigned char g_modules[QR_MAX_SIZE * QR_MAX_SIZE];
static int g_size;
static wchar_t g_caption[160];

static int scale(int dip, int dpi)
{
    return MulDiv(dip, dpi, 96);
}

// Appends text to out, escaping the characters that are special in a Wi-Fi QR code.
static int append_escaped(wchar_t *out, int length, const wchar_t *text)
{
    for (; *text; text++) {
        BOOL special = *text == L'\\' || *text == L';' || *text == L',' || *text == L'"' || *text == L':';
        if (length + 2 >= PAYLOAD_MAX) {
            return -1;
        }
        if (special) {
            out[length++] = L'\\';
        }
        out[length++] = *text;
    }
    return length;
}

// Builds "WIFI:T:WPA;S:<name>;P:<password>;;", which phone cameras understand.
static BOOL build_code(const wchar_t *ssid, const wchar_t *password)
{
    static const wchar_t prefix[] = L"WIFI:T:WPA;S:";
    static const wchar_t middle[] = L";P:";
    static const wchar_t suffix[] = L";;";
    wchar_t text[PAYLOAD_MAX];
    unsigned char bytes[PAYLOAD_MAX * 3];
    int length = 0;
    int byte_count;

    lstrcpyW(text, prefix);
    length = lstrlenW(text);
    length = append_escaped(text, length, ssid);
    if (length < 0 || length + 3 >= PAYLOAD_MAX) {
        return FALSE;
    }
    lstrcpyW(text + length, middle);
    length += lstrlenW(middle);
    length = append_escaped(text, length, password);
    if (length < 0 || length + 2 >= PAYLOAD_MAX) {
        return FALSE;
    }
    lstrcpyW(text + length, suffix);
    length += lstrlenW(suffix);

    byte_count = WideCharToMultiByte(CP_UTF8, 0, text, length, (LPSTR)bytes, sizeof(bytes), NULL, NULL);
    if (byte_count <= 0) {
        return FALSE;
    }
    return qr_encode(bytes, byte_count, g_modules, &g_size);
}

static void client_size(int dpi, int *width, int *height)
{
    int total = g_size + 2 * QUIET_ZONE;
    int module = scale(QR_DIP, dpi) / total;
    int code;

    if (module < 2) {
        module = 2;
    }
    code = module * total;
    *width = code + 2 * scale(MARGIN_DIP, dpi);
    *height = scale(MARGIN_DIP, dpi) + code + scale(CAPTION_DIP, dpi);
}

static void paint(HWND window)
{
    PAINTSTRUCT paint_info;
    HDC dc = BeginPaint(window, &paint_info);
    RECT client;
    int dpi = (int)GetDpiForWindow(window);
    int margin = scale(MARGIN_DIP, dpi);
    int caption_height = scale(CAPTION_DIP, dpi);
    int total = g_size + 2 * QUIET_ZONE;
    int available;
    int module;
    int left;
    int top;
    NONCLIENTMETRICSW metrics;
    HFONT font = NULL;
    HGDIOBJ old_font = NULL;
    RECT text_area;

    GetClientRect(window, &client);
    FillRect(dc, &client, (HBRUSH)GetStockObject(WHITE_BRUSH));

    available = client.right - 2 * margin;
    if (client.bottom - margin - caption_height < available) {
        available = client.bottom - margin - caption_height;
    }
    module = available / total;
    if (module < 1) {
        module = 1;
    }
    left = (client.right - module * total) / 2;
    top = margin;

    for (int y = 0; y < g_size; y++) {
        for (int x = 0; x < g_size; x++) {
            if (g_modules[y * g_size + x]) {
                RECT cell;
                cell.left = left + (x + QUIET_ZONE) * module;
                cell.top = top + (y + QUIET_ZONE) * module;
                cell.right = cell.left + module;
                cell.bottom = cell.top + module;
                FillRect(dc, &cell, (HBRUSH)GetStockObject(BLACK_BRUSH));
            }
        }
    }

    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        metrics.lfMessageFont.lfHeight = -MulDiv(9, dpi, 72);
        metrics.lfMessageFont.lfWidth = 0;
        font = CreateFontIndirectW(&metrics.lfMessageFont);
    }
    if (font) {
        old_font = SelectObject(dc, font);
    }
    text_area.left = margin;
    text_area.right = client.right - margin;
    text_area.top = top + module * total;
    text_area.bottom = client.bottom;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));
    DrawTextW(dc, g_caption, -1, &text_area, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    if (font) {
        SelectObject(dc, old_font);
        DeleteObject(font);
    }

    EndPaint(window, &paint_info);
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_PAINT:
        paint(window);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) {
            DestroyWindow(window);
            return 0;
        }
        break;

    case WM_DPICHANGED: {
        const RECT *suggested = (const RECT *)lparam;
        SetWindowPos(window, NULL, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_DESTROY:
        if (window == g_window) {
            g_window = NULL;
        }
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static BOOL register_class(void)
{
    static BOOL registered;
    WNDCLASSEXW window_class;

    if (registered) {
        return TRUE;
    }
    window_class.cbSize = sizeof(window_class);
    window_class.style = 0;
    window_class.lpfnWndProc = window_proc;
    window_class.cbClsExtra = 0;
    window_class.cbWndExtra = 0;
    window_class.hInstance = GetModuleHandleW(NULL);
    window_class.hIcon = NULL;
    window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    window_class.hbrBackground = NULL;
    window_class.lpszMenuName = NULL;
    window_class.lpszClassName = WINDOW_CLASS;
    window_class.hIconSm = NULL;
    registered = RegisterClassExW(&window_class) != 0;
    return registered;
}

BOOL qr_window_show(HWND owner, const wchar_t *ssid, const wchar_t *password, BOOL activate)
{
    int dpi = (int)GetDpiForWindow(owner);
    RECT frame;
    RECT owner_rect;
    MONITORINFO monitor;
    int width;
    int height;
    int x;
    int y;

    if (!build_code(ssid, password)) {
        return FALSE;
    }
    wsprintfW(g_caption, L"Scan with a phone camera to join \"%s\"", ssid);

    if (g_window) {
        InvalidateRect(g_window, NULL, TRUE);
        ShowWindow(g_window, activate ? SW_SHOW : SW_SHOWNOACTIVATE);
        if (activate) {
            SetForegroundWindow(g_window);
        }
        return TRUE;
    }

    if (!register_class()) {
        return FALSE;
    }

    client_size(dpi, &width, &height);
    frame.left = 0;
    frame.top = 0;
    frame.right = width;
    frame.bottom = height;
    AdjustWindowRectExForDpi(&frame, WINDOW_STYLE, FALSE, 0, (UINT)dpi);
    width = frame.right - frame.left;
    height = frame.bottom - frame.top;

    // Beside the main window if there is room, otherwise on top of its edge.
    GetWindowRect(owner, &owner_rect);
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor);
    x = owner_rect.right + scale(GAP_DIP, dpi);
    y = owner_rect.top;
    if (x + width > monitor.rcWork.right) {
        x = monitor.rcWork.right - width;
    }
    if (y + height > monitor.rcWork.bottom) {
        y = monitor.rcWork.bottom - height;
    }
    if (x < monitor.rcWork.left) {
        x = monitor.rcWork.left;
    }
    if (y < monitor.rcWork.top) {
        y = monitor.rcWork.top;
    }

    g_window = CreateWindowExW(0, WINDOW_CLASS, L"Join Wi-Fi", WINDOW_STYLE, x, y, width, height, owner, NULL,
                               GetModuleHandleW(NULL), NULL);
    if (!g_window) {
        return FALSE;
    }
    ShowWindow(g_window, activate ? SW_SHOW : SW_SHOWNOACTIVATE);
    UpdateWindow(g_window);
    return TRUE;
}

void qr_window_close(void)
{
    if (g_window) {
        DestroyWindow(g_window);
    }
}
