#!/usr/bin/env python3
"""Checks VIA over Bluetooth (Hybrid): lists every 0xFF60 interface, then sends VIA's
get-protocol-version to the Bluetooth one (hidapi bus type 2) and times round trips.
Run: uv run -q --with hidapi python3 tools/bench/ble_raw_check.py [-n COUNT] [--cold K]"""
import argparse
import statistics
import sys
import time

import hid

VID = 0xCAFE


def main():
    p = argparse.ArgumentParser()
    p.add_argument("-n", type=int, default=50, help="round trips to time")
    p.add_argument("--cold", type=int, default=0, metavar="K",
                   help="then K requests 6 s apart (past the fast-poll and latency-off windows); Vial waits 500 ms")
    a = p.parse_args()

    raw = [d for d in hid.enumerate(VID) if d["usage_page"] == 0xFF60 and d["usage"] == 0x61]
    for d in raw:
        print(f"0xFF60: {d['product_string']!r} serial={d['serial_number']!r} path={d['path']}")
    ble = [d for d in raw if d.get("bus_type") == 2]
    if not ble:
        sys.exit("no Bluetooth VIA interface (is the Mac's GATT cache stale? re-pair the slot)")

    h = hid.device()
    h.open_path(ble[0]["path"])
    try:
        times = []
        for i in range(a.n):
            req = bytes([0x01]) + bytes(31)
            t0 = time.perf_counter()
            h.write(b"\x00" + req)
            resp = bytes(h.read(32, 2000))
            dt = (time.perf_counter() - t0) * 1000
            if not resp:
                sys.exit(f"round trip {i}: no reply within 2 s")
            if i == 0:
                print(f"reply: {resp[:4].hex(' ')} ... ({len(resp)} bytes)")
                if resp[0] != 0x01 or resp[1:3] != b"\x00\x09":
                    sys.exit("unexpected reply")
            times.append(dt)
        # an unknown command must come back as VIA's "unhandled" (0xFF)
        h.write(b"\x00" + bytes([0x77]) + bytes(31))
        resp = bytes(h.read(32, 2000))
        print(f"unknown command reply: {resp[:2].hex(' ') if resp else 'none'}")
        cold = []
        for i in range(a.cold):
            time.sleep(6)
            t0 = time.perf_counter()
            h.write(b"\x00" + bytes([0x01]) + bytes(31))
            resp = bytes(h.read(32, 3000))
            cold.append((time.perf_counter() - t0) * 1000 if resp else float("inf"))
            print(f"cold {i + 1}: {cold[-1]:.0f} ms", flush=True)
        if cold:
            print(f"cold: max {max(cold):.0f} ms, {sum(c > 500 for c in cold)} of {len(cold)} over Vial's 500 ms")
    finally:
        h.close()
    times.sort()
    print(f"{len(times)} round trips: median {statistics.median(times):.1f} ms, "
          f"min {times[0]:.1f}, max {times[-1]:.1f}, p90 {times[int(len(times) * 0.9) - 1]:.1f}")


if __name__ == "__main__":
    main()
