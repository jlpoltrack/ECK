#!/usr/bin/env python3
"""Average each key's raw reading for a few seconds (hands off) and save it as JSON, or compare two
snapshots: per-key ratio new/old. For judging R3S scan timing changes (docs/r3s-hardware.md).
Usage: ECK_PRODUCT=R3S uv run -q --with hidapi python3 tools/r3s/rest_snapshot.py save OUT.json [SEC]
       python3 tools/r3s/rest_snapshot.py compare OLD.json NEW.json"""
import json
import os
import statistics
import sys
import time

if sys.argv[1] == "compare":
    old, new = (json.load(open(p)) for p in sys.argv[2:4])
    keys = [k for k in old if old[k] > 0 and new.get(k, 0) > 0]
    ratio = sorted(new[k] / old[k] for k in keys)
    print(f"{len(keys)} keys: ratio new/old min {ratio[0]:.3f} median {statistics.median(ratio):.3f} max {ratio[-1]:.3f}")
    print(f"rest old median {statistics.median(old[k] for k in keys):.0f}, new {statistics.median(new[k] for k in keys):.0f}")
    sys.exit(0)

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "bench"))
import key_depth as kd  # noqa: E402

k = kd.Kbd()
sums, n, t = [0] * kd.N, 0, time.time()
while time.time() - t < (float(sys.argv[3]) if len(sys.argv) > 3 else 3):
    raw = k.diag(1)
    sums = [a + b for a, b in zip(sums, raw)]
    n += 1
json.dump({str(i): s / n for i, s in enumerate(sums)}, open(sys.argv[2], "w"))
print(f"saved {sys.argv[2]} ({n} samples)")
