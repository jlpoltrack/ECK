#!/usr/bin/env python3
"""Identify which DSLogic channels carry the HHKB Hybrid's STM32<->nRF52 lines.

Halts ECK over SWD and, while dslcap records, pulses each STM32-driven candidate pin
a unique number of times (PB3=1, PB5=2, PA15=3, PB6=4). Pins the nRF52 may drive
(PB4, PC5) are never made outputs: their pull-up/pull-down is cycled instead
(PB4=5, PC5=6 cycles), which only moves a line nothing else is driving.
Uses pyOCD (attach, no reset), then resets the STM32 so ECK restarts.
Run: uv run --with pyocd --with numpy python3 tools/bench/spi_pin_id.py --stm <uid>
"""
import argparse
import configparser
import os
import re
import subprocess
import sys
import time
import zipfile

import numpy as np

DSLCAP = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "Tools-Bench", "dslogic", "build", "dslcap")
PORT = {"A": 0x50000000, "B": 0x50000400, "C": 0x50000800}
MODER, PUPDR, IDR, BSRR = 0x00, 0x0C, 0x10, 0x18
OUTPUTS = [("PB3", 1), ("PB5", 2), ("PA15", 3), ("PB6", 4)]
PULLS = [("PB4", 5), ("PC5", 6)]
UID = ""


def swd(*cmds):
    """Run pyOCD commander commands in one attached session (no reset)."""
    args = ["pyocd", "commander", "-t", "cortex_m", "-M", "attach", "-O", "frequency=4000000", "-u", UID]
    for c in cmds:
        args += ["-c", c]
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode:
        sys.exit("SWD failed:\n" + (r.stdout + r.stderr)[-800:])
    return r.stdout


def rd(addr):
    return int(swd(f"read32 {addr:#x}").split()[1], 16)


def wr(addr, val):
    return f"write32 {addr:#x} {val:#x}"


def pin(name):
    return PORT[name[1]], int(name[2:])


def pulse_outputs():
    for name, count in OUTPUTS:
        base, n = pin(name)
        level = (rd(base + IDR) >> n) & 1
        moder = rd(base + MODER)
        hold = (1 << n) if level else (1 << (n + 16))       # BSRR word keeping the current level
        flip = (1 << (n + 16)) if level else (1 << n)
        writes = [wr(base + BSRR, hold), wr(base + MODER, (moder & ~(3 << 2 * n)) | (1 << 2 * n))]
        for _ in range(count):
            writes += [wr(base + BSRR, flip), "sleep 5", wr(base + BSRR, hold), "sleep 5"]
        writes += [wr(base + MODER, moder)]
        swd(*writes)
        print(f"{name}: idle {level}, pulsed {count}x")
        time.sleep(0.5)


def cycle_pulls():
    for name, count in PULLS:
        base, n = pin(name)
        pupdr = rd(base + PUPDR) & ~(3 << 2 * n)
        moder = rd(base + MODER)
        writes = [wr(base + MODER, moder & ~(3 << 2 * n))]  # input
        for _ in range(count):
            writes += [wr(base + PUPDR, pupdr | (1 << 2 * n)), "sleep 5",   # pull-up
                       wr(base + PUPDR, pupdr | (2 << 2 * n)), "sleep 5"]   # pull-down
        writes += [wr(base + PUPDR, pupdr), wr(base + MODER, moder)]
        swd(*writes)
        print(f"{name}: pull cycled {count}x")
        time.sleep(0.5)


def load_sr(path):
    z = zipfile.ZipFile(path)
    meta = configparser.ConfigParser()
    meta.read_string(z.read("metadata").decode())
    dev = meta["device 1"]
    num, unit = dev["samplerate"].split()
    rate = float(num) * {"Hz": 1, "kHz": 1e3, "MHz": 1e6}[unit]
    names = {int(k[5:]) - 1: v for k, v in dev.items() if k.startswith("probe")}
    parts = sorted((p for p in z.namelist() if p.startswith("logic-1-")), key=lambda p: int(p.rsplit("-", 1)[1]))
    dtype = np.uint8 if dev.get("unitsize", "1") == "1" else np.uint16
    return np.frombuffer(b"".join(z.read(p) for p in parts), dtype=dtype), rate, names


def analyze(path, gap_s=0.2):
    samples, rate, names = load_sr(path)
    label = {c: n for n, c in OUTPUTS + PULLS}
    print(f"\n{len(samples) / rate:.1f} s at {rate / 1e6:g} MHz")
    for ch in sorted(names):
        x = (samples >> ch) & 1
        edges = np.flatnonzero(np.diff(x)) + 1
        if not len(edges):
            print(f"  D{ch}: constant {x[0]}")
            continue
        bursts = np.split(edges, np.flatnonzero(np.diff(edges) > gap_s * rate) + 1)
        desc = []
        for b in bursts:
            pulses = len(b) // 2
            desc.append(f"{pulses}p@{b[0] / rate:.1f}s" + (f"={label[pulses]}" if pulses in label else ""))
        print(f"  D{ch}: idle {x[0]}, {len(edges)} edges; " + ", ".join(desc[:8]) + (" ..." if len(desc) > 8 else ""))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--stm", help="STM32 probe UID (pyOCD -u), needed for a capture: never the nRF52's")
    ap.add_argument("-o", "--out", default="pin_id.sr")
    ap.add_argument("--analyze", help="only analyze an existing capture")
    a = ap.parse_args()
    if a.analyze:
        return analyze(a.analyze)
    if not a.stm:
        sys.exit("--stm <STM32 probe UID> is required for a capture")
    global UID
    UID = a.stm

    ready = a.out + ".ready"
    if os.path.exists(ready):
        os.unlink(ready)
    cap = subprocess.Popen([DSLCAP, "-s", "-r", "1M", "-t", "25000", "-V", "1.5", "--ready", ready, "-o", a.out])
    for _ in range(100):
        if os.path.exists(ready):
            break
        time.sleep(0.1)
    else:
        cap.kill()
        sys.exit("dslcap did not arm")
    os.unlink(ready)
    time.sleep(1)
    swd("halt")
    try:
        pulse_outputs()
        cycle_pulls()
    finally:
        swd("reset")  # restart ECK from a clean reset
    cap.wait()
    analyze(a.out)


if __name__ == "__main__":
    main()
