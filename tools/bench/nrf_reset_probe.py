#!/usr/bin/env python3
"""Check what the HHKB Hybrid's STM32 PD2 does to the nRF52 (reset line or supply).

Attaches to both running chips without halting, clears the nRF52 RESETREAS, drives PD2
high for --hold ms from the STM32 side, drives it low again, then reads RESETREAS:
RESETPIN (bit 0) means PD2 reaches the nRF52 reset pin; a power-on reset clears all bits.

Run: uv run --with pyocd python3 tools/nrf_reset_probe.py --stm <uid> --nrf <uid>
"""
import argparse
import time

from pyocd.core.exceptions import TransferError
from swd_attach import attach

GPIOD = 0x50000C00
MODER, BSRR, BRR = 0x00, 0x18, 0x28
NRF_RESETREAS = 0x40000400
REASONS = {0: "RESETPIN", 1: "DOG", 2: "SREQ", 3: "LOCKUP", 16: "OFF", 17: "LPCOMP", 18: "DIF", 19: "NFC"}


def session(uid):
    return attach(uid, frequency=1000000, resume_on_disconnect=True, auto_unlock=False)


def decode(v):
    return ",".join(n for b, n in REASONS.items() if v >> b & 1) or "power-on"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stm", required=True)
    ap.add_argument("--nrf", required=True)
    ap.add_argument("--hold", type=int, default=20, help="ms to hold PD2 high")
    a = ap.parse_args()
    stm, nrf = session(a.stm), session(a.nrf)
    st, nt = stm.board.target, nrf.board.target
    nt.write32(NRF_RESETREAS, 0xFFFFFFFF)  # write-1-to-clear
    print("nRF RESETREAS before:", hex(nt.read32(NRF_RESETREAS)))
    moder = st.read32(GPIOD + MODER)
    print("PD2 mode:", (moder >> 4) & 3, "(1 = output)")
    st.write32(GPIOD + BSRR, 1 << 2)
    time.sleep(a.hold / 1000)
    try:
        print("nRF answers while PD2 high: RESETREAS", hex(nt.read32(NRF_RESETREAS)))
    except TransferError as e:
        print("nRF does not answer while PD2 high:", e)
    st.write32(GPIOD + BRR, 1 << 2)
    time.sleep(0.5)
    for attempt in range(5):
        try:
            nrf.close()
            nrf = session(a.nrf)
            v = nrf.board.target.read32(NRF_RESETREAS)
            print("nRF RESETREAS after:", hex(v), decode(v))
            break
        except Exception as e:  # reconnect may fail while the nRF52 boots
            print("reconnect failed:", e)
            time.sleep(0.5)
    stm.close()
    nrf.close()


if __name__ == "__main__":
    main()
