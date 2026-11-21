#!/usr/bin/env python3
"""Worst-case stack depth from a function, from GCC's -fstack-usage -fcallgraph-info=su output.

Usage: stack_depth.py BUILD_DIR [--indirect F1,F2] [--skip F1,F2] FUNCTION [FUNCTION ...]
  --indirect  functions that indirect calls (function pointers) may reach, e.g. TinyUSB's class driver
  --skip      functions to leave out of every chain (e.g. debug output that is off in normal use)
Build first, e.g.: cmake -S stm32 -B DIR -DHHKB_VARIANT=hybrid \\
    -DCMAKE_C_FLAGS="-fstack-usage -fcallgraph-info=su" && cmake --build DIR
Prints the deepest call chain with each frame. Indirect calls, recursion and functions without
stack data (assembly, libgcc, libc) are listed, since the total can't include them.
"""
import pathlib
import re
import sys

NODE = re.compile(r'node: \{ title: "([^"]+)" label: "([^"]*)"')
EDGE = re.compile(r'edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)"')


def name(title):
    return title.rsplit(":", 1)[-1]


def load(build):
    frames, calls = {}, {}
    for ci in pathlib.Path(build).rglob("*.ci"):
        if "CompilerId" in ci.name:  # CMake's compiler check has its own main()
            continue
        for line in ci.read_text().splitlines():
            if m := NODE.match(line):
                if b := re.search(r"\\n(\d+) bytes \((\w+)", m[2]):
                    frames[name(m[1])] = (int(b[1]), b[2])
            elif m := EDGE.match(line):
                calls.setdefault(name(m[1]), set()).add(name(m[2]))
    return frames, calls


def deepest(fn, frames, calls, stack, memo, notes):
    if fn in SKIP:
        return 0, []
    if fn in stack:
        notes.add(f"recursion: {' -> '.join(stack[stack.index(fn):] + [fn])}")
        return 0, []
    if fn in memo:
        return memo[fn]
    size, kind = frames.get(fn, (0, None))
    if kind is None:
        notes.add(f"no stack data: {fn}")
    elif kind != "static":
        notes.add(f"{kind} frame: {fn}")
    targets = set(calls.get(fn, ()))
    if "__indirect_call" in targets:
        targets |= INDIRECT
        notes.add(f"indirect call in: {fn}" + (" (resolved by --indirect)" if INDIRECT else ""))
    best = (0, [])
    for c in sorted(targets):
        if c != "__indirect_call":
            best = max(best, deepest(c, frames, calls, stack + [fn], memo, notes), key=lambda x: x[0])
    memo[fn] = (size + best[0], [(fn, size)] + best[1])
    return memo[fn]


INDIRECT, SKIP = set(), set()


def main():
    args = sys.argv[1:]
    for flag, target in (("--indirect", INDIRECT), ("--skip", SKIP)):
        while flag in args:
            i = args.index(flag)
            target.update(args[i + 1].split(","))
            del args[i:i + 2]
    if len(args) < 2:
        sys.exit(__doc__)
    frames, calls = load(args[0])
    for root in args[1:]:
        notes = set()
        total, chain = deepest(root, frames, calls, [], {}, notes)
        print(f"{root}: {total} bytes")
        for fn, size in chain:
            print(f"  {size:5}  {fn}")
        for n in sorted(notes):
            print(f"  note: {n}")


if __name__ == "__main__":
    main()
