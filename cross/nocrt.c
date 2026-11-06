// Minimal startup for the Linux cross-build, which links no C runtime.
// MSVC builds use the normal runtime and do not compile this file.

#include <windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int);

void *memset(void *target, int value, size_t count)
{
    void *start = target;
    __asm__ volatile("rep stosb" : "+D"(target), "+c"(count) : "a"(value) : "memory");
    return start;
}

void *memcpy(void *target, const void *source, size_t count)
{
    void *start = target;
    __asm__ volatile("rep movsb" : "+D"(target), "+S"(source), "+c"(count) : : "memory");
    return start;
}

void WINAPI wWinMainCRTStartup(void)
{
    ExitProcess((UINT)wWinMain(GetModuleHandleW(NULL), NULL, GetCommandLineW(), SW_SHOWDEFAULT));
}
