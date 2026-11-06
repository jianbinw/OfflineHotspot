#!/usr/bin/env bash
# Cross-compiles OfflineHotspot.exe on Linux with clang and lld, linking no C
# runtime. MinGW-w64 provides the Windows headers; set SYSROOT to its include
# directory. On Windows, use build.cmd instead.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
sysroot="${SYSROOT:?Set SYSROOT to the MinGW-w64 headers directory}"
out="$root/build"
mkdir -p "$out/obj" "$out/lib"

target=x86_64-w64-windows-gnu
cflags=(--target="$target" -std=c11 -O2 -isystem "$sysroot" -I "$root/src"
        -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00
        -fno-stack-protector -mno-stack-arg-probe
        -Wall -Wextra -Werror -Wno-unknown-pragmas -Wno-missing-field-initializers)

for source in "$root"/src/{main,hotspot,logic,settings,qr,qrwindow}.c "$root/cross/nocrt.c"; do
    clang "${cflags[@]}" -c "$source" -o "$out/obj/$(basename "${source%.c}").o"
done

clang -E -x c -DRC_INVOKED -DUNICODE --target="$target" -isystem "$sysroot" -I "$root/src" -P \
    "$root/src/app.rc" -o "$out/obj/app.full.rc"
# The MinGW headers leak a few C declarations; resources start at LANGUAGE.
sed -n '/^LANGUAGE/,$p' "$out/obj/app.full.rc" > "$out/obj/app.preprocessed.rc"
(cd "$root/src" && llvm-rc -no-preprocess -I "$root/src" -fo "$out/obj/app.res" "$out/obj/app.preprocessed.rc")

libs=()
for def in "$root"/cross/defs/*.def; do
    lib="$out/lib/$(basename "${def%.def}").lib"
    llvm-lib -machine:x64 -def:"$def" -out:"$lib" 2>/dev/null
    libs+=("$lib")
done

# Keep the subsystem below 10.0. From 10.0 the Windows loader expects a load config
# that handles the security cookie, and without one the app cannot start. There
# is no load config here because no C runtime is linked. Windows 10 itself is
# still required at run time, by the Wi-Fi Direct API.
lld-link -nologo -nodefaultlib -entry:wWinMainCRTStartup -subsystem:windows,6.0 -machine:x64 \
    -opt:ref -opt:icf -dynamicbase -nxcompat -highentropyva -guard:no \
    -out:"$out/OfflineHotspot.exe" "$out"/obj/*.o "$out/obj/app.res" "${libs[@]}"

headers="$(llvm-readobj --file-headers "$out/OfflineHotspot.exe")"
subsystem="$(awk '/MajorSubsystemVersion:/ {print $2}' <<<"$headers")"
load_config="$(awk '/LoadConfigTableRVA:/ {print $2}' <<<"$headers")"
if [ "$subsystem" -ge 10 ] && [ "$load_config" = "0x0" ]; then
    echo "Refusing to ship: subsystem $subsystem without a load config will not start on Windows" >&2
    rm -f "$out/OfflineHotspot.exe"
    exit 1
fi

echo "Built $out/OfflineHotspot.exe"
