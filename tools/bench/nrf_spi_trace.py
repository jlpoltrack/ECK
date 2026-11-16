#!/usr/bin/env python3
"""Trace the frames PFU's nRF52 app exchanges with Bank 1 over the HHKB Hybrid's SPI link.

PFU's app (stock A0.48) is the SPI master and runs a legacy SPI0 driver: transfer start at
0x2b9a8 (r0 = TX buffer; other arguments logged raw) and completion at 0x2ba6c.
Hardware breakpoints there pause only the master, so no byte is lost. Prints one line per frame.
Run: uv run --with pyocd python3 tools/bench/nrf_spi_trace.py --nrf <uid> [--seconds N] [--out f]
"""
import argparse
import time

from swd_attach import attach

START, DONE = 0x2B9A8, 0x2BA6C
DHCSR, DEMCR = 0xE000EDF0, 0xE000EDFC
FP_CTRL, FP_COMP0 = 0xE0002000, 0xE0002008
P0_IN = 0x50000510
LAST_RX = 0x20003104


def connect(uid):
    s = attach(uid, frequency=4000000, resume_on_disconnect=True, auto_unlock=False)
    t = s.board.target
    arm_fpb(t)
    return s, t


def arm_fpb(t):
    """pyOCD writes breakpoints only when it resumes the core, so program the FPB directly."""
    t.write32(FP_CTRL, 3)  # KEY | ENABLE
    for i, addr in enumerate((START, DONE)):
        t.write32(FP_COMP0 + 4 * i, (addr & 0x1FFFFFFC) | (0x80000000 if addr & 2 else 0x40000000) | 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nrf", required=True)
    ap.add_argument("--seconds", type=float, default=30)
    ap.add_argument("--max", type=int, default=100000, help="stop after this many frames")
    ap.add_argument("--out", help="also append lines to this file")
    a = ap.parse_args()
    out = open(a.out, "a") if a.out else None

    def emit(line):
        print(line, flush=True)
        if out:
            out.write(line + "\n")
            out.flush()

    t0 = time.monotonic()
    s = t = None
    n = 0
    pending = None
    try:
        while time.monotonic() - t0 < a.seconds and n < a.max:
            try:
                if t is None:
                    s, t = connect(a.nrf)
                    emit(f"{time.monotonic() - t0:9.3f} armed")
                dhcsr = t.read32(DHCSR)
                if dhcsr & (1 << 25):  # S_RESET_ST: reset by PD2, FPB cleared
                    arm_fpb(t)
                    emit(f"{time.monotonic() - t0:9.3f} nRF52 reset, re-armed")
                if not dhcsr & (1 << 17):
                    time.sleep(0.0003)
                    continue
                pc = t.read_core_register("pc")
                ts = time.monotonic() - t0
                if pc == START:
                    r = [t.read_core_register(f"r{i}") for i in range(4)]
                    tx = bytes(t.read_memory_block8(r[0], 64)) if 0x20000000 <= r[0] < 0x20010000 else b""
                    pin18 = (t.read32(P0_IN) >> 18) & 1
                    lr = t.read_core_register("lr")
                    last_rx = t.read8(LAST_RX)  # RX byte of the previous transfer without an RX buffer
                    emit(f"{ts:9.3f} START lr={lr:#x} prev_rx={last_rx:02x} r0={r[0]:#x} r1={r[1]:#x} r2={r[2]:#x} "
                         f"r3={r[3]:#x} req={pin18} tx={tx[:max(r[2], 1)].hex()}")
                elif pc == DONE:
                    r0 = t.read_core_register("r0")
                    rx = bytes(t.read_memory_block8(r0, 64)) if 0x20000000 <= r0 < 0x20010000 else b""
                    emit(f"{ts:9.3f} DONE r0={r0:#x} rx={rx.hex()}")
                    n += 1
                t.resume()
            except Exception as e:  # nRF52 held in reset by the STM32 (PD2): reconnect
                emit(f"{time.monotonic() - t0:9.3f} lost ({type(e).__name__})")
                try:
                    s.close()
                except Exception:
                    pass
                s = t = None
                time.sleep(0.01)
    except KeyboardInterrupt:
        pass
    if t is not None:
        try:
            t.write32(FP_COMP0, 0)
            t.write32(FP_COMP0 + 4, 0)
            if t.read32(DHCSR) & (1 << 17):
                t.resume()
            s.close()
        except Exception:
            pass


if __name__ == "__main__":
    main()
