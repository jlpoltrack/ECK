#!/usr/bin/env python3
"""Time the Hybrid's Bluetooth reconnection after a restart (power switch off and on, on USB).

Run it, then switch the keyboard off (hold 2 s) and on (hold 1 s). It reports how long after
the keyboard is back on USB its paired host is connected again (VIA value 36, Bluetooth timing).
Run: uv run --with hidapi python3 tools/bench/ble_reconnect.py [TIMEOUT_S]
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from via_hid import open_keyboard, send  # noqa: E402

BLE_CONN_ID = 36  # stm32/via/hhkb_via_controls.c


def conn(h):
    """The granted timing as text, "not connected", or "" when the keyboard didn't answer."""
    r = bytes(send(h, [0x08, 0, BLE_CONN_ID]))
    return r[3:].split(b"\0")[0].decode(errors="replace") if len(r) > 3 and r[0] == 0x08 else ""


def main():
    timeout = float(sys.argv[1]) if len(sys.argv) > 1 else 120
    h = open_keyboard(wait=10)
    print(f"now: {conn(h)}; switch the keyboard off, then on")
    h.close()
    end = time.monotonic() + timeout
    while open_keyboard(required=False) is not None:  # wait for it to leave USB
        if time.monotonic() > end:
            sys.exit("the keyboard never left USB")
        time.sleep(0.1)
    print("off")
    h = None
    while h is None:
        if time.monotonic() > end:
            sys.exit("the keyboard did not come back on USB")
        h = open_keyboard(required=False)
        time.sleep(0.05)
    back = time.monotonic()
    print("back on USB")
    try:
        while time.monotonic() < end:
            try:
                s = conn(h)
            except Exception:
                s = ""
            if s and s != "not connected":
                print(f"connected {time.monotonic() - back:.2f} s after USB: {s}")
                return
            time.sleep(0.05)
        sys.exit("no Bluetooth connection within the timeout")
    finally:
        h.close()


if __name__ == "__main__":
    main()
