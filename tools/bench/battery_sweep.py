#!/usr/bin/env python3
"""Find the HHKB Hybrid's battery-level steps: a PPK2 (source mode, on the battery terminals)
steps the supply down while macOS reports the battery level the keyboard sends over Bluetooth.

Uses the PPK2 library from the Tools-Bench repo next to ECK (its `bench` package). The keyboard must be on
(hold the switch once the supply is up) and connected to the Mac. Prints one line per step.
Run: uv run --with-editable ../Tools-Bench python3 tools/bench/battery_sweep.py --start 3200 --stop 1900 --step 100
"""
import argparse
import re
import subprocess
import sys
import time

try:
    from bench.ppk2 import ppk2lib as P
except ModuleNotFoundError as e:
    if e.name not in ("bench", "bench.ppk2"):  # a real error inside the package: show it
        raise
    sys.exit("needs Tools-Bench's bench package: uv run --with-editable ../Tools-Bench python3 "
             "tools/bench/battery_sweep.py ...")


def mac_battery(name_hint):
    """Battery Level that macOS shows for the Bluetooth device named like name_hint (None if absent)."""
    out = subprocess.run(["system_profiler", "SPBluetoothDataType"], capture_output=True, text=True).stdout
    i = out.lower().find(name_hint.lower())
    m = re.search(r"Battery Level: (\d+)%", out[i:i + 600]) if i >= 0 else None
    return int(m.group(1)) if m else None


def flashes(bins, threshold_ma=1.5):
    """Orange LED flashes in 100 ms bins: runs of >= 3 bins above the median + threshold, as start
    times in seconds; a pair 1-1.5 s apart is the two-flash (very low) warning."""
    hi = bins > (sorted(bins)[len(bins) // 2] + threshold_ma)
    starts, run = [], 0
    for i, h in enumerate(list(hi) + [False]):
        if h:
            run += 1
        else:
            if run >= 3:
                starts.append(round((i - run) / 10, 1))
            run = 0
    return starts


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--start", type=int, default=3200, help="mV")
    ap.add_argument("--stop", type=int, default=1900, help="mV")
    ap.add_argument("--step", type=int, default=100, help="mV")
    ap.add_argument("--hold", type=float, default=40, help="seconds per step")
    ap.add_argument("--warmup", type=float, default=60, help="seconds at the start voltage to power on")
    ap.add_argument("--name", default="HHKB", help="part of the keyboard's Bluetooth name")
    ap.add_argument("--capture", action="store_true",
                    help="record current at each step: the orange low-battery blink shows as swing")
    a = ap.parse_args()
    with P.PPK2() as ppk:
        ppk.set_mode("source")
        ppk.set_vdd(a.start)
        ppk.dut_power(True)
        print(f"supply on at {a.start} mV: hold the switch to turn the keyboard on", flush=True)
        time.sleep(a.warmup)
        mv = a.start
        while mv >= a.stop:
            ppk.set_vdd(mv)
            if a.capture:
                cur = ppk.capture(a.hold).current * 1e3
                bins = cur[: len(cur) // 10000 * 10000].reshape(-1, 10000).mean(1)  # 100 ms
                print(f"{mv} mV: mean {cur.mean():.2f} mA, flashes {flashes(bins)}, "
                      f"Mac battery {mac_battery(a.name)}", flush=True)
            else:
                time.sleep(a.hold)
                print(f"{mv} mV: Mac battery {mac_battery(a.name)}", flush=True)
            mv -= a.step
        ppk.dut_power(False)


if __name__ == "__main__":
    main()
