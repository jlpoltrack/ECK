#!/usr/bin/env python3
"""Flash ECK's nRF52 app on the HHKB Hybrid over SWD (pyOCD), verify it, retry on failure.

Only the sectors written are erased (never auto-unlock: that mass-erases PFU's bootloader).
App only (refused unless the chip holds the SoftDevice the app is built for):
    uv run --with pyocd python3 tools/nrf_flash.py --nrf <uid> [hex]
SoftDevice and app from a .hfb's nRF section (0x1000-0x35FFF; MBR and bootloader untouched),
e.g. to recover from a SoftDevice that doesn't start, or to go back to stock with PFU's .hfb:
    uv run --with pyocd python3 tools/nrf_flash.py --nrf <uid> --hfb <file.hfb>
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

from pyocd.core.helpers import ConnectHelper

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "_build/nrf52832_xxaa"
APP_BASE = 0x26000
SD_ID_ADDR = 0x300C   # SoftDevice info block: firmware ID in the low half-word
SD_ID_APP = 0x0101    # S132 v7.2.0, which the app is built against
HFB_NRF_OFFSET = 0x10020  # nRF section of a Hybrid .hfb: image of 0x1000..0x35FFF
HFB_NRF_BASE, HFB_NRF_LEN = 0x1000, 0x35000


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


def hfb_runs(path):
    """The nRF52 section of a Hybrid .hfb as {0x1000: bytes}."""
    data = Path(path).read_bytes()
    if len(data) != HFB_NRF_OFFSET + HFB_NRF_LEN + 16:
        sys.exit(f"{path}: not a Hybrid .hfb ({len(data)} bytes)")
    return {HFB_NRF_BASE: data[HFB_NRF_OFFSET:HFB_NRF_OFFSET + HFB_NRF_LEN]}


def softdevice_id(uid):
    """Firmware ID of the SoftDevice on the chip, read without halting it."""
    s = ConnectHelper.session_with_chosen_probe(
        unique_id=uid, target_override="cortex_m", connect_mode="attach",
        options={"auto_unlock": False, "resume_on_disconnect": True})
    s.open()
    sd_id = s.board.target.read32(SD_ID_ADDR) & 0xFFFF
    s.close()
    return sd_id


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
    ap.add_argument("--hfb", help="write the SoftDevice and app from this Hybrid .hfb instead")
    ap.add_argument("--tries", type=int, default=3)
    a = ap.parse_args()
    if a.hfb:
        runs = hfb_runs(a.hfb)
        tmp = tempfile.NamedTemporaryFile(suffix=".bin", delete=False)
        tmp.write(runs[HFB_NRF_BASE])
        tmp.close()
        load = ["-a", hex(HFB_NRF_BASE), tmp.name]
        what = "nRF52 SoftDevice and app"
    else:
        runs = hex_segments(a.hex)
        if min(runs) < APP_BASE:
            sys.exit(f"{a.hex} writes below {APP_BASE:#x} (MBR/SoftDevice): refusing")
        sd_id = softdevice_id(a.nrf)
        if sd_id != SD_ID_APP:
            sys.exit(f"chip holds SoftDevice ID {sd_id:#06x}, the app needs {SD_ID_APP:#06x}: "
                     "write a whole .hfb with --hfb instead")
        load = [a.hex]
        what = "nRF52 app"
    for attempt in range(1, a.tries + 1):
        subprocess.run(["pyocd", "load", "-u", a.nrf, "-t", "nrf52832", "-O", "auto_unlock=false",
                        "-e", "sector", *load], capture_output=True)
        if verify_and_run(a.nrf, runs):
            size = sum(len(d) for d in runs.values())
            print(f"{what} written, verified and reset ({size} bytes, attempt {attempt})")
            return
        print(f"attempt {attempt}: verify failed, retrying")
    sys.exit("nRF52 flash failed")


if __name__ == "__main__":
    main()
