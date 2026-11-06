@echo off
rem Run from an "x64 Native Tools Command Prompt for VS".
setlocal
cd /d "%~dp0"

where cl >nul 2>nul
if errorlevel 1 (
    echo cl.exe was not found. Open the x64 Native Tools Command Prompt for VS and try again.
    exit /b 1
)

if not exist build mkdir build

rc /nologo /i src /fo build\app.res src\app.rc || exit /b 1

cl /nologo /O2 /W3 /MT /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 /D_CRT_SECURE_NO_WARNINGS ^
   /Isrc /Fobuild\ /Febuild\OfflineHotspot.exe ^
   src\main.c src\hotspot.c src\logic.c src\settings.c src\qr.c src\qrwindow.c build\app.res ^
   /link /SUBSYSTEM:WINDOWS /MANIFEST:NO ^
   kernel32.lib user32.lib gdi32.lib advapi32.lib crypt32.lib bcrypt.lib iphlpapi.lib comctl32.lib runtimeobject.lib || exit /b 1

echo Built build\OfflineHotspot.exe
