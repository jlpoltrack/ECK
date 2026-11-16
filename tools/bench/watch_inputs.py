#!/usr/bin/env python3
"""Print every input-pin change on the HHKB Hybrid's STM32 (GPIOA-D) and nRF52 (P0), live.

For identifying buttons and lines by hand (press the power switch, plug USB...). Reads IDR/IN
registers over SWD without halting; ignores pins listed with --ignore (e.g. the matrix mux).
Run: uv run --with pyocd python3 tools/watch_inputs.py --stm <uid> --nrf <uid> [--seconds 60]
"""
import argparse
import time

from swd_attach import attach

STM_PORTS = {"A": 0x50000010, "B": 0x50000410, "C": 0x50000810, "D": 0x50000C10}
NRF_IN = 0x50000510


def session(uid):
    return attach(uid, frequency=4000000, resume_on_disconnect=True, auto_unlock=False)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stm")
    ap.add_argument("--nrf")
    ap.add_argument("--seconds", type=float, default=60)
    ap.add_argument("--ignore", default="PA1,PB12,PB13,PB14,PB0,PB1,PB2,PB10,PC6,PB15,PC7,PC8,PA13,PA14,PA11,PA12,"
                            "PA15,PB3,PB4,PB5,P0.22,P0.23,P0.24,P0.25",
                    help="comma-separated pins to skip (default: matrix, SWD, USB and link pins)")
    a = ap.parse_args()
    ignore = set(a.ignore.split(",")) if a.ignore else set()
    sessions = {}
    if a.stm:
        sessions["stm"] = session(a.stm)
    if a.nrf:
        sessions["nrf"] = session(a.nrf)
    last = {}
    t0 = time.monotonic()
    while time.monotonic() - t0 < a.seconds:
        vals = {}
        try:
            if "stm" in sessions:
                t = sessions["stm"].board.target
                for p, addr in STM_PORTS.items():
                    vals["P" + p] = t.read32(addr) & 0xFFFF
                if not vals["PA"]:  # the STM32 sometimes reads back 0 while it sleeps: drop the sample
                    continue
            if "nrf" in sessions:
                vals["P0."] = sessions["nrf"].board.target.read32(NRF_IN)
        except Exception:
            continue
        for port, v in vals.items():
            old = last.get(port)
            if old is not None and v != old:
                for bit in range(32):
                    if (v ^ old) >> bit & 1:
                        name = f"{port}{bit}"
                        if name not in ignore:
                            print(f"{time.monotonic() - t0:8.3f} {name} -> {(v >> bit) & 1}", flush=True)
            last[port] = v
        time.sleep(0.005)
    for s in sessions.values():
        s.close()


if __name__ == "__main__":
    main()
