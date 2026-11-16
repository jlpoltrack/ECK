#!/usr/bin/env python3
"""Map HHKB Hybrid STM32 pins to nRF52 GPIOs with two SWD probes at once.

Halts both chips and makes every candidate pin on both sides an input, so neither
side drives the other. Then, for each STM32 candidate, it switches the STM32's
internal pull-up/pull-down and reads the nRF52 P0.IN register: nRF pins that follow
are wired to that STM32 pin. Both chips are reset afterwards.

Run: uv run --with pyocd python3 tools/pin_map.py --stm <uid> --nrf <uid>
"""
import argparse
import time

from pyocd.core.exceptions import TransferError
from swd_attach import attach

# STM32L072 GPIO
STM_PORT = {"A": 0x50000000, "B": 0x50000400, "C": 0x50000800, "D": 0x50000C00}
MODER, PUPDR, IDR = 0x00, 0x0C, 0x10
# Spare pins: excludes matrix, ADC, DIP switches, LEDs, USB and SWD.
CANDIDATES = ["PA0", "PA2", "PA10", "PA15", "PB3", "PB4", "PB5", "PB6", "PB7", "PB8", "PB9", "PB11",
              "PC0", "PC1", "PC2", "PC3", "PC5", "PC9", "PC10", "PC11", "PC12", "PC13", "PD2"]
# nRF52832 P0
NRF_IN, NRF_PIN_CNF = 0x50000510, 0x50000700
NRF_INPUT_NOPULL = 0x0  # DIR=input, INPUT=connect, PULL=disabled


def session(uid):
    return attach(uid, frequency=4000000, resume_on_disconnect=False, auto_unlock=False)


def prepare_nrf(nt):
    nt.halt()
    for n in range(32):
        nt.write32(NRF_PIN_CNF + 4 * n, NRF_INPUT_NOPULL)


def reverse(st, nt):
    """nRF52 pulls each of its pins up/down; read every STM32 input (except PD2 and SWD)."""
    keep = {("A", 13), ("A", 14), ("D", 2)}
    for port, base in STM_PORT.items():
        moder, pupdr = st.read32(base + MODER), st.read32(base + PUPDR)
        for i in range(16):
            # Leave outputs alone (Bank 1 drives PB6/PC9 low, maybe link enables); analog -> input.
            if (port, i) not in keep and (moder >> 2 * i) & 3 == 3:
                moder &= ~(3 << 2 * i)
                pupdr &= ~(3 << 2 * i)
        st.write32(base + PUPDR, pupdr)
        st.write32(base + MODER, moder)
    for n in range(32):
        if n == 21:  # P0.21 is the nRF52 reset pin by default
            continue
        levels = []
        for pull in (3, 1, 3, 1):  # up, down, up, down (PIN_CNF PULL field)
            nt.write32(NRF_PIN_CNF + 4 * n, pull << 2)
            time.sleep(0.005)
            levels.append({p: st.read32(b + IDR) for p, b in STM_PORT.items()})
        nt.write32(NRF_PIN_CNF + 4 * n, NRF_INPUT_NOPULL)
        hits = [f"P{p}{i}" for p in STM_PORT for i in range(16)
                if (levels[0][p] & levels[2][p] & ~(levels[1][p] | levels[3][p])) >> i & 1]
        print(f"P0.{n:02d} -> " + (", ".join(hits) if hits else "-"), flush=True)


def stm_inputs(st):
    """Analog STM32 pins become plain inputs so IDR reads them; outputs and SWD stay."""
    for port, base in STM_PORT.items():
        moder = st.read32(base + MODER)
        for i in range(16):
            if (port, i) not in {("A", 13), ("A", 14)} and (moder >> 2 * i) & 3 == 3:
                moder &= ~(3 << 2 * i)
        st.write32(base + MODER, moder)


