#!/usr/bin/env python3
"""Check the Hybrid's EEPROM writers over USB, no keypresses: changes a hold-tap setting, the Vial combo
term and one layer-3 key at once (three writers), checks they survive a reset, then restores them and
checks the settings (except calibration), the Vial record and the Bluetooth word match the start.
Prints only changed address ranges, never contents (the macro area may hold a password).
Run: uv run --with hidapi python3 tools/bench/eeprom_save_test.py
"""
import os
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from via_hid import open_keyboard, send, via  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TAP_ID = 14                     # VIA custom value: tapping term (10 ms units)
COMBO_TERM = 2                  # Vial QMK setting: combo term (ms), kept in the Vial record
KEY = (3, 0, 0)                 # layer, row, col
# Data EEPROM offsets in the settings record (stm32/ec/ec_settings.h), skipping its CRC and the
# calibration arrays: every save also writes the live rest levels, which drift.
SETTINGS_HEAD, SETTINGS_TAIL = (0x1500, 0x1506), (0x15F8, 0x1608)
VIAL_RECORD = (0x1608, 0x17F8)  # Vial entries record (stm32/via/vial.c)
BLE_WORD = (0x17F8, 0x17FC)     # Hybrid Bluetooth settings word (stm32/hybrid/hybrid_ble.c): must not change


def get(h):
    return (via(h, [0x08, 0, TAP_ID])[3], struct.unpack("<H", send(h, bytes([0xFE, 0x0A, COMBO_TERM, 0]))[1:3])[0],
            int.from_bytes(via(h, [0x04, *KEY])[4:6], "big"))


def put(h, tap, combo, key):
    via(h, [0x07, 0, TAP_ID, tap])
    send(h, bytes([0xFE, 0x0B, COMBO_TERM, 0]) + struct.pack("<H", combo))
    via(h, [0x05, *KEY, key >> 8, key & 0xFF])


def dump(tmp, name):
    d = os.path.join(tmp, name)
    r = subprocess.run(["uv", "run", "-q", "--with", "hidapi", "python3", os.path.join(ROOT, "tools/hybrid_update.py"),
                        "backup", d], capture_output=True, text=True, timeout=60)
    if r.returncode:
        sys.exit(f"backup failed: {r.stdout}{r.stderr}")
    return open(os.path.join(d, "eeprom.bin"), "rb").read()


def ranges(a, b):
    out, start = [], None
    for i in range(len(a) + 1):
        diff = i < len(a) and a[i] != b[i]
        if diff and start is None:
            start = i
        elif not diff and start is not None:
            out.append(f"{start:#06x}-{i - 1:#06x}")
            start = None
    return out


def main():
    h = open_keyboard(wait=20)
    orig = get(h)
    changed = (orig[0] + 1 if orig[0] < 50 else orig[0] - 1, orig[1] + 10 if orig[1] < 500 else orig[1] - 10,
               0x0068 if orig[2] != 0x0068 else 0x0069)
    with tempfile.TemporaryDirectory() as tmp:
        h.close()
        e0 = dump(tmp, "e0")
        h = open_keyboard(wait=20)
        put(h, *changed)
        time.sleep(5)
        h.close()
        dump(tmp, "e1")
        h = open_keyboard(wait=20)
        after_reset = get(h)
        put(h, *orig)
        time.sleep(5)
        h.close()
        e2 = dump(tmp, "e2")
        h = open_keyboard(wait=20)
        restored = get(h)
    ok = after_reset == changed and restored == orig
    print(f"changed values survived a reset: {after_reset == changed}; restored values survived: {restored == orig}")
    for name, (lo, hi) in (("settings header", SETTINGS_HEAD), ("settings values", SETTINGS_TAIL),
                           ("Vial record", VIAL_RECORD), ("Bluetooth word", BLE_WORD)):
        same = e0[lo:hi] == e2[lo:hi]
        ok &= same
        print(f"{name}: {'identical to the start' if same else 'DIFFERS from the start'}")
    print("changed ranges, start vs end (keymap slots alternate, so 0x0600-0x0fff may differ):",
          ", ".join(ranges(e0, e2)) or "none")
    print("PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
