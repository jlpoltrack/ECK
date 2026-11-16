#!/usr/bin/env python3
"""Dump a SEGGER RTT up-buffer from RAM over SWD (pyOCD), no interactive terminal needed.

Usage: rtt_dump.py --uid <probe> --cb <_SEGGER_RTT address> [--target nrf52832] [--channel 0]
"""
import argparse
import struct

from pyocd.core.helpers import ConnectHelper


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--uid", required=True)
    ap.add_argument("--cb", required=True, type=lambda v: int(v, 16))
    ap.add_argument("--target", default="nrf52832")
    ap.add_argument("--channel", type=int, default=0)
    a = ap.parse_args()
    with ConnectHelper.session_with_chosen_probe(unique_id=a.uid, target_override=a.target, connect_mode="attach",
                                                 options={"auto_unlock": False}) as s:
        t = s.target
        # SEGGER_RTT_CB: char acID[16]; int MaxNumUpBuffers; int MaxNumDownBuffers; up buffers (24 bytes each)
        desc = a.cb + 24 + 24 * a.channel
        _, buf, size, wr, rd, _ = struct.unpack("<6I", bytes(t.read_memory_block8(desc, 24)))
        data = bytes(t.read_memory_block8(buf, size))
        out = data[rd:wr] if wr >= rd else data[rd:] + data[:wr]
        print(out.decode(errors="replace"))


if __name__ == "__main__":
    main()
