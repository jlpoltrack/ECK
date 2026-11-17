#!/usr/bin/env python3
"""
HHKB Firmware Container (.hfb) Tool
===================================

Wraps a raw binary into a standard 65,570-byte HHKB Classic/Type-S firmware update container (.hfb),
or, with --nrf-app and --softdevice, into a Hybrid container (make_hybrid_hfb) that adds the nRF52 image.
Computes the authentic CRC-16/MCRF4XX (poly 0x1021 reflected 0x8408, init 0xFFFF, no final XOR).

Format breakdown (65,570 bytes total):
- bytes [0:2]  : Outer CRC-16/MCRF4XX over bytes [2:65570], little-endian.
- bytes [2:4]  : Inner CRC-16/MCRF4XX over the 64 KB code payload [4:65540], little-endian.
- bytes [4:65540] : 64 KB Bank 2 firmware binary (padded with 0xFF).
- bytes [65540:65554] : 14 bytes 0xFF padding.
- bytes [65554:65560] : 6 bytes Model ID ASCII (e.g. 'AHHX01', 'AHHS01').
- bytes [65560:65564] : 4 bytes BCD version (e.g. 0A 04 02 09 -> A4.29).
- bytes [65564:65570] : 6 bytes 0xFF padding.

Usage:
    python3 make_hfb.py input.bin output.hfb [--model AHHX01] [--version A4.50]
    python3 make_hfb.py input.bin output.hfb --nrf-app nrf52832_xxaa.bin --softdevice s132_nrf52_7.2.0_softdevice.hex
"""

import sys
import argparse
import hashlib

BANK2_SIZE = 65536
CLASSIC_HFB_SIZE = 65570


def compute_crc16_mcrf4xx(data: bytes) -> int:
    """
    Computes CRC-16/MCRF4XX (poly 0x1021 reflected -> 0x8408, init 0xFFFF, no final XOR).
    Returns 16-bit integer.
    """
    crc = 0xFFFF
    for b in data:
        cur = (crc ^ b) & 0xFF
        for _ in range(8):
            if cur & 1:
                cur = (cur >> 1) ^ 0x8408
            else:
                cur >>= 1
        crc = (crc >> 8) ^ cur
    return crc

def parse_version_bytes(ver_str: str) -> bytes:
    """'A4.29' -> 0A 04 02 09, 'A4.5' -> 0A 04 05 00, 'A4' -> 0A 04 00 00 (hex digits; the A is optional)."""
    ver = ver_str.strip()
    if ver[:1] in ("A", "a"):
        ver = ver[1:]
    parts = ver.split(".")
    minor = parts[1] if len(parts) > 1 else "00"
    return bytes([0x0A, int(parts[0], 16), int(minor[0], 16), int(minor[1:2] or "0", 16)])


def wrap_binary_to_hfb(raw_bin: bytes, model: str = "AHHX01", version_bytes: bytes = bytes([0x0A, 0x04, 0x05, 0x00])) -> bytes:
    """Classic container: outer CRC, inner CRC, 64 KB Bank 2 image (0xFF padded), 30-byte trailer."""
    if len(raw_bin) > BANK2_SIZE:
        raise ValueError(f"Firmware binary size ({len(raw_bin)} bytes) exceeds 64KB bank limit!")
    padded = raw_bin + b"\xff" * (BANK2_SIZE - len(raw_bin))
    inner_tag = compute_crc16_mcrf4xx(padded).to_bytes(2, "little")
    model_bytes = model.encode("ascii")[:6].ljust(6, b"\x00")
    body = inner_tag + padded + b"\xff" * 14 + model_bytes + version_bytes + b"\xff" * 6
    return compute_crc16_mcrf4xx(body).to_bytes(2, "little") + body


def make_hfb(bin_path: str, out_path: str, model: str = "AHHX01", version_str: str = None):
    with open(bin_path, "rb") as f:
        data = f.read()
    if version_str is None:
        version_str = "A8.50" if "S01" in model else "A4.50"
    hfb_data = wrap_binary_to_hfb(data, model, parse_version_bytes(version_str))
    assert len(hfb_data) == CLASSIC_HFB_SIZE
    with open(out_path, "wb") as f:
        f.write(hfb_data)
    print(f"Generated {out_path}: {len(hfb_data)} bytes (Outer CRC: {hfb_data[0:2].hex()}, Inner Tag: {hfb_data[2:4].hex()}, Model: {model}, Version: {version_str})")


