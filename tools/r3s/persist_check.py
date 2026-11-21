#!/usr/bin/env python3
"""R3S EEPROM check, no key presses: change the tapping term, the indicator colour and brightness and
one layer-3 key over VIA, restart
through ST's DFU loader (reading the EEPROM there), check both survived and that Topre's EEPROM data
still matches the stock backup, then restore both. Prints address ranges only, never contents
(the macro area may hold a password).
Run: ECK_PRODUCT=R3S uv run -q --with hidapi python3 tools/r3s/persist_check.py STOCK_EEPROM.bin"""
import os
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from via_hid import open_keyboard, via  # noqa: E402

TAP_ID = 14            # VIA custom value: tapping term (10 ms units)
LED_IDS = (34, 35)     # indicator colour 1-7, brightness 1-5
KEY = (3, 0, 4)        # layer 3, row 0, col 4 (Esc position)
TOPRE = [(0x000, 0x060), (0x500, 0xC00), (0x17C0, 0x17E0), (0x17E4, 0x1800)]  # tag word 0x17E0 skipped
ECK = {"settings": (0x100, 0x298), "keymap slots": (0xC00, 0x17C0)}


def get(h):
    led = tuple(via(h, [0x08, 0, i])[3] for i in LED_IDS)
    return via(h, [0x08, 0, TAP_ID])[3], int.from_bytes(via(h, [0x04, *KEY])[4:6], "big"), led


def put(h, tap, key, led):
    via(h, [0x07, 0, TAP_ID, tap])
    for i, v in zip(LED_IDS, led):
        via(h, [0x07, 0, i, v])
    via(h, [0x09, 0, 0])  # save settings
    via(h, [0x05, *KEY, key >> 8, key & 0xFF])


def restart_and_read(tmp):
    """VIA jump to ST's DFU loader, read the EEPROM, leave (Topre's Bank 1 starts ECK again)."""
    h = open_keyboard()
    try:
        h.write(b"\x00\x0b" + bytes(31))
    except OSError:
        pass
    h.close()
    tool = [sys.executable, os.path.join(HERE, "r3s_backup.py")]
    subprocess.run(tool + ["read", tmp], check=True, capture_output=True, timeout=120)
    sys.path.insert(0, HERE)
    import hybrid_update  # noqa: E402  (its leave() starts the chip from the loader)
    hybrid_update.leave()
    return open(os.path.join(tmp, "eeprom.bin"), "rb").read()


def main():
    stock = open(sys.argv[1], "rb").read()
    h = open_keyboard()
    tap0, key0, led0 = get(h)
    print(f"before: indicator colour {led0[0]}, brightness level {led0[1]}")
    tap1, key1 = (tap0 + 1 if tap0 < 50 else tap0 - 1), (0x0004 if key0 != 0x0004 else 0x0005)  # KC_A / KC_B
    led1 = (led0[0] % 7 + 1, led0[1] % 5 + 1)
    put(h, tap1, key1, led1)
    h.close()
    time.sleep(3)  # settings save after 500 ms, keymap one word per poll
    with tempfile.TemporaryDirectory() as tmp:
        ee = restart_and_read(tmp)
    ok = True
    for a, b in TOPRE:
        same = ee[a:b] == stock[a:b]
        ok &= same
        print(f"Topre data {a:#06x}-{b - 1:#06x}: {'unchanged' if same else 'CHANGED'}")
    for name, (a, b) in ECK.items():
        print(f"ECK {name} {a:#06x}-{b - 1:#06x}: {'written' if any(ee[a:b]) else 'EMPTY'}")
    h = open_keyboard(wait=20)
    tap, key, led = get(h)
    print(f"after restart: tapping term {'kept' if tap == tap1 else 'LOST'}, layer-3 key {'kept' if key == key1 else 'LOST'}, "
          f"indicator style {'kept' if led == led1 else 'LOST'}")
    ok &= tap == tap1 and key == key1 and led == led1
    put(h, tap0, key0, led0)
    restored = get(h) == (tap0, key0, led0)
    print("restored", "ok" if restored else "FAIL")
    h.close()
    sys.exit(0 if ok and restored else 1)


if __name__ == "__main__":
    main()
