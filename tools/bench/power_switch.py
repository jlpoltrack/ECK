#!/usr/bin/env python3
"""Press the HHKB Hybrid's power switch over SWD and log the LED pins.

Drives PC5 (switch to ground, active low) as an output low for --hold ms, which is what the
switch does electrically, then returns it to an input. Logs PA8 (blue) / PA9 (orange) changes.
--stop-debug first sets DBGMCU DBG_STOP so SWD keeps working in STOP (raises STOP current).
Run: uv run --with pyocd python3 tools/power_switch.py --sn <st-link uid> --hold 300 --watch 3
"""
import argparse
import time

from swd_attach import attach

GPIOA_ODR, GPIOC_MODER, GPIOC_BSRR = 0x50000014, 0x50000800, 0x50000818
RCC_IOPENR, RCC_APB2ENR, DBGMCU_CR = 0x4002102C, 0x40021034, 0x40015804
PIN = 5


def stable_read(t, addr):
    """SWD reads of this STM32 sometimes return 0 for the whole word: never write one back."""
    for _ in range(20):
        a, b = t.read32(addr), t.read32(addr)
        if a == b and a != 0:
            return a
    raise RuntimeError(f"no stable read of {addr:#x}")


def set_pc5_output(t, out):
    """Halted, so the read is reliable even in STOP (RCC then reads as 0 over SWD)."""
    t.halt()
    try:
        moder = stable_read(t, GPIOC_MODER) & ~(3 << 2 * PIN)
        t.write32(GPIOC_MODER, moder | (1 << 2 * PIN) if out else moder)
    finally:
        t.resume()


def leds(t):
    v = t.read32(GPIOA_ODR)
    return ("B" if v >> 8 & 1 else "-") + ("O" if v >> 9 & 1 else "-")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sn", required=True)
    ap.add_argument("--hold", type=int, default=0, help="press length in ms (0 = just watch)")
    ap.add_argument("--watch", type=float, default=3, help="seconds to log LEDs after the press starts")
    ap.add_argument("--stop-debug", action="store_true")
    a = ap.parse_args()
    s = attach(a.sn, resume_on_disconnect=True)
    t = s.board.target
    if a.stop_debug:
        t.halt()
        t.write32(RCC_APB2ENR, stable_read(t, RCC_APB2ENR) | (1 << 22))
        t.write32(DBGMCU_CR, 0x7)  # DBG_SLEEP, DBG_STOP, DBG_STANDBY
        t.resume()
    t0 = time.monotonic()
    pressed = False
    if a.hold:
        t.halt()
        gpioc_on = stable_read(t, RCC_IOPENR) & 0x4
        t.resume()
        if not gpioc_on:
            raise SystemExit("GPIOC clock is off")
        t.write32(GPIOC_BSRR, 1 << (16 + PIN))
        set_pc5_output(t, True)
        pressed = True
    last = None
    while time.monotonic() - t0 < a.watch:
        ms = (time.monotonic() - t0) * 1000
        try:
            if pressed and ms >= a.hold:
                set_pc5_output(t, False)
                pressed = False
                print(f"{ms:7.0f} ms released")
            cur = leds(t)
        except Exception as e:  # STOP mode without --stop-debug, or a reset
            cur = f"no SWD ({type(e).__name__})"
        if cur != last:
            print(f"{ms:7.0f} ms LEDs {cur}")
            last = cur
        time.sleep(0.01)
    try:
        if pressed:
            set_pc5_output(t, False)
        s.close()
    except Exception:
        pass


if __name__ == "__main__":
    main()
