#!/usr/bin/env python3
"""Log changes in a RAM window of the running nRF52 (HHKB Hybrid) without halting it.

Polls the window over SWD as fast as the probe allows and prints each change with a
timestamp. Used to see PFU's app message buffers during Bank 1 updater traffic.
Run: uv run --with pyocd python3 tools/nrf_ram_watch.py --nrf <uid> --addr 0x20002900 --size 0x100
"""
import argparse
import time

from pyocd.core.helpers import ConnectHelper


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nrf", required=True)
    ap.add_argument("--addr", type=lambda v: int(v, 0), default=0x20002900)
    ap.add_argument("--size", type=lambda v: int(v, 0), default=0x100)
    ap.add_argument("--seconds", type=float, default=60)
    ap.add_argument("--out")
    a = ap.parse_args()
    out = open(a.out, "w") if a.out else None
    t0 = time.monotonic()
    s = t = None
    prev = None
    while time.monotonic() - t0 < a.seconds:
        try:
            if t is None:
                s = ConnectHelper.session_with_chosen_probe(
                    unique_id=a.nrf, target_override="cortex_m", connect_mode="attach",
                    options={"frequency": 8000000, "resume_on_disconnect": True, "auto_unlock": False})
                s.open()
                t = s.board.target
            cur = bytes(t.read_memory_block8(a.addr, a.size))
        except Exception as e:  # nRF52 held in reset (PD2)
            line = f"{time.monotonic() - t0:9.3f} lost ({type(e).__name__})"
            print(line, flush=True)
            if out:
                out.write(line + "\n")
            try:
                s.close()
            except Exception:
                pass
            s = t = None
            prev = None
            time.sleep(0.01)
            continue
        if cur != prev:
            line = f"{time.monotonic() - t0:9.3f} {cur.hex()}"
            if prev is not None:
                diffs = [i for i in range(a.size) if cur[i] != prev[i]]
                line += f"  changed {a.addr + diffs[0]:#x}..{a.addr + diffs[-1]:#x}"
            print(line, flush=True)
            if out:
                out.write(line + "\n")
                out.flush()
            prev = cur
    if s:
        s.close()


if __name__ == "__main__":
    main()
