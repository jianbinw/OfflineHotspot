#!/usr/bin/env bash
# Builds and runs the tests with sanitizers. Needs gcc on Linux.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/build/test"
mkdir -p "$out"
flags=(-g -O1 -std=gnu11 -fsanitize=address,undefined -fno-omit-frame-pointer -Wno-incompatible-pointer-types)

gcc "${flags[@]}" -I "$root/src" "$root/test/test_logic.c" "$root/src/logic.c" -o "$out/logic"
gcc "${flags[@]}" -I "$root/test/fake_windows" -I "$root/src" \
    "$root/test/test_hotspot.c" "$root/src/hotspot.c" -o "$out/hotspot"

ASAN_OPTIONS=detect_leaks=1 "$out/logic"
ASAN_OPTIONS=detect_leaks=1 "$out/hotspot"
