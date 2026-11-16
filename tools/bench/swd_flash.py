#!/usr/bin/env python3
"""Flash an ECK Bank 2 image to an HHKB over SWD (STM32CubeProgrammer + ST-Link, NRST wired).

Writes the 64 KB Bank 2 section, then the EEPROM words PFU's Bank 1 checks before
booting Bank 2 (0x08080360 = 1, 0x08080370 = CRC-16/MCRF4XX of the section), and resets.
Accepts a .hfb (uses its STM32 section) or a raw .bin (padded with 0xFF to 64 KB).
Run: python3 tools/bench/swd_flash.py [--sn <ST-Link serial>] <.hfb|.bin>
"""
import argparse
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from hybrid_update import BANK2, compute_crc16_mcrf4xx, stm32_section  # noqa: E402

CLI_DEFAULT = ("/Applications/STMicroelectronics/STM32Cube/STM32CubeProgrammer/"
               "STM32CubeProgrammer.app/Contents/Resources/bin/STM32_Programmer_CLI")
WORD_VALID = 0x08080360
WORD_CRC = 0x08080370


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

    sec = stm32_section(a.image)
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
