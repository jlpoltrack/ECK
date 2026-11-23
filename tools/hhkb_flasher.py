#!/usr/bin/env python3
"""
HHKB Flasher CLI (Open-Source Firmware Updater for Happy Hacking Keyboard)
========================================================================

Communicates directly with PFU's updater on the HHKB Classic / Classic Type-S (and the Hybrid,
whose updater needs PFU's nRF52 app) via USB Raw HID (Vendor ID: 0x04FE, UsagePage: 0xFF00,
Usage: 0x01) to read device info and flash firmware into STM32L072 Bank 2 (0x08010000).

Supports:
- Flashing .hfb containers (65,570 bytes; Hybrid 282,672) via authentic PFU E1/E2 protocol.
- Flashing raw .bin firmware (64 KB, Classic only) by packaging it on the fly into .hfb.
- Bypassing official PFU version downgrade restrictions.
- Local CRC and model compatibility checks before starting an update.

Protocol Notes (from tmk research & reverse engineering):
- Cmd 0x02: Read ID & Version (response: 55 55 02 00 00 39 [data...])
- Cmd 0xE0: Enter Firmup Mode
- Cmd 0xE1: Firmup Start (container length, e.g. 65,570 [22 00 01 00], CRC16, erase Bank 2)
- Cmd 0xE2: Send chunk (Header: E2 00 <len> <seq_lo> <seq_hi> [payload])
- Cmd 0xE3: Firmup End (device verification/reboot; response status unverified)
"""

import argparse
import os
import re
import sys
import time
import hashlib

try:
    import hid
except ImportError:
    print("Error: 'hidapi' package is required.\nRun: pip install hidapi (or: uv run --with hidapi python3 ...)", file=sys.stderr)
    sys.exit(1)

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_hfb import CLASSIC_HFB_SIZE, compute_crc16_mcrf4xx, wrap_binary_to_hfb  # noqa: E402

PFU_VID = 0x04FE
PFU_USAGE_PAGE = 0xFF00
PFU_USAGE = 0x01

REPORT_SIZE = 64
HYBRID_HFB_SIZE = 282672
HYBRID_MD5 = slice(0x10010, 0x10020)  # MD5 of the nRF52 section, 0x10020 up to the 16-byte trailer
# Bank 1 relays the nRF image; do not unplug meanwhile. The nRF52 bootloader finishes about 4 s
# after the final command (measured); 15 s leaves margin.
HYBRID_SETTLE_SEC = 15
CHUNK_PAYLOAD_SIZE = 57  # 7-byte header plus 57 data bytes fills a 64-byte HID report
FIRMUP_REENUMERATION_DELAY_SEC = 10

CMD_GET_INFO = 0x02
CMD_FIRMUP_MODE = 0xE0
CMD_FIRMUP_START = 0xE1
CMD_FIRMUP_SEND = 0xE2
CMD_FIRMUP_END = 0xE3

REQ_HEADER = bytes([0xAA, 0xAA])
RESP_HEADER = bytes([0x55, 0x55])

# Whole families by PFU part number: colour B/W/Y, N = blank keycaps, S = Type-S, SC/SC1 = editions.
# Each family shares one PFU firmware file, so one board (docs/models.md).
MODEL_PATTERNS = {
    "AHHX01": r"PD-KB401[BW]N?",
    "AHHS01": r"PD-KB401[BWY]N?SC[0-9]*",
    "AHUX01": r"PD-KB800[BWY]N?(?:S(?:C[0-9]*)?)?",
    "JIS_UNSUPPORTED": r"PD-KB(?:421|820).*",
}


def device_model(type_number: str):
    """Map recognised physical model numbers to the container model ID."""
    name = type_number.upper().strip()
    for model, pattern in MODEL_PATTERNS.items():
        if re.fullmatch(pattern, name):
            return model
    return None

def hybrid_md5_ok(data: bytes) -> bool:
    return hashlib.md5(data[HYBRID_MD5.stop:-16]).digest() == data[HYBRID_MD5]


