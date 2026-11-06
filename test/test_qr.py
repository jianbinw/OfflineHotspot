r"""Encodes strings of many lengths with src/qr.c and checks that OpenCV decodes them.
Build the helper first, from an x64 Native Tools prompt in the project root:
    cl /Isrc /Fobuild\test\ /Febuild\test\qr_dump.exe test\qr_dump.c src\qr.c
then run:  python test/test_qr.py build\test\qr_dump.exe
Needs opencv-python-headless and numpy."""
import subprocess
import sys

import cv2
import numpy as np

exe = sys.argv[1]
detector = cv2.QRCodeDetectorAruco()
failures = 0
SPECIAL = "\\;,\":"


def wifi(ssid, password):
    def esc(text):
        return "".join("\\" + c if c in SPECIAL else c for c in text)

    return f"WIFI:T:WPA;S:{esc(ssid)};P:{esc(password)};;"


cases = [wifi("OfflineHotspot", "abcd1234wxyz"), "a", "x" * 14, "x" * 15, "hello world" * 3]
cases += ["".join(chr(33 + (i * 7) % 90) for i in range(n)) for n in range(1, 214, 3)] + ["q" * 213]
cases += [wifi('ss;i,d:"\\', 'p;a,s:"\\' * 7), wifi("é" * 16, "p" * 63)]

for text in cases:
    data = text.encode("utf-8")
    if len(data) > 213:
        continue
    out = subprocess.run([exe], input=data, capture_output=True).stdout.decode().split()
    grid = np.array([[int(c) for c in row] for row in out], dtype=np.uint8)
    n = grid.shape[0]
    image = np.pad(1 - grid, 4, constant_values=1) * 255
    image = np.kron(image, np.ones((6, 6), dtype=np.uint8)).astype(np.uint8)
    decoded, _, _ = detector.detectAndDecode(image)
    ok = decoded == text
    failures += not ok
    print("ok  " if ok else "FAIL", f"v{(n - 17) // 4}", len(data), "bytes")
print("failures:", failures)
sys.exit(1 if failures else 0)
