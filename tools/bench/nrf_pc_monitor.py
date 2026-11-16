#!/usr/bin/env python3
"""Log which nRF52 image runs on the HHKB Hybrid, without halting it (DWT PC sampling).

Prints a line whenever the sampled PC moves between regions (MBR, SoftDevice, app,
PFU bootloader) or the probe loses the chip (held in reset or SWD busy).
Run: uv run --with pyocd python3 tools/nrf_pc_monitor.py --nrf <uid> [--seconds N]
"""
import argparse
import time

from swd_attach import attach

DEMCR, DWT_PCSR = 0xE000EDFC, 0xE000101C
NRF_RESETREAS, NRF_GPREGRET = 0x40000400, 0x4000051C
REGIONS = [(0x1000, "MBR"), (0x26000, "SoftDevice"), (0x78000, "app"), (0x80000, "bootloader")]


def region(pc):
    if pc == 0xFFFFFFFF:
        return "sleeping/halted"
    for end, name in REGIONS:
        if pc < end:
            return name
    return f"other {pc:#x}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nrf", required=True)
    ap.add_argument("--seconds", type=float, default=600)
    ap.add_argument("--period", type=float, default=0.05)
    a = ap.parse_args()
    t0 = time.monotonic()
    last = None
    sess = None
    while time.monotonic() - t0 < a.seconds:
        try:
            if sess is None:
                sess = attach(a.nrf, frequency=1000000, resume_on_disconnect=True, auto_unlock=False)
                t = sess.board.target
                t.write32(DEMCR, t.read32(DEMCR) | (1 << 24))
                rr = t.read32(NRF_RESETREAS)
                gp = t.read32(NRF_GPREGRET)
                print(f"{time.monotonic() - t0:7.2f}s attached, RESETREAS {rr:#x} GPREGRET {gp:#x}", flush=True)
                t.write32(NRF_RESETREAS, 0xFFFFFFFF)
            # Several samples: sleeping cores read 0xFFFFFFFF, so keep the busiest region.
            pcs = [sess.board.target.read32(DWT_PCSR) for _ in range(8)]
            awake = [p for p in pcs if p != 0xFFFFFFFF]
            state = region(awake[-1]) if awake else "sleeping/halted"
        except Exception as e:  # chip in reset or probe busy
            state = f"no answer ({type(e).__name__})"
            try:
                if sess:
                    sess.close()
            except Exception:
                pass
            sess = None
        if state != last:
            print(f"{time.monotonic() - t0:7.2f}s {state}", flush=True)
            last = state
        time.sleep(a.period)
    if sess:
        sess.close()


if __name__ == "__main__":
    main()
