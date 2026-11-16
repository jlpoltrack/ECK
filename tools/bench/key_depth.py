#!/usr/bin/env python3
"""Measure HHKB Topre key depth over USB (firmware built with -d ...,HHKB_ADC_DIAGNOSTICS).

  live           press keys slowly: shows the deepest key; after each press prints the depth at
                 which the firmware registered it (actuation) and released it, and the peak
  noise [SEC]    hands off: per-key noise at rest (worst keys, and a map by matrix position)
  record SEC F   hands off: raw readings of keys c0r0..c3r0 (13 keys) at scan rate, as JSON, for
                 tools/rt_noise_sim.c
  press [SEC]    press keys fully a few times: per key the rest and full-press ADC readings and the
                 readings at which the firmware registered each press
  cal            each key's rest level and range (calibration) and its reading now
  scantime [SEC] longest scan with interrupts held off, per second

Depth is permille (1/1000) of the key's calibrated range: 0 at rest, 1000 bottomed out.
Run: uv run --with hidapi python3 tools/bench/key_depth.py live
"""
import os
import statistics
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from via_hid import open_keyboard, send  # noqa: E402

ROWS, COLS = 4, 15
N = ROWS * COLS
DIAG_ID = 31
NAMES = {**{0x04 + i: chr(65 + i) for i in range(26)}, **{0x1E + i: str((i + 1) % 10) for i in range(10)},
         0x28: "Enter", 0x29: "Esc", 0x2A: "BkSp", 0x2B: "Tab", 0x2C: "Space", 0x2D: "-", 0x2E: "=",
         0x2F: "[", 0x30: "]", 0x31: "\\", 0x33: ";", 0x34: "'", 0x35: "`", 0x36: ",", 0x37: ".",
         0x38: "/", 0xE0: "LCtrl", 0xE1: "LShift", 0xE2: "LAlt", 0xE3: "LGui", 0xE5: "RShift",
         0xE6: "RAlt", 0xE7: "RGui", 0x4C: "Del"}


class Kbd:
    def __init__(self):
        self.h = open_keyboard()

    def q(self, b):
        r = send(self.h, b)
        if len(r) < 32:
            sys.exit("no reply from the keyboard")
        return r

    def diag_block(self, kind, first):
        if not getattr(self, "checked", False):  # without the readout the request is echoed: zeros
            self.checked = True
            if not any(self.diag_block(3, 0)):  # every key has a nonzero range in a real reply
                sys.exit("no depth data: flash a build from ./build.sh hybrid -d HHKB_ADC_DIAGNOSTICS")
        r = self.q([0x08, 0, DIAG_ID, kind, first])
        return [int.from_bytes(r[5 + 2 * i:7 + 2 * i], "little") for i in range(min(13, N - first))]

    def diag(self, kind):
        return [v for first in range(0, N, 13) for v in self.diag_block(kind, first)]

    def pressed(self):
        r = self.q([0x02, 0x03])
        bits = set()
        for row in range(ROWS):
            v = int.from_bytes(r[2 + 2 * row:4 + 2 * row], "big")
            bits |= {col * ROWS + row for col in range(COLS) if v >> col & 1}
        return bits

    def names(self):
        r = b""
        for off in range(0, N * 2, 28):
            r += self.q([0x12, off >> 8, off & 255, min(28, N * 2 - off)])[4:4 + min(28, N * 2 - off)]
        names = []
        for i in range(N):
            col, row = divmod(i, ROWS)
            kc = int.from_bytes(r[2 * (row * COLS + col):2 * (row * COLS + col) + 2], "big")
            names.append(NAMES.get(kc, f"r{row}c{col}"))
        return names


