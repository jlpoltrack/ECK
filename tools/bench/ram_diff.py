#!/usr/bin/env python3
"""Find RAM that a USB request changes on a running STM32 (HHKB), without halting it.

Takes RAM snapshots over SWD: two at rest (to mask counters that change anyway), then
runs --cmd (e.g. a flasher request) and takes another; prints changed ranges with bytes.
Run: uv run --with pyocd --with hidapi python3 tools/bench/ram_diff.py --stm <uid> --cmd "..."
"""
import argparse
import shlex
import subprocess
import time

from swd_attach import attach

RAM, SIZE = 0x20000000, 0x5000


def snap(t):
    return bytes(t.read_memory_block8(RAM, SIZE))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stm", required=True)
    ap.add_argument("--cmd", required=True)
    ap.add_argument("--out", help="save the after-snapshot here")
    a = ap.parse_args()
    s = attach(a.stm, frequency=4000000, resume_on_disconnect=True)
    t = s.board.target
    r1 = snap(t)
    time.sleep(0.5)
    r2 = snap(t)
    noisy = {i for i in range(SIZE) if r1[i] != r2[i]}
    r = subprocess.run(shlex.split(a.cmd), capture_output=True, text=True)
    if r.returncode:
        s.close()
        raise SystemExit(f"command failed ({r.returncode}):\n{r.stdout}{r.stderr}")
    time.sleep(0.3)
    r3 = snap(t)
    s.close()
    if a.out:
        open(a.out, "wb").write(r3)
    changed = [i for i in range(SIZE) if r3[i] != r2[i] and i not in noisy]
    ranges = []
    for i in changed:
        if ranges and i - ranges[-1][1] <= 8:
            ranges[-1][1] = i
        else:
            ranges.append([i, i])
    print(f"{len(noisy)} noisy bytes masked, {len(changed)} changed")
    for lo, hi in ranges:
        lo8, hi8 = lo & ~7, (hi | 7) + 1
        print(f"{RAM + lo8:#010x}: before {r2[lo8:hi8].hex(' ')}")
        print(f"{'':10}  after  {r3[lo8:hi8].hex(' ')}")


if __name__ == "__main__":
    main()
