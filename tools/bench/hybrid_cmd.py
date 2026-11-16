#!/usr/bin/env python3
"""Send a Bluetooth bridge command from the HHKB Hybrid's STM32 over SWD (bench automation).

Writes g_hybrid_test_cmd, which ECK's link task forwards to the nRF52 as a BLE_LINK_CMD_* frame.
Needs a build with it: ./build.sh hybrid -d HHKB_HYBRID_TEST_CMD
Commands: pair N, cancel, delete N, clear, select N, disconnect.
Run: uv run --with pyocd python3 tools/bench/hybrid_cmd.py --stm <uid> pair 2
"""
import argparse
import subprocess
from pathlib import Path  # noqa: F401

from swd_attach import attach

from repo_paths import REPO  # noqa: E402

ELF = REPO / "build/hybrid/hhkb_ec_hybrid.elf"
CMDS = {"pair": 1, "clear": 2, "disconnect": 3, "select": 4, "cancel": 5, "delete": 6}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stm", required=True)
    ap.add_argument("cmd", choices=CMDS)
    ap.add_argument("arg", nargs="?", type=int, default=0)
    a = ap.parse_args()
    nm = subprocess.run(["arm-none-eabi-nm", str(ELF)], capture_output=True, text=True, check=True).stdout
    addr = next((int(l.split()[0], 16) for l in nm.splitlines() if l.endswith(" g_hybrid_test_cmd")), None)
    if addr is None:
        raise SystemExit(f"{ELF} has no g_hybrid_test_cmd: build with ./build.sh hybrid -d HHKB_HYBRID_TEST_CMD")
    s = attach(a.stm, frequency=1000000, resume_on_disconnect=True)
    t = s.board.target
    t.write8(addr, CMDS[a.cmd])
    t.write8(addr + 1, a.arg)
    t.write8(addr + 2, 1)
    s.close()


if __name__ == "__main__":
    main()