def drive(st, nt, nrf_outputs):
    """Strong drive both ways: nRF app outputs push, then spare STM32 pins push."""
    stm_inputs(st)
    for n in nrf_outputs:
        levels = []
        for v in (1, 0, 1, 0):
            nt.write32(0x50000508 if v else 0x5000050C, 1 << n)  # OUTSET / OUTCLR
            nt.write32(NRF_PIN_CNF + 4 * n, 3)                     # output, input disconnected
            time.sleep(0.002)
            levels.append({p: st.read32(b + IDR) for p, b in STM_PORT.items()})
        nt.write32(NRF_PIN_CNF + 4 * n, NRF_INPUT_NOPULL)
        hits = [f"P{p}{i}" for p in STM_PORT for i in range(16)
                if (levels[0][p] & levels[2][p] & ~(levels[1][p] | levels[3][p])) >> i & 1]
        print(f"nRF P0.{n:02d} drives -> " + (", ".join(hits) if hits else "-"), flush=True)
    prepare_nrf(nt)
    for name in CANDIDATES:
        if name == "PD2":
            continue
        base, i = STM_PORT[name[1]], int(name[2:])
        moder = st.read32(base + MODER)
        levels = []
        for v in (1, 0, 1, 0):
            st.write32(base + 0x18, (1 << i) if v else (1 << (i + 16)))  # BSRR
            st.write32(base + MODER, (moder & ~(3 << 2 * i)) | (1 << 2 * i))
            time.sleep(0.002)
            levels.append(nt.read32(NRF_IN))
        st.write32(base + MODER, moder)
        follow = levels[0] & levels[2] & ~(levels[1] | levels[3])
        hits = [f"P0.{n:02d}" for n in range(32) if follow >> n & 1]
        print(f"STM {name:5s} drives -> " + (", ".join(hits) if hits else "-"), flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--stm", required=True, help="probe UID wired to the STM32")
    ap.add_argument("--nrf", required=True, help="probe UID wired to the nRF52")
    ap.add_argument("--drive", action="store_true", help="strong push-pull drive both ways")
    ap.add_argument("--reverse", action="store_true", help="nRF52 pulls its pins; read all STM32 inputs")
    ap.add_argument("--exclude", default="", help="comma-separated STM32 pins to skip")
    a = ap.parse_args()

    stm, nrf = session(a.stm), session(a.nrf)
    st, nt = stm.target, nrf.target
    try:
        st.halt()
        nt.halt()
        cnf = [nt.read32(NRF_PIN_CNF + 4 * n) for n in range(32)]
        print("nRF PIN_CNF before:", " ".join(f"{n}:{c:x}" for n, c in enumerate(cnf) if c != 2))
        if a.drive:
            outs = [n for n, c in enumerate(cnf) if c & 3 == 3]
            return drive(st, nt, outs)
        prepare_nrf(nt)
        if a.reverse:
            return reverse(st, nt)

        skip = set(filter(None, a.exclude.split(",")))
        pins = [(p, STM_PORT[p[1]], int(p[2:])) for p in CANDIDATES if p not in skip]
        for _, base, i in pins:  # all candidates: input, no pull
            st.write32(base + MODER, st.read32(base + MODER) & ~(3 << 2 * i))
            st.write32(base + PUPDR, st.read32(base + PUPDR) & ~(3 << 2 * i))
        time.sleep(0.01)

        for name, base, i in pins:
            pupdr = st.read32(base + PUPDR) & ~(3 << 2 * i)
            levels = []
            try:
                for pull in (1, 2, 1, 2):  # up, down, up, down
                    st.write32(base + PUPDR, pupdr | (pull << 2 * i))
                    time.sleep(0.005)
                    levels.append(nt.read32(NRF_IN))
            except TransferError:
                # nRF52 went away while this pin was pulled low: a reset or power line (PD2, the
                # known reset, holds it in reset when high and is skipped).
                # Its debug port does not come back cleanly in this session, so stop here.
                st.write32(base + PUPDR, pupdr | (1 << 2 * i))
                print(f"{name:5s} -> nRF52 stops responding when pulled low (reset/power); "
                      f"rerun with --exclude {name}", flush=True)
                break
            st.write32(base + PUPDR, pupdr)
            follow = (levels[0] & levels[2]) & ~(levels[1] | levels[3])
            hits = [n for n in range(32) if follow >> n & 1]
            print(f"{name:5s} -> " + (", ".join(f"P0.{n:02d}" for n in hits) if hits else "-"), flush=True)
    finally:
        try:
            nt.reset()
        except TransferError:
            print("nRF52 reset over SWD failed: power-cycle the keyboard")
        st.reset()
        nrf.close()
        stm.close()


if __name__ == "__main__":
    main()
