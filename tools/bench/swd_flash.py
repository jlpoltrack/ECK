#!/usr/bin/env python3
"""Flash a ECK Bank 2 image to an HHKB over SWD (STM32CubeProgrammer + ST-Link, NRST wired).

Writes the 64 KB Bank 2 section, then the EEPROM words PFU's Bank 1 checks before
booting Bank 2 (0x08080360 = 1, 0x08080370 = CRC-16/MCRF4XX of the section), and resets.
Accepts a .hfb (uses its STM32 section) or a raw .bin (padded with 0xFF to 64 KB).
"""
import argparse
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from repo_paths import REPO  # noqa: E402

sys.path.insert(0, str(REPO / "tools/hhkb"))
from make_hfb import compute_crc16_mcrf4xx  # noqa: E402

CLI_DEFAULT = ("/Applications/STMicroelectronics/STM32Cube/STM32CubeProgrammer/"
               "STM32CubeProgrammer.app/Contents/Resources/bin/STM32_Programmer_CLI")
BANK2 = 0x08010000
SECTION = 0x10000
WORD_VALID = 0x08080360
WORD_CRC = 0x08080370


def section_from(path):
    data = open(path, "rb").read()
    if path.endswith(".hfb"):
        if len(data) < 4 + SECTION or int.from_bytes(data[2:4], "little") != compute_crc16_mcrf4xx(data[4:4 + SECTION]):
            sys.exit(f"{path}: not a valid .hfb (size or STM32 CRC)")
        sec = data[4:4 + SECTION]
        sp, reset = int.from_bytes(sec[0:4], "little"), int.from_bytes(sec[4:8], "little")
        if sp >> 24 != 0x20 or not BANK2 <= reset < BANK2 + SECTION:
            sys.exit(f"{path}: Bank 2 vectors look wrong (SP {sp:#x}, reset {reset:#x})")
        return sec
    if len(data) > SECTION:
        sys.exit(f"{path}: {len(data)} bytes exceeds the 64 KB Bank 2")
    return data + b"\xff" * (SECTION - len(data))


SERIAL = []  # ["sn=<ST-Link serial>"] when --sn is given


def run(cli, *args):
    cmd = [cli, "-c", "port=SWD", *SERIAL, "freq=1000", "mode=UR", "reset=HWrst", *args]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    out = proc.stdout
    if proc.returncode != 0 or "Error" in out:
        sys.exit(f"STM32_Programmer_CLI failed: {' '.join(args)}\n{out[-1500:]}")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("image", help=".hfb or .bin")
    ap.add_argument("--cli", default=os.environ.get("STM32_PROGRAMMER_CLI", CLI_DEFAULT))
    ap.add_argument("--no-reset", action="store_true", help="leave the core held after writing")
    ap.add_argument("--sn", help="ST-Link serial number, when several probes are attached")
    a = ap.parse_args()
    if a.sn:
        SERIAL.append(f"sn={a.sn}")

    sec = section_from(a.image)
    crc = compute_crc16_mcrf4xx(sec)
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
        f.write(sec)
        tmp = f.name
    try:
        run(a.cli, "-w", tmp, hex(BANK2), "-v")
    finally:
        os.unlink(tmp)
    run(a.cli, "-w32", hex(WORD_VALID), "0x1", "-w32", hex(WORD_CRC), f"0x{crc:08X}")
    print(f"Bank 2 written and verified, CRC tag 0x{crc:04X}")
    if not a.no_reset:
        run(a.cli, "-rst")
        print("Reset")


if __name__ == "__main__":
    main()
