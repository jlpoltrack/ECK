#!/usr/bin/env python3
"""Bench check of a VIAL_ENABLE build over USB, no keypresses: serial magic, protocol versions,
the compressed definition, unlock status, entry counts, QMK settings and entry/keycode round trips.
Restores every entry and key it changes. Run: uv run -q --with hidapi python3 tools/bench/vial_check.py"""
import json
import lzma
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from via_hid import PRODUCT, open_keyboard, send  # noqa: E402

# ECK_PRODUCT=R3S: the Realforce R3S (product ID, matrix and key count)
PID, ROWS, COLS, KEYS = (0x4011, 6, 16, 87) if "R3S" in PRODUCT else (0x4010, 4, 15, 60)

fails = 0


def check(name, ok, detail=""):
    global fails
    fails += not ok
    print(f"{'PASS' if ok else 'FAIL'}  {name}{': ' + detail if detail else ''}")


def vial(h, sub, *args):
    return send(h, bytes([0xFE, sub, *args]))


def entry(h, op, idx, data=b""):
    return send(h, bytes([0xFE, 0x0D, op, idx]) + data)


def main():
    import hid
    serial = next((d["serial_number"] for d in hid.enumerate(0xCAFE, PID) if d["usage_page"] == 0xFF60), "")
    check("USB serial has Vial's magic", serial.startswith("vial:f64c2b3c-"), serial)
    h = open_keyboard()
    try:
        r = send(h, [0x01])
        check("VIA protocol 9", r[1:3] == b"\x00\x09", r[1:3].hex())
        r = vial(h, 0x00)
        check("Vial protocol 6", struct.unpack("<I", r[:4])[0] == 6, r[:12].hex())
        size = struct.unpack("<I", vial(h, 0x01)[:4])[0]
        blob = b"".join(vial(h, 0x02, p & 0xFF, p >> 8) for p in range((size + 31) // 32))[:size]
        d = json.loads(lzma.decompress(blob))
        keys = sum(isinstance(k, str) for row in d["layouts"]["keymap"] for k in row)
        check("definition", d["matrix"] == {"rows": ROWS, "cols": COLS} and keys == KEYS,
              f"{size} B compressed, {keys} keys, {len(d.get('customKeycodes', []))} custom keycodes")
        r = vial(h, 0x05)
        check("always unlocked, no unlock keys", r[0] == 1 and r[1] == 0 and r[2:4] == b"\xff\xff", r[:4].hex())
        r = entry(h, 0x00, 0)
        check("entry counts 8/8/8/8, Caps Word + Layer Lock", r[:4] == bytes([8] * 4) and r[31] == 3, r[:4].hex())

        ids, after = [], 0
        while True:
            r = send(h, bytes([0xFE, 0x09]) + struct.pack("<H", after))
            batch = [i for i in struct.unpack("<16H", r) if i != 0xFFFF]
            if not batch:
                break
            ids += batch
            after = batch[-1]
        values = {}
        for i in ids:
            r = send(h, bytes([0xFE, 0x0A]) + struct.pack("<H", i))
            values[i] = (r[0], r[1:5].hex())
        check("QMK settings", ids == [1, 2, 6, 7, 21, 23, 24, 25] and all(v[0] == 0 for v in values.values()),
              ", ".join(f"{i}={v[1]}" for i, v in values.items()))
        term = send(h, bytes([0xFE, 0x0A, 2, 0]))[1:3]
        send(h, bytes([0xFE, 0x0B, 2, 0]) + struct.pack("<H", 75))
        got = struct.unpack("<H", send(h, bytes([0xFE, 0x0A, 2, 0]))[1:3])[0]
        send(h, bytes([0xFE, 0x0B, 2, 0]) + term)
        check("combo term set/get", got == 75, f"{got} ms")
        r = send(h, bytes([0xFE, 0x0B, 7, 0]) + struct.pack("<H", 3000))
        check("tapping term 3000 ms refused", r[0] != 0, r[:1].hex())
        r = send(h, [0x02, 0x03])
        check("matrix reply in protocol-9 layout", len(r) == 32 and r[10:] == bytes(22), r[:10].hex())

        # Last entry of each kind: write, read back, restore (QK_BOOT is allowed: no lock)
        for name, get, size_, data in (
            ("tap dance", 1, 10, struct.pack("<5H", 0x04, 0x7C00, 0x06, 0x07, 180)),
            ("combo", 3, 10, struct.pack("<5H", 0x04, 0x05, 0, 0, 0x29)),
            ("key override", 5, 10, struct.pack("<3H4B", 0x2A, 0x4C, 0xF, 0x02, 0, 0x02, 0x81)),
            ("alt repeat key", 7, 6, struct.pack("<2H2B", 0x50, 0x4F, 0, 0x08)),
        ):
            old = entry(h, get, 7)[1:1 + size_]
            entry(h, get + 1, 7, data)
            back = entry(h, get, 7)[1:1 + size_]
            entry(h, get + 1, 7, old)
            check(f"{name} round trip", back == data, back.hex())

        # Vial keycodes in the keymap (layer 3, row 0, col 0): TD(0), (Alt) Repeat, Caps Word,
        # Layer Lock, Grave Escape, QK_BOOT, OSL(1), OSM(LSFT)
        old = send(h, [0x04, 3, 0, 0])[4:6]
        for kc in (0x5700, 0x7C79, 0x7C7A, 0x7C73, 0x7C7B, 0x7C16, 0x7C00, 0x5281, 0x52A2):
            send(h, [0x05, 3, 0, 0, kc >> 8, kc & 0xFF])
            got = send(h, [0x04, 3, 0, 0])[4:6]
            check(f"keycode {kc:#06x}", got == bytes([kc >> 8, kc & 0xFF]), got.hex())
        send(h, [0x05, 3, 0, 0, *old])
        check("keymap restored", send(h, [0x04, 3, 0, 0])[4:6] == old)
    finally:
        h.close()
    print(f"{'all passed' if not fails else f'{fails} failed'}")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