def validate_container(data: bytes, expected_model=None, *, check_crc=True, check_model=True):
    """Validate the complete HFB before sending any update command."""
    hybrid = "AHUX01" in (expected_model, data[-16:-10].decode("ascii", errors="replace"))
    if hybrid and len(data) != HYBRID_HFB_SIZE:
        raise ValueError(f"Hybrid container must be exactly {HYBRID_HFB_SIZE:,} bytes")
    if not hybrid and len(data) != CLASSIC_HFB_SIZE:
        raise ValueError(f"Classic container must be exactly {CLASSIC_HFB_SIZE:,} bytes")
    if hybrid and check_crc and not hybrid_md5_ok(data):
        raise ValueError("Invalid Hybrid nRF52 section MD5")
    if len(data) < CLASSIC_HFB_SIZE:
        raise ValueError("Firmware container must be at least 65,570 bytes")
    if check_crc:
        outer = int.from_bytes(data[:2], "little")
        inner = int.from_bytes(data[2:4], "little")
        if outer != compute_crc16_mcrf4xx(data[2:]):
            raise ValueError("Invalid HFB outer CRC")
        if inner != compute_crc16_mcrf4xx(data[4:65540]):
            raise ValueError("Invalid HFB firmware CRC")
    # The trailer ends with model (6), version (4) and 6 x 0xFF in every container size.
    model = data[-16:-10].decode("ascii", errors="replace")
    if check_model:
        if expected_model == "JIS_UNSUPPORTED":
            raise ValueError("JIS models (PD-KB421, PD-KB820) are not yet supported: 69-key matrix routing differs from 60-key ANSI. Use --skip-model-check only for experimental testing.")
        if expected_model is None:
            raise ValueError("Cannot determine keyboard model; use --skip-model-check only if intentional")
        if model != expected_model:
            raise ValueError(f"Firmware model {model!r} does not match keyboard {expected_model}")
    return model

def find_hhkb_device(product_id=None):
    """Locate matching HHKB device with UsagePage 0xFF00 / Usage 0x01."""
    devices = hid.enumerate(PFU_VID)
    matches = []
    for d in devices:
        if product_id is not None and d["product_id"] != product_id:
            continue
        up = d.get("usage_page", 0)
        if up == PFU_USAGE_PAGE or up == 0:
            matches.append(d)

    if not matches:
        return None

    for d in matches:
        if d.get("usage_page") == PFU_USAGE_PAGE and d.get("usage") == PFU_USAGE:
            return d
    # The hidapi libusb backend reports zero usage values for all interfaces.
    # On the Classic, the PFU vendor interface is interface 2.
    for d in matches:
        if d.get("usage_page") == 0 and d.get("interface_number") == 2:
            return d
    return None

