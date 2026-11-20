#!/usr/bin/env python3
"""Check VIA's Reset all settings (IDs 32 arm, 33 reset) over USB, then restore the EEPROM.

Reads the settings, checks the button does nothing unarmed, arms and resets, waits for the
restart, checks the defaults loaded and the keymap is unchanged, then writes BACKUP/eeprom.bin
back (hybrid_update.py restore-eeprom) and checks the settings are as before. No key presses.
Run: uv run --with hidapi python3 tools/bench/settings_reset_check.py BACKUP_DIR
(BACKUP_DIR from hybrid_update.py backup, taken just before.)
"""
import os
import subprocess
import sys
import time

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
from via_hid import open_keyboard, send, via  # noqa: E402

GET, SET = 0x08, 0x07
# id: (name, Hybrid default)
SETTINGS = {1: ("actuation", 30), 3: ("rapid trigger", 0), 9: ("LED 0", 5), 10: ("LED 1", 0),
            14: ("tapping term", 22), 17: ("quick tap", 15)}
ARM, RESET = 32, 33


def read_settings(h):
    return {i: via(h, [GET, 0, i], 4)[3] for i in SETTINGS}


def keymap_head(h):
    return bytes(via(h, [0x12, 0, 0, 28])[4:32])


def wait_back(timeout=20.0):
    time.sleep(1.0)  # the reset comes 200 ms after the reply
    h = open_keyboard(required=False, wait=timeout)
    if h is None:
        sys.exit("FAIL: the keyboard did not come back on USB")
    return h


def main():
    if len(sys.argv) != 2 or not os.path.isfile(os.path.join(sys.argv[1], "eeprom.bin")):
        sys.exit(__doc__)
    backup = sys.argv[1]
    h = open_keyboard()
    before, keys = read_settings(h), keymap_head(h)
    print("before:", {SETTINGS[i][0]: v for i, v in before.items()})
    send(h, [SET, 0, RESET, 1])  # unarmed: ignored
    time.sleep(0.5)
    if read_settings(h) != before:
        sys.exit("FAIL: the unarmed button changed settings")
    send(h, [SET, 0, ARM, 1])
    if via(h, [GET, 0, ARM], 4)[3] != 1:
        sys.exit("FAIL: arm toggle did not read back")
    send(h, [SET, 0, RESET, 1])
    h.close()
    h = wait_back()
    after = read_settings(h)
    print("after reset:", {SETTINGS[i][0]: v for i, v in after.items()})
    bad = [SETTINGS[i][0] for i, (_, d) in SETTINGS.items() if after[i] != d]
    if bad:
        sys.exit(f"FAIL: not at the defaults: {', '.join(bad)}")
    if via(h, [GET, 0, ARM], 4)[3] != 0:
        sys.exit("FAIL: still armed after the restart")
    if keymap_head(h) != keys:
        sys.exit("FAIL: the keymap changed")
    h.close()  # hybrid_update.py opens the keyboard itself
    print("defaults loaded, keymap kept; restoring the EEPROM backup")
    r = subprocess.run(["uv", "run", "-q", "--with", "hidapi", "python3", os.path.join(ROOT, "tools/hybrid_update.py"),
                        "restore-eeprom", os.path.join(backup, "eeprom.bin")], capture_output=True, text=True, timeout=180)
    if r.returncode:
        sys.exit(f"FAIL: restore-eeprom: {r.stdout}{r.stderr}")
    h = wait_back(30.0)
    restored = read_settings(h)
    h.close()
    if restored != before:
        sys.exit(f"FAIL: settings after the restore differ: {restored}")
    print("settings restored\nPASS")


if __name__ == "__main__":
    main()
