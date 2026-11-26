#!/usr/bin/env python3
"""Bluetooth throughput probe (Hybrid; needs the nRF52 probe build, not a release): floods the
Bluetooth VIA interface with 32-byte reports the nRF52 counts and drops, then reads what arrived,
whether the host used write requests or commands, and the MTU, data length, PHY and interval.
Run: uv run -q --with hidapi python3 tools/bench/ble_probe.py [-n COUNT]"""
import argparse
import struct
import sys
import time

import hid

PROBE = 0xE5
PHY = {1: "1M", 2: "2M", 4: "coded"}


def open_ble():
    devs = [d for d in hid.enumerate(0xCAFE) if d["usage_page"] == 0xFF60 and d.get("bus_type") == 2]
    if not devs:
        sys.exit("no Bluetooth VIA interface")
    h = hid.device()
    h.open_path(devs[0]["path"])
    return h


def cmd(h, sub, payload=b""):
    h.write(b"\x00" + bytes([PROBE, sub]) + payload + bytes(30 - len(payload)))


def stats(h):
    cmd(h, 3)
    end = time.monotonic() + 3
    while time.monotonic() < end:
        r = bytes(h.read(32, 500))
        if len(r) == 32 and r[0] == PROBE and r[1] == 3:
            w, req, wc, nbytes, ticks = struct.unpack_from("<5I", r, 2)
            mtu, = struct.unpack_from("<H", r, 22)
            interval, latency = struct.unpack_from("<2H", r, 27)
            return dict(writes=w, req=req, cmd=wc, bytes=nbytes, secs=ticks / 16384, mtu=mtu, dl=r[24],  # app_timer: RTC prescaler 1
                        phy=f"{PHY.get(r[25], r[25])}/{PHY.get(r[26], r[26])}", interval=interval * 1.25,
                        latency=latency, latency_off=r[31])
    sys.exit("no stats reply")


def run(h, n, label):
    cmd(h, 1)
    time.sleep(0.3)
    t0 = time.perf_counter()
    for i in range(n):
        cmd(h, 2, struct.pack("<I", i))
    host_s = time.perf_counter() - t0
    time.sleep(1.5)
    s = stats(h)
    rate = s["bytes"] / s["secs"] / 1024 if s["secs"] else 0
    print(f"{label}: sent {n} in {host_s:.2f} s ({n * 32 / host_s / 1024:.1f} KB/s host side); "
          f"received {s['writes']} ({s['req']} write requests, {s['cmd']} write commands) "
          f"over {s['secs']:.2f} s = {rate:.1f} KB/s, {s['writes'] / s['secs'] / (1000 / s['interval']) if s['secs'] else 0:.1f} "
          f"reports per connection event")
    print(f"  link: MTU {s['mtu']}, data length {s['dl']}, PHY tx/rx {s['phy']}, interval {s['interval']:.2f} ms, "
          f"latency {s['latency']} (off now: {s['latency_off']})")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("-n", type=int, default=2000, help="reports per run")
    a = p.parse_args()
    h = open_ble()
    try:
        run(h, a.n, "as granted")
        cmd(h, 4)  # ask for 2M PHY; the host may refuse
        time.sleep(2)
        run(h, a.n, "after 2M request")
    finally:
        h.close()


if __name__ == "__main__":
    main()