class HHKBDevice:
    def __init__(self, dev_info):
        self.dev_info = dev_info
        self.handle = hid.device()
        self.handle.open_path(dev_info["path"])
        self.handle.set_nonblocking(False)

    def close(self):
        try:
            self.handle.close()
        except Exception:
            pass

    def reconnect(self, timeout_sec=10, any_product=False):
        """Reopen the PFU HID interface after Firmup Mode resets USB."""
        self.close()
        # Keymap Tool waits after E0. libusb can still open the old interface
        # before the reset, and the replacement can have the same USB path.
        time.sleep(FIRMUP_REENUMERATION_DELAY_SEC)
        deadline = time.monotonic() + timeout_sec
        last_error = None
        while time.monotonic() < deadline:
            dev_info = find_hhkb_device(None if any_product else self.dev_info["product_id"])
            if dev_info is not None:
                try:
                    handle = hid.device()
                    handle.open_path(dev_info["path"])
                    handle.set_nonblocking(False)
                    self.handle = handle
                    self.dev_info = dev_info
                    return
                except OSError as exc:
                    last_error = exc
                    try:
                        handle.close()
                    except Exception:
                        pass
            time.sleep(0.1)
        message = "PFU HID interface did not return after Firmup Mode"
        if last_error is not None:
            message += f": {last_error}"
        raise TimeoutError(message) from last_error

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def send_report(self, data: bytes):
        """Send a 64-byte report to the keyboard."""
        if len(data) > REPORT_SIZE:
            raise ValueError(f"Packet too large ({len(data)} > {REPORT_SIZE})")
        packet = data + b"\x00" * (REPORT_SIZE - len(data))
        res = self.handle.write(b"\x00" + packet)
        if res != len(packet) + 1:
            raise IOError(f"Incomplete HID write: {res} bytes")
        return res

    def read_report(self, timeout_ms=5000) -> bytes:
        """Read a 64-byte response report from the keyboard."""
        start = time.monotonic()
        timeout_sec = timeout_ms / 1000.0
        while time.monotonic() - start < timeout_sec:
            remaining_ms = max(1, int((timeout_sec - (time.monotonic() - start)) * 1000))
            res = self.handle.read(REPORT_SIZE, timeout_ms=remaining_ms)
            if res:
                return bytes(res)
        raise TimeoutError("Device did not respond within timeout")

    def get_info(self):
        """Query keyboard model and current firmware version (Cmd 0x02)."""
        req = REQ_HEADER + bytes([CMD_GET_INFO, 0x00, 0x00, 0x00])
        self.send_report(req)
        resp = self.read_report(timeout_ms=3000)

        if not resp.startswith(RESP_HEADER + bytes([CMD_GET_INFO])):
            raise RuntimeError(f"Unexpected response to GetInfo: {resp[:8].hex()}")

        # The payload starts at EEPROM 0x310. Version fields correspond to
        # 0x338 and 0x340; their relationship to the active code is unverified.
        type_str = resp[6:26].split(b"\x00")[0].decode("ascii", errors="ignore").strip()
        ver_raw = resp[46:50]

        # Format version string correctly: e.g. [0A 04 02 09] -> "A4.29", [0A 08 00 03] -> "A8.03"
        if len(ver_raw) == 4 and (ver_raw[0] == 0x0A or ver_raw[0] == 0x0B):
            prefix = "A" if ver_raw[0] == 0x0A else "B"
            ver_str = f"{prefix}{ver_raw[1]:X}.{ver_raw[2]:X}{ver_raw[3]:X}"
        else:
            ver_str = ver_raw.decode("ascii", errors="ignore").strip()

        # The documented response has a 57-byte payload, ending at byte 62.
        bank2_broken = (resp[62] == 0x01) if len(resp) > 62 else None

        return {
            "type_number": type_str or "Unknown HHKB",
            "version": ver_str or "Unknown",
            "raw_version": ver_raw.hex(),
            "bank2_broken": bank2_broken,
        }

    def flash_firmware(self, hfb_container: bytes, progress_cb=None, skip_end=False):
        """
        Execute authentic firmware update sequence:
        0xE0 -> 0xE1 (container size, crc) -> 0xE2 (send chunks) -> 0xE3
        """
        if len(hfb_container) < CLASSIC_HFB_SIZE:
            raise ValueError(f"Firmware container must be at least 65,570 bytes (got {len(hfb_container)})")
        # Hybrid containers also carry nRF52 firmware; allow the updater more time per step.
        multi_image = len(hfb_container) > CLASSIC_HFB_SIZE

        outer_crc_bytes = hfb_container[0:2]

        # 1. Cmd E0: Enter Firmup Mode
        req_e0 = REQ_HEADER + bytes([CMD_FIRMUP_MODE, 0x00, 0x00, 0x00])
        self.send_report(req_e0)
        resp = self.read_report(timeout_ms=3000)
        if not resp.startswith(RESP_HEADER + bytes([CMD_FIRMUP_MODE])):
            raise RuntimeError(f"Firmup mode entry rejected: {resp[:6].hex()}")

        print("Waiting for the PFU update interface to reconnect...")
        # The Hybrid updater's USB PID is unverified; match any PFU vendor interface.
        self.reconnect(timeout_sec=20 if multi_image else 10, any_product=multi_image)

        # 2. Cmd E1: Firmup Start
        # Format matching tmk capture: e1 00 08 22 00 01 00 <CRC_LSB> <CRC_MSB> 00 00
        # 0x00010022 = 65,570 in little-endian (22 00 01 00)
        size_bytes = len(hfb_container).to_bytes(4, "little")
        req_e1 = REQ_HEADER + bytes([CMD_FIRMUP_START, 0x00, 0x08]) + size_bytes + outer_crc_bytes + bytes([0x00, 0x00])
        self.send_report(req_e1)

        # Flash erase Bank 2 takes up to 10 seconds; the Hybrid also prepares the nRF52.
        resp = self.read_report(timeout_ms=60000 if multi_image else 15000)
        if not resp.startswith(RESP_HEADER + bytes([CMD_FIRMUP_START])):
            raise RuntimeError(f"Firmup start failed (flash erase error): {resp[:6].hex()}")

        time.sleep(0.2)

        # 3. Cmd E2: Send chunks. PFU sends the container without its outer CRC.
        # Format matching tmk capture: e2 00 <len> <seq_lo> <seq_hi> <payload>
        payload = hfb_container[2:]
        total_bytes = len(payload)
        offset = 0
        seq = 0

        while offset < total_bytes:
            chunk = payload[offset : offset + CHUNK_PAYLOAD_SIZE]
            chunk_len = len(chunk)
            length_byte = chunk_len + 2  # data + 2 bytes sequence
            seq_bytes = seq.to_bytes(2, "little")

            packet = REQ_HEADER + bytes([CMD_FIRMUP_SEND, 0x00, length_byte]) + seq_bytes + chunk
            self.send_report(packet)

            resp = self.read_report(timeout_ms=10000 if multi_image else 3000)
            if not resp.startswith(RESP_HEADER + bytes([CMD_FIRMUP_SEND])):
                raise RuntimeError(f"Firmup send failed at offset {offset} (seq {seq}): {resp[:6].hex()}")

            offset += chunk_len
            seq += 1

            if progress_cb:
                progress_cb(offset, total_bytes)

        time.sleep(0.2)
        if skip_end:
            return None  # research: leave the updater before it activates the image

        # 4. Cmd E3: Firmup End. Prefix alone does not verify device status.
        req_e3 = REQ_HEADER + bytes([CMD_FIRMUP_END, 0x00, 0x00, 0x00])
        self.send_report(req_e3)
        try:
            resp = self.read_report(timeout_ms=30000 if multi_image else 5000)
            if not resp.startswith(RESP_HEADER + bytes([CMD_FIRMUP_END])):
                raise RuntimeError(f"Unexpected firmup end response: {resp[:6].hex()}")
            return True
        except TimeoutError:
            return False

