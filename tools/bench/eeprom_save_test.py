#!/usr/bin/env python3
"""Check the Hybrid's EEPROM writers over USB, no keypresses: changes a hold-tap setting, the Bluetooth
scan rate and one layer-3 key at once (three writers in two tasks), checks they survive a reset, then
restores them and checks the settings (except calibration) and Bluetooth word match the start.
Prints only changed address ranges, never contents (the macro area may hold a password).
Run: uv run --with hidapi python3 tools/bench/eeprom_save_test.py
"""
import os
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TAP_ID, SCAN_ID = 14, 29        # VIA custom values: tapping term (10 ms units), Bluetooth scan rate
KEY = (3, 0, 0)                 # layer, row, col
# Data EEPROM offsets in the settings record (stm32/ec/ec_settings.h), skipping its CRC and the
# calibration arrays: every save also writes the live rest levels, which drift.
SETTINGS_HEAD, SETTINGS_TAIL = (0x1500, 0x1506), (0x15F8, 0x1608)
BLE_WORD = (0x17F8, 0x17FC)     # Hybrid Bluetooth settings word (stm32/hybrid/hybrid_ble.c)


def open_kb():
    import hid
    for _ in range(40):
        devs = [d for d in hid.enumerate() if d["usage_page"] == 0xFF60 and d["usage"] == 0x61
                and "HHKB" in (d["product_string"] or "")]
        if devs:
            h = hid.device()
            h.open_path(devs[0]["path"])
            return h
        time.sleep(0.5)
    sys.exit("no HHKB found on USB")


def via(h, req):
    h.write(b"\x00" + bytes(req) + bytes(32 - len(req)))
    resp = bytes(h.read(32, 1000))
    if not resp or resp[0] != req[0]:
        sys.exit(f"no VIA answer to {req[0]:#04x}")
    return resp


def get(h):
    return (via(h, [0x08, 0, TAP_ID])[3], via(h, [0x08, 0, SCAN_ID])[3],
            int.from_bytes(via(h, [0x04, *KEY])[4:6], "big"))


def put(h, tap, scan, key):
    via(h, [0x07, 0, TAP_ID, tap])
    via(h, [0x07, 0, SCAN_ID, scan])
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
    h = open_kb()
    orig = get(h)
    changed = (orig[0] + 1 if orig[0] < 50 else orig[0] - 1, (orig[1] + 1) % 5, 0x0068 if orig[2] != 0x0068 else 0x0069)
    with tempfile.TemporaryDirectory() as tmp:
        h.close()
        e0 = dump(tmp, "e0")
        h = open_kb()
        put(h, *changed)
        time.sleep(5)
        h.close()
        dump(tmp, "e1")
        h = open_kb()
        after_reset = get(h)
        put(h, *orig)
        time.sleep(5)
        h.close()
        e2 = dump(tmp, "e2")
        h = open_kb()
        restored = get(h)
    ok = after_reset == changed and restored == orig
    print(f"changed values survived a reset: {after_reset == changed}; restored values survived: {restored == orig}")
    for name, (lo, hi) in (("settings header", SETTINGS_HEAD), ("settings values", SETTINGS_TAIL),
                           ("Bluetooth word", BLE_WORD)):
        same = e0[lo:hi] == e2[lo:hi]
        ok &= same
        print(f"{name}: {'identical to the start' if same else 'DIFFERS from the start'}")
    print("changed ranges, start vs end (keymap slots alternate, so 0x0600-0x0fff may differ):",
          ", ".join(ranges(e0, e2)) or "none")
    print("PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