def live(k):
    names = k.names()
    print("press keys slowly (Ctrl+C to stop)")
    cur, peak, act, rel = None, 0, None, None
    while True:
        depth, down = k.diag(0), k.pressed()
        i = max(range(N), key=lambda j: depth[j])
        if cur is None and depth[i] > 30:
            cur, peak, act, rel = i, 0, None, None
        if cur is not None:
            d = depth[cur]
            peak = max(peak, d)
            if act is None and cur in down:
                act = d
            if act is not None and rel is None and cur not in down:
                rel = d
            bar = "#" * (min(d, 1100) // 25)
            print(f"\r{names[cur]:>6} {d:5d} {'DOWN' if cur in down else 'up  '} {bar:<44}", end="", flush=True)
            if d < 15:
                print(f"\r{names[cur]:>6}: peak {peak}, registered at {act}, released at {rel}" + " " * 30)
                cur = None


def noise(k, seconds):
    names, samples, t = k.names(), [], time.time()
    print(f"hands off for {seconds:.0f} s ...")
    while time.time() - t < seconds:
        samples.append((k.diag(1), k.diag(0)))
    rng = k.diag(3)
    rows = []
    for i in range(N):
        raw = [s[0][i] for s in samples]
        rows.append((max(raw) - min(raw), statistics.pstdev(raw), names[i], rng[i]))
    rows.sort(reverse=True)
    print(f"{len(samples)} samples. Worst keys (peak-to-peak counts, sd, range, peak-to-peak as permille):")
    for pp, sd, name, r in rows[:8]:
        print(f"  {name:>6}: {pp:4d}  sd {sd:5.1f}  range {r:5d}  {1000 * pp / r if r else 0:5.1f}")
    print(f"median peak-to-peak {statistics.median(r[0] for r in rows)} counts")
    print("sd by position (rows down, columns across):")
    for row in range(ROWS):
        print(" ".join(f"{statistics.pstdev([s[0][c * ROWS + row] for s in samples]):5.1f}" for c in range(COLS)))
    typed = [names[i] for i in range(N) if max(s[0][i] for s in samples) > 1200]
    if typed:
        print("pressed during the run (ignore these):", typed)


def record(k, seconds, path):
    import json
    vals, t = [], time.time()
    while time.time() - t < seconds:
        vals.append(k.diag_block(1, 0))
    json.dump(vals, open(path, "w"))
    print(f"{len(vals)} samples to {path}")


def press(k, seconds):
    names, rest, hi, act, prev = k.names(), [[] for _ in range(N)], [0] * N, [[] for _ in range(N)], set()
    print(f"press keys slowly, fully, a few times each, for {seconds:.0f} s ...", flush=True)
    t = time.time()
    while time.time() - t < seconds:
        raw, down = k.diag(1), k.pressed()
        for i in range(N):
            hi[i] = max(hi[i], raw[i])
            if i in down and i not in prev:
                act[i].append(raw[i])
            if i not in down:
                rest[i].append(raw[i])
        prev = down
    for i in sorted((i for i in range(N) if act[i]), key=lambda i: names[i]):
        r = sorted(rest[i])
        print(f"{names[i]:>6}: rest {r[len(r) // 2]:4d}  full press {hi[i]:4d}  registered at {act[i]}")
    print(f"pressed keys: {sum(1 for a in act if a)}")


def cal(k):
    names, rest, rng, raw = k.names(), k.diag(2), k.diag(3), k.diag(1)
    for i in range(N):
        print(f"{names[i]:>6}: rest {rest[i]:5d} range {rng[i]:5d} now {raw[i]:5d}")


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "live"
    k = Kbd()
    if cmd == "live":
        try:
            live(k)
        except KeyboardInterrupt:
            print()
    elif cmd == "noise":
        noise(k, float(sys.argv[2]) if len(sys.argv) > 2 else 10)
    elif cmd == "record" and len(sys.argv) > 3:
        record(k, float(sys.argv[2]), sys.argv[3])
    elif cmd == "press":
        press(k, float(sys.argv[2]) if len(sys.argv) > 2 else 20)
    elif cmd == "scantime":
        k.diag_block(4, 0)  # reset the maximum
        for _ in range(int(float(sys.argv[2]) if len(sys.argv) > 2 else 5)):
            time.sleep(1)
            print(f"longest scan with interrupts off: {k.diag_block(4, 0)[0]} us", flush=True)
    elif cmd == "cal":
        cal(k)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
