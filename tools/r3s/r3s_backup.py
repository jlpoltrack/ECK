#!/usr/bin/env python3
"""Read a Realforce R3S's data EEPROM and flash banks through a temporary Bank 2 image.

  wrap STOCK.rfb IMAGE.bin OUT.rfb   wrap a raw Bank 2 image in an .rfb with the stock trailer
  install FILE.rfb                   flash an .rfb through Topre's updater (stock or our image)
  read DIR                           from ST's DFU loader: save eeprom.bin, bank1.bin, bank2.bin
  restore FILE.rfb                   from ST's DFU loader: write its Bank 2 image and Topre's CRC tag
  flash FILE.rfb                     from any state: Topre's updater (stock firmware running), or ST's
                                     DFU loader (already in it, or entered from ECK over VIA)

The temporary image (tools/r3s/dfu_jump.c, build with `make -C tools/r3s`) starts ST's loader.
Run with: uv run -q --with hidapi python3 tools/r3s/r3s_backup.py ...
"""
import argparse
import os
import pathlib
import subprocess
import sys
import tempfile
import time

import hid

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import hhkb_flasher as flasher  # noqa: E402
from make_hfb import compute_crc16_mcrf4xx as crc16  # noqa: E402
import hybrid_update as dfu  # noqa: E402

TOPRE_VID = 0x0853
RFB_SIZE = 65568  # 2 + 2 CRCs, 64 KB Bank 2 image, 28-byte trailer
SECTION = 0x10000
TAG_WORD = 0x080817E0  # Topre's Bank 2 tag: CRC-16 of the 64 KB image in bytes 2-3 (found 2026-11-17)
DFU_ID = "0483:df11"
READS = (("eeprom.bin", 2, 0x08080000, 0x1800), ("bank1.bin", 0, 0x08000000, SECTION),
         ("bank2.bin", 0, 0x08010000, SECTION))


def find_r3s(product_id=None):
    for d in hid.enumerate(TOPRE_VID):
        if product_id is not None and d["product_id"] != product_id:
            continue
        if d.get("usage_page") == 0xFF00:
            return d
    return None


def check_rfb(data):
    if len(data) != RFB_SIZE:
        sys.exit(f"expected a {RFB_SIZE}-byte .rfb, got {len(data)} bytes")
    if int.from_bytes(data[0:2], "little") != crc16(data[2:]):
        sys.exit("outer CRC mismatch")
    if int.from_bytes(data[2:4], "little") != crc16(data[4:4 + SECTION]):
        sys.exit("inner CRC mismatch")
    sp, reset = (int.from_bytes(data[4 + i:8 + i], "little") for i in (0, 4))
    if not (0x20000000 <= sp <= 0x20005000 and 0x08010000 <= reset < 0x08020000 and reset & 1):
        sys.exit(f"bad vectors: SP {sp:#010x}, reset {reset:#010x}")


def wrap(stock_path, image_path, out_path):
    stock = pathlib.Path(stock_path).read_bytes()
    check_rfb(stock)
    image = pathlib.Path(image_path).read_bytes()
    if len(image) > SECTION:
        sys.exit("image larger than 64 KB")
    payload = image + b"\xff" * (SECTION - len(image))
    body = crc16(payload).to_bytes(2, "little") + payload + stock[4 + SECTION:]
    out = crc16(body).to_bytes(2, "little") + body
    check_rfb(out)
    pathlib.Path(out_path).write_bytes(out)
    print(f"wrote {out_path} (trailer {stock[4 + SECTION:].hex()})")


def install(path):
    data = pathlib.Path(path).read_bytes()
    check_rfb(data)
    # hhkb_flasher speaks the same updater protocol; point its lookups at Topre's VID and size.
    flasher.find_hhkb_device = find_r3s
    flasher.CLASSIC_HFB_SIZE = RFB_SIZE
    dev = find_r3s()
    if dev is None:
        sys.exit("no R3S vendor interface (VID 0x0853, usage page 0xFF00) found")
    print(f"found {dev['product_string']} (PID {dev['product_id']:#06x})")
    with flasher.HHKBDevice(dev) as kbd:
        info = kbd.get_info()
        print(f"model {info['type_number']}, version {info['version']}")
        ok = kbd.flash_firmware(data, progress_cb=flasher.print_progress)
    print(f"\nfinal command {'acknowledged' if ok else 'not acknowledged'}")


