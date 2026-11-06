// Just enough of the Windows API for hotspot.c to compile on Linux, so its
// real code can run against the fake Wi-Fi Direct objects in test_hotspot.c.
#pragma once

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef int32_t HRESULT;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef uint32_t UINT32;
typedef uint32_t UINT;
typedef uint32_t DWORD;
typedef int BOOL;
typedef void *HWND;
typedef void *HANDLE;
typedef intptr_t LPARAM;
typedef uintptr_t WPARAM;
typedef int64_t __int64;

typedef struct GUID {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
} GUID;
typedef GUID IID;
typedef const GUID *REFIID;

typedef struct FakeHString *HSTRING;

#define TRUE 1
#define FALSE 0
#define STDMETHODCALLTYPE
#define WM_APP 0x8000
#define HEAP_ZERO_MEMORY 8

#define S_OK ((HRESULT)0)
#define S_FALSE ((HRESULT)1)
#define E_FAIL ((HRESULT)0x80004005)
#define E_POINTER ((HRESULT)0x80004003)
#define E_NOINTERFACE ((HRESULT)0x80004002)
#define E_OUTOFMEMORY ((HRESULT)0x8007000E)
#define REGDB_E_CLASSNOTREG ((HRESULT)0x80040154)
#define SUCCEEDED(hr) ((hr) >= 0)
#define FAILED(hr) ((hr) < 0)

typedef struct IInspectable IInspectable;
typedef struct IInspectableVtbl {
    HRESULT (*QueryInterface)(IInspectable *, REFIID, void **);
    ULONG (*AddRef)(IInspectable *);
    ULONG (*Release)(IInspectable *);
} IInspectableVtbl;
struct IInspectable {
    const IInspectableVtbl *lpVtbl;
};

static inline LONG InterlockedIncrement(volatile LONG *value) { return __sync_add_and_fetch(value, 1); }
static inline LONG InterlockedDecrement(volatile LONG *value) { return __sync_sub_and_fetch(value, 1); }

static inline HANDLE GetProcessHeap(void) { return NULL; }
void *HeapAlloc(HANDLE heap, DWORD flags, size_t size);
BOOL HeapFree(HANDLE heap, DWORD flags, void *memory);

BOOL PostMessageW(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

int lstrlenW(const wchar_t *text);
int lstrcmpW(const wchar_t *a, const wchar_t *b);
wchar_t *lstrcpynW(wchar_t *target, const wchar_t *source, int count);
int wsprintfW(wchar_t *target, const wchar_t *format, ...);

HRESULT WindowsCreateString(const wchar_t *text, UINT32 length, HSTRING *out);
HRESULT WindowsDeleteString(HSTRING text);
const wchar_t *WindowsGetStringRawBuffer(HSTRING text, UINT32 *length);
HRESULT RoActivateInstance(HSTRING class_name, IInspectable **out);
HRESULT RoGetActivationFactory(HSTRING class_name, REFIID iid, void **out);
