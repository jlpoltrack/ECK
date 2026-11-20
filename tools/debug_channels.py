#!/usr/bin/env python3
"""Turn ECK's HID Console debug channels on or off over VIA (docs/debug.md); without arguments,
show them. Channels: matrix (EC Matrix Stream), rate (Polling Rate Monitor), via (VIA telemetry),
cal (Calibration Monitor, also in VIA's Calibration section).
Run: uv run --with hidapi python3 tools/debug_channels.py [matrix|rate|via|cal on|off]"""
import sys

from via_hid import open_keyboard, via

CHANNELS = {"matrix": 19, "via": 23, "rate": 24, "cal": 25}  # VIA custom value IDs (hhkb_via_controls.c)


def main():
    args = sys.argv[1:]
    if args and (len(args) != 2 or args[0] not in CHANNELS or args[1] not in ("on", "off")):
        sys.exit(__doc__)
    h = open_keyboard()
    try:
        if args:
            via(h, [0x07, 0, CHANNELS[args[0]], args[1] == "on"])
        for name, ident in CHANNELS.items():
            print(f"{name:6} {'on' if via(h, [0x08, 0, ident])[3] else 'off'}")
    finally:
        h.close()


if __name__ == "__main__":
    main()