def dfu_present():
    r = subprocess.run(["dfu-util", "-l", "-d", DFU_ID], capture_output=True, text=True, timeout=10)
    return "Found DFU" in r.stdout


def read(out_dir):
    for _ in range(60):
        if dfu_present():
            break
        time.sleep(0.5)
    else:
        sys.exit("ST's DFU loader did not appear")
    os.makedirs(out_dir, exist_ok=True)
    for name, alt, addr, n in READS:
        with tempfile.TemporaryDirectory() as d:
            tmp = os.path.join(d, "r.bin")
            r = subprocess.run(["dfu-util", "-d", DFU_ID, "-a", str(alt), "-s", f"{addr:#010x}:{n}", "-U", tmp],
                               capture_output=True, text=True, timeout=120)
            if r.returncode != 0:
                sys.exit(f"{name}: dfu-util failed:\n{r.stdout}{r.stderr}")
            data = pathlib.Path(tmp).read_bytes()
        if len(data) != n:
            sys.exit(f"{name}: read {len(data)} bytes, expected {n}")
        pathlib.Path(out_dir, name).write_bytes(data)
        print(f"saved {os.path.join(out_dir, name)} ({n} bytes)")


def is_topre_bank1(bank1):
    """Topre's updater in Bank 1 names Topre; an HHKB's (PFU's updater or ECK's stub) doesn't (eck.js isTopreBank1)."""
    return b"Topre" in bank1


def restore(path):
    data = pathlib.Path(path).read_bytes()
    check_rfb(data)
    if not dfu_present():
        sys.exit("ST's DFU loader not found")
    # In ST's loader an HHKB and the R3S look alike (0483:df11): never write an R3S image to an HHKB
    if not is_topre_bank1(dfu.read(0, 0x08000000, SECTION)):
        sys.exit("the keyboard in ST's DFU loader has no Topre updater in Bank 1, so it is not the R3S "
                 "(an HHKB?); nothing written. To restart it, unplug USB (a Hybrid on batteries: take them out too)")
    image = data[4:4 + SECTION]
    word = bytearray(dfu.read(2, TAG_WORD, 4))
    word[2:4] = b"\0\0"
    dfu.write_verified(2, TAG_WORD, bytes(word), "Bank 2 CRC tag cleared")  # a half-written image never boots
    dfu.write_verified(0, 0x08010000, image, "Bank 2")
    word[2:4] = crc16(image).to_bytes(2, "little")
    dfu.write_verified(2, TAG_WORD, bytes(word), f"Bank 2 CRC tag ({crc16(image):#06x})")
    dfu.leave()


def find_eck():
    for d in hid.enumerate():
        if d["usage_page"] == 0xFF60 and d["usage"] == 0x61 and "R3S" in (d["product_string"] or ""):
            return d
    return None


def flash(path):
    if dfu_present():
        return restore(path)
    if find_r3s() is not None:
        return install(path)
    dev = find_eck()
    if dev is None:
        sys.exit("no R3S found: not on stock firmware, ECK or ST's DFU loader")
    h = hid.device()
    h.open_path(dev["path"])
    try:
        h.write(b"\x00\x0b" + bytes(31))  # VIA id_bootloader_jump: ECK enters ST's DFU loader
    except OSError:
        pass  # the device may vanish before the write returns
    h.close()
    for _ in range(50):
        time.sleep(0.2)
        if dfu_present():
            return restore(path)
    sys.exit("the keyboard did not come back as a DFU device")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    w = sub.add_parser("wrap")
    w.add_argument("stock")
    w.add_argument("image")
    w.add_argument("out")
    sub.add_parser("install").add_argument("file")
    sub.add_parser("read").add_argument("dir")
    sub.add_parser("restore").add_argument("file")
    sub.add_parser("flash").add_argument("file")
    a = ap.parse_args()
    if a.cmd == "wrap":
        wrap(a.stock, a.image, a.out)
    elif a.cmd == "install":
        install(a.file)
    elif a.cmd == "read":
        read(a.dir)
    elif a.cmd == "flash":
        flash(a.file)
    else:
        restore(a.file)


if __name__ == "__main__":
    main()
