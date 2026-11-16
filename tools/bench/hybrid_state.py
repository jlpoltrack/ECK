#!/usr/bin/env python3
"""Print the HHKB Hybrid's Bluetooth bridge state on both chips over SWD, without halting.

Symbol addresses come from the ELF files, so rebuild before use.
Run: uv run --with pyocd python3 tools/hybrid_state.py --stm <uid> --nrf <uid>
"""
import argparse
import subprocess
from pathlib import Path

from swd_attach import attach

from repo_paths import REPO, NRF_APP  # noqa: E402

NRF_ELF = NRF_APP / "_build/nrf52832_xxaa.out"
STM_ELF = REPO / "build/hybrid/hhkb_ec_hybrid.elf"
NRF_VARS = {"m_conn_handle": 2, "m_secured": 1, "m_advertising_on": 1, "m_pairing": 1, "m_slot": 1,
            "m_deletes_pending": 1, "m_q_count": 1}
STM_VARS = {"s_flags": 1, "s_ready": 1, "s_slot": 1, "s_nrf_slot": 1, "s_prefer_ble": 1, "s_ble_scan_rate": 1}


def symbols(elf):
    out = subprocess.run(["arm-none-eabi-nm", str(elf)], capture_output=True, text=True, check=True).stdout
    return {f[2]: int(f[0], 16) for f in (line.split() for line in out.splitlines()) if len(f) == 3}


def dump(uid, elf, wanted):
    syms = symbols(elf)
    s = attach(uid, frequency=1000000, resume_on_disconnect=True, auto_unlock=False)
    t = s.board.target
    vals = {}
    for name, size in wanted.items():
        if name in syms:
            vals[name] = t.read16(syms[name]) if size == 2 else t.read8(syms[name])
    s.close()
    return vals


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stm")
    ap.add_argument("--nrf")
    a = ap.parse_args()
    if a.stm:
        print("STM32:", " ".join(f"{k}={v:#x}" for k, v in dump(a.stm, STM_ELF, STM_VARS).items()))
    if a.nrf:
        print("nRF52:", " ".join(f"{k}={v:#x}" for k, v in dump(a.nrf, NRF_ELF, NRF_VARS).items()))


if __name__ == "__main__":
    main()