def print_progress(current, total):
    percent = (current / total) * 100
    bar_len = 40
    filled = int(bar_len * current // total)
    bar = "=" * filled + "-" * (bar_len - filled)
    sys.stdout.write(f"\rFlashing: [{bar}] {percent:5.1f}% ({current}/{total} bytes)")
    sys.stdout.flush()

def main():
    parser = argparse.ArgumentParser(
        description="Open-Source Flasher for Happy Hacking Keyboard (HHKB Classic / Type-S)"
    )
    parser.add_argument("firmware", nargs="?", help="Path to .bin or .hfb firmware file to flash")
    parser.add_argument("--info", action="store_true", help="Read device information only (no flashing)")
    parser.add_argument("--force", action="store_true", help="Bypass Bank 2 reset-vector check")
    parser.add_argument("--skip-end", action="store_true", help="Research: transfer everything but never send the final command (E3)")
    parser.add_argument("--skip-crc", action="store_true", help="Skip both HFB CRC checks")
    parser.add_argument("--skip-model-check", action="store_true", help="Skip HFB/device model check")
    parser.add_argument("--model", choices=("AHHX01", "AHHS01"), help="Model ID to use when wrapping a raw .bin (Classic only)")
    args = parser.parse_args()

    print("=== HHKB Open Flasher CLI ===")
    print("Searching for connected HHKB keyboards (VID 0x04FE)...")
    dev_info = find_hhkb_device()

    if not dev_info:
        print("Error: No compatible HHKB device found via USB HID.", file=sys.stderr)
        print("Make sure your HHKB is plugged in and recognized by the OS.", file=sys.stderr)
        sys.exit(1)

    print(f"Found HHKB: {dev_info.get('product_string', 'Keyboard')} (PID: 0x{dev_info['product_id']:04X})")

    with HHKBDevice(dev_info) as kbd:
        info = kbd.get_info()
        print(f"  Model       : {info['type_number']}")
        print(f"  Bank 2 version field (unverified): {info['version']} (raw: {info['raw_version']})")
        if info['bank2_broken']:
            print("  Bank 2 State: BROKEN (Running in Bank 1 recovery bootloader)")

        if args.info or not args.firmware:
            print("\nDevice query completed successfully.")
            return

        fw_path = args.firmware
        if not os.path.isfile(fw_path):
            print(f"Error: File not found: {fw_path}", file=sys.stderr)
            sys.exit(1)

        with open(fw_path, "rb") as f:
            data = f.read()

        expected_model = device_model(info['type_number'])

        if len(data) >= CLASSIC_HFB_SIZE:
            hfb_container = data
            raw_payload = data[4:4+65536]
        elif expected_model == "AHUX01":
            raise ValueError("Hybrid needs a full container: build one with make_hfb.py --nrf-app <app.bin> --softdevice <s132 .hex>")
        elif 8 <= len(data) <= 65536:
            print("Wrapping raw binary into .hfb container...")
            model = args.model or expected_model
            if model is None:
                raise ValueError("Unknown keyboard model: specify --model for a raw binary")
            version = bytes([0x0A, 0x08, 0x05, 0x00]) if model == "AHHS01" else bytes([0x0A, 0x04, 0x05, 0x00])
            hfb_container = wrap_binary_to_hfb(data, model=model, version_bytes=version)
            raw_payload = hfb_container[4:4+65536]
        else:
            raise ValueError(f"Invalid file size: {len(data)} bytes")

        model = validate_container(hfb_container, expected_model, check_crc=not args.skip_crc,
                                   check_model=not args.skip_model_check)
        print(f"Container model: {model}; CRC check: {'skipped' if args.skip_crc else 'passed'}; "
              f"model check: {'skipped' if args.skip_model_check else 'passed'}")

        # Sanity check vector table from payload offset 4
        initial_sp = int.from_bytes(raw_payload[0:4], "little")
        reset_vector = int.from_bytes(raw_payload[4:8], "little")
        print(f"Payload Initial SP: 0x{initial_sp:08X}, Reset Vector: 0x{reset_vector:08X}")

        # Check Bank 2 address range (0x08010000 - 0x08020000)
        if not (0x08010000 <= (reset_vector & ~1) < 0x08020000 and reset_vector & 1) and not args.force:
            print("Warning: Reset vector does not point into Bank 2 (0x08010000-0x08020000).")
            print("Use --force if you intentionally want to flash this image.")
            confirm = input("Continue anyway? (y/N): ").strip().lower()
            if confirm != "y":
                print("Aborted.")
                sys.exit(1)

        print("\nStarting firmware update process...")
        start_time = time.time()
        acknowledged = kbd.flash_firmware(hfb_container, progress_cb=print_progress, skip_end=args.skip_end)
        print("\n\nTransfer finished in {:.2f}s.".format(time.time() - start_time))
        if acknowledged:
            print("Final command acknowledged; verify the device after reboot.")
        else:
            print("No final acknowledgement: update result is unconfirmed. Verify the device after reboot.")
        if len(hfb_container) > CLASSIC_HFB_SIZE:
            # Bank 1 may still be writing the nRF52 image after the final reply.
            for remaining in range(HYBRID_SETTLE_SEC, 0, -1):
                sys.stdout.write(f"\rDo not unplug: waiting for the Bluetooth module update ({remaining:2d}s)")
                sys.stdout.flush()
                time.sleep(1)
            print("\rBluetooth module wait finished; the keyboard can be unplugged.            ")

if __name__ == "__main__":
    main()