NRF_SECTION_BASE = 0x1000  # nRF52 address of the container's first nRF52 byte
NRF_APP_BASE = 0x26000     # application start after the S132 SoftDevice (v6.0.0 and v7.2.0 alike)
NRF_APP_MAX = 0x10000      # the stock section ends at nRF address 0x36000


HYBRID_MODEL = "AHUX01"
HYBRID_VERSION = "A0.48"  # PFU's HHKB800_FW_A048.hfb


def read_ihex(path: str, start: int, end: int) -> bytes:
    """Bytes start..end-1 of an Intel HEX file, 0xFF where it has no data."""
    out = bytearray(b"\xff" * (end - start))
    upper = 0
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line.startswith(":"):
                continue
            rec = bytes.fromhex(line[1:])
            if sum(rec) & 0xFF:
                raise ValueError(f"{path}: bad checksum in {line[:20]}")
            n, addr, kind, data = rec[0], int.from_bytes(rec[1:3], "big"), rec[3], rec[4:4 + rec[0]]
            if kind == 0:
                a = upper + addr
                for i in range(n):
                    if start <= a + i < end:
                        out[a + i - start] = data[i]
            elif kind == 4:
                upper = int.from_bytes(data, "big") << 16
            elif kind == 2:
                upper = int.from_bytes(data, "big") << 4
    return bytes(out)


def make_hybrid_hfb(bin_path: str, nrf_app_path: str, softdevice_hex: str, out_path: str,
                    version_str: str = HYBRID_VERSION):
    """HHKB Hybrid container: STM32 Bank 2 image, then the nRF52 image from 0x1000 (Nordic's
    S132 v7.2.0 SoftDevice + ECK's app at 0x26000), which PFU's nRF52 bootloader rewrites whole."""
    with open(bin_path, "rb") as f:
        data = f.read()
    with open(nrf_app_path, "rb") as f:
        app = f.read()
    if len(data) > BANK2_SIZE:
        raise ValueError(f"Firmware binary size ({len(data)} bytes) exceeds 64KB bank limit!")
    if len(app) > NRF_APP_MAX:
        raise ValueError(f"nRF52 app ({len(app)} bytes) exceeds {NRF_APP_MAX} bytes")
    sd = read_ihex(softdevice_hex, NRF_SECTION_BASE, NRF_APP_BASE)
    if sd[0x3004 - NRF_SECTION_BASE:0x3008 - NRF_SECTION_BASE] != (0x51B1E5DB).to_bytes(4, "little"):
        raise ValueError(f"{softdevice_hex}: no SoftDevice info magic at 0x3004")
    nrf = sd + app + b"\xff" * (NRF_APP_MAX - len(app))
    section = data + b"\xff" * (BANK2_SIZE - len(data))
    trailer = HYBRID_MODEL.encode() + parse_version_bytes(version_str) + b"\xff" * 6
    body = (compute_crc16_mcrf4xx(section).to_bytes(2, "little") + section + b"\xff" * 12 +
            hashlib.md5(nrf).digest() + nrf + trailer)
    hfb = compute_crc16_mcrf4xx(body).to_bytes(2, "little") + body
    with open(out_path, "wb") as f:
        f.write(hfb)
    print(f"Generated {out_path}: {len(hfb)} bytes (model {HYBRID_MODEL}, version {version_str}, "
          f"outer CRC {hfb[0:2].hex()}, STM32 CRC {hfb[2:4].hex()})")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Wrap binary into HHKB .hfb firmware package")
    parser.add_argument("input", help="Input raw .bin file")
    parser.add_argument("output", help="Output .hfb file")
    parser.add_argument("--model", default="AHHX01", help="Model code (default: AHHX01, e.g. AHHS01 for Classic Type-S)")
    parser.add_argument("--version", default=None, help="Firmware version (default: A4.50 for Classic, A8.50 for Type-S)")
    parser.add_argument("--nrf-app", default=None, help="HHKB Hybrid: nRF52 app .bin linked at 0x26000")
    parser.add_argument("--softdevice", default=None, help="HHKB Hybrid: Nordic s132_nrf52_7.2.0_softdevice.hex")

    args = parser.parse_args()
    if args.nrf_app or args.softdevice:
        if not (args.nrf_app and args.softdevice):
            parser.error("the Hybrid needs both --nrf-app and --softdevice")
        make_hybrid_hfb(args.input, args.nrf_app, args.softdevice, args.output, args.version or HYBRID_VERSION)
        sys.exit(0)

    make_hfb(args.input, args.output, model=args.model, version_str=args.version)
