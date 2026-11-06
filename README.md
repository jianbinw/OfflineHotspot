# Offline Hotspot

A small Windows 10/11 app that turns your PC's Wi-Fi adapter into an access point
**without needing an internet connection**. Phones, tablets and laptops can join
it and talk to the PC over TCP/UDP. It does not share internet, even if the PC
has some.

Windows' built-in Mobile Hotspot generally needs a connection to share. This app
uses the Wi-Fi Direct "legacy access point" API instead, following Microsoft's
[WiFiDirectLegacyAP sample](https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/WiFiDirectLegacyAP).

## Use it

1. Turn Wi-Fi on. You do not need to be connected to a network.
2. Turn off Windows Mobile Hotspot if it is running.
3. Run `OfflineHotspot.exe` (from the releases page, or [build it yourself](#build-it-yourself)).
   It needs no install or admin rights.
4. Check the network name and password, then select **Start hotspot**.
5. A **Join Wi-Fi** window opens with a QR code. Scan it with a phone camera to join
   the network, or join it by name and password. **Show QR code** reopens it.
6. Use the **Hotspot IP** shown in the app to reach the PC from that device.

Keep the app open: closing it stops the hotspot. The PC will not go to sleep on
its own while the hotspot runs. The name and password are remembered, so devices
can rejoin next time. The name is stored in your Windows user profile, and the
password is encrypted to your account (`HKCU\Software\OfflineHotspot`).

Windows may warn about an unknown publisher because the app is not code signed.
Select **More info**, then **Run anyway**, or build it yourself.

## Does my PC support it?

Your Wi-Fi driver must support Wi-Fi Direct group owner mode. In a terminal, run:

    netsh wlan show wirelesscapabilities

and look for the Wi-Fi Direct entries, particularly **Wi-Fi Direct GO**.

## Troubleshooting

| What you see | What to do |
| --- | --- |
| "Wi-Fi is turned off" | Turn Wi-Fi on, and turn off Airplane mode. |
| "Wi-Fi Direct is in use" | Turn off Mobile Hotspot and close other hotspot, casting or wireless display apps. Mobile Hotspot always takes priority over Wi-Fi Direct, and starting it later will stop this app's hotspot too. |
| Hotspot will not start or stops at once | Check the driver support above, update the Wi-Fi driver, and make sure *Microsoft Wi-Fi Direct Virtual Adapter* is not disabled in Device Manager (under View > Show hidden devices). |
| Phone says "no internet" or drops off | That is expected. Choose to stay connected, and turn off settings such as "switch to mobile data". |
| Device joins but cannot reach a server on the PC | Windows Firewall is the likely cause. This network may count as Public, so allow your server app on Public networks. |
| Hotspot IP says "Waiting..." | Windows may be slow to assign an address. It also appears once a device connects. |
| Connected device isn't listed | The list relies on Windows reporting the connection. If the device says it has joined, try reaching the Hotspot IP anyway. |

## Known limits

- It's meant for devices talking to this PC. Microsoft describes this API as
  "not a general-purpose Wi-Fi access point", and device-to-device traffic may
  not work.
- You can't choose the Wi-Fi band or channel, because the API has no setting for it.
- Passwords must be 8 to 63 English keyboard characters, the WPA2 limit.
- Network names are limited to 32 bytes.

## Build it yourself

**On Windows:** install Visual Studio with the "Desktop development with C++"
workload, open the *x64 Native Tools Command Prompt*, then run `build.cmd`. The
result is `build\OfflineHotspot.exe`.

**On Linux:** `SYSROOT=<MinGW-w64 headers> cross/build.sh` builds with clang and
lld, linking no C runtime.

## How it was checked

- `tools/verify_abi.py` compares every interface ID and vtable slot in
  `src/wifidirect_abi.h` with Microsoft's generated metadata.
- `test/run.sh` runs the real `src/hotspot.c` against fake Wi-Fi Direct objects
  under sanitizers, covering start, stop, device connections, failures, late
  callbacks and leaks. It also tests input validation.
- `test/test_qr.py` encodes Wi-Fi strings of many lengths (versions 1 to 10) with
  `src/qr.c` and checks that OpenCV decodes them. Build `test/qr_dump.c` with
  `src/qr.c` first. The QR window was also checked by decoding a screenshot of it.
- **It has not yet been run on real Windows or Wi-Fi hardware.** Nor has the
  window been seen on screen, and `build.cmd` has not been run with Visual Studio.

## Layout

| Path | Purpose |
| --- | --- |
| `src/hotspot.c` | The hotspot itself, ported from the sample |
| `src/wifidirect_abi.h` | C bindings for the Wi-Fi Direct API |
| `src/main.c`, `src/app.rc` | The window |
| `src/qr.c`, `src/qrwindow.c` | QR code encoder and the window that shows it |
| `src/settings.c` | Remembers the name and password |
| `src/logic.c` | Validation and password generation |
| `tools/` | Interface ID generator and ABI verifier |
| `test/` | Tests that run without Windows |
| `cross/` | Linux cross-build |
