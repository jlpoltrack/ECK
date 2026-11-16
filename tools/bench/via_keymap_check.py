#!/usr/bin/env python3
"""Check VIA keymap handling over USB, then restore the keymap as it was.

Checks that KC_NO, KC_TRNS and modified keys and mod-taps read back as written, and that the
keymap-reset command (0x06) is answered and restores the default keymap.
Run: uv run --with hidapi python3 tools/bench/via_keymap_check.py
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from via_hid import open_keyboard, via  # noqa: E402

RESET, GET_BUF, SET_BUF = 0x06, 0x12, 0x13
LAYERS, KEYS = 4, 60
SIZE = LAYERS * KEYS * 2


def read_keymap(h):
    out = bytearray()
    for off in range(0, SIZE, 28):
        k = min(28, SIZE - off)
        out += via(h, bytes([GET_BUF, off >> 8, off & 0xFF, k]))[4:4 + k]
    return bytes(out)


def write_keymap(h, data, start=0):
    for off in range(0, len(data), 28):
        chunk = data[off:off + 28]
        o = start + off
        via(h, bytes([SET_BUF, o >> 8, o & 0xFF, len(chunk)]) + chunk)


def main():
    h = open_keyboard()
    saved = read_keymap(h)
    last = SIZE - 2  # layer 3, last key
    ok = True
    try:
        for code in (0x0000, 0x0001, 0x0204, 0x1204, 0x2204, 0x3104):  # NO, TRNS, LSFT(A), RSFT(A), LSFT_T(A), RCTL_T(A)
            write_keymap(h, code.to_bytes(2, "big"), last)
            got = int.from_bytes(read_keymap(h)[last:], "big")
            print(f"write 0x{code:04x}: reads 0x{got:04x}", "ok" if got == code else "FAIL")
            ok &= got == code
        r = via(h, bytes([RESET]))
        print("keymap reset (0x06): answered", "ok" if r[0] == RESET else "FAIL")
        ok &= r[0] == RESET
        reset = read_keymap(h)
        print(f"after reset: {sum(a != b for a, b in zip(reset[::2], saved[::2]))} of {LAYERS * KEYS} keys "
              f"differ from the saved keymap; key 3/59 reads 0x{int.from_bytes(reset[last:], 'big'):04x}")
    finally:
        write_keymap(h, saved)
        restored = read_keymap(h) == saved
        print("keymap restored", "ok" if restored else "FAIL")
        h.close()
    sys.exit(0 if ok and restored else 1)


if __name__ == "__main__":
    main()
