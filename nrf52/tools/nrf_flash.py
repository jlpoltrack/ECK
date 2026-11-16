#!/usr/bin/env python3
"""Flash ECK's nRF52 app on the HHKB Hybrid over SWD (pyOCD), verify it, retry on failure.

Only the app's sectors are erased (never auto-unlock: that mass-erases PFU's bootloader).
Run: uv run --with pyocd python3 tools/nrf_flash.py --nrf <uid> [hex]
"""
import argparse
import subprocess
import sys
from pathlib import Path

from pyocd.core.helpers import ConnectHelper

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "_build/nrf52832_xxaa"
APP_BASE = 0x26000


def hex_segments(path):
    """Intel HEX data records as {address: bytes}, merged into contiguous runs."""
    runs, base = {}, 0
    cur_addr, cur = None, bytearray()
    for line in Path(path).read_text().split():
        rec = bytes.fromhex(line[1:])
        n, addr, kind, data = rec[0], rec[1] << 8 | rec[2], rec[3], rec[4:4 + rec[0]]
        if kind == 4:
            base = (data[0] << 8 | data[1]) << 16
        elif kind == 2:
            base = (data[0] << 8 | data[1]) << 4
        elif kind == 0:
            a = base + addr
            if cur_addr is not None and a == cur_addr + len(cur):
                cur += data
            else:
                if cur_addr is not None:
                    runs[cur_addr] = bytes(cur)
                cur_addr, cur = a, bytearray(data)
    if cur_addr is not None:
        runs[cur_addr] = bytes(cur)
    return runs


def verify_and_run(uid, runs):
    """Compare flash with the hex, then make sure the core runs (a failed pyOCD exit leaves it halted)."""
    s = ConnectHelper.session_with_chosen_probe(
        unique_id=uid, target_override="cortex_m", connect_mode="attach",
        options={"auto_unlock": False, "resume_on_disconnect": True})
    s.open()
    t = s.board.target
    ok = all(bytes(t.read_memory_block8(a, len(d))) == d for a, d in runs.items())
    if ok:
        t.reset()
    s.close()
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nrf", required=True)
    ap.add_argument("hex", nargs="?", default=str(BUILD) + ".hex")
    ap.add_argument("--tries", type=int, default=3)
    a = ap.parse_args()
    runs = hex_segments(a.hex)
    if min(runs) < APP_BASE:
        sys.exit(f"{a.hex} writes below {APP_BASE:#x} (MBR/SoftDevice): refusing")
    for attempt in range(1, a.tries + 1):
        subprocess.run(["pyocd", "load", "-u", a.nrf, "-t", "nrf52832", "-O", "auto_unlock=false",
                        "-e", "sector", a.hex], capture_output=True)
        if verify_and_run(a.nrf, runs):
            size = sum(len(d) for d in runs.values())
            print(f"nRF52 app written, verified and reset ({size} bytes, attempt {attempt})")
            return
        print(f"attempt {attempt}: verify failed, retrying")
    sys.exit("nRF52 flash failed")


if __name__ == "__main__":
    main()
