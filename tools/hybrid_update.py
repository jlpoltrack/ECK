#!/usr/bin/env python3
"""Update or restore the HHKB Professional Hybrid's firmware over USB.

STM32: through ST's USB DFU loader (needs dfu-util). ECK's bootloader key (or VIA's bootloader
command, sent here) starts the loader in the chip's ROM; holding the power switch while
plugging in USB does the same once the ECK boot stub is in Bank 1.
nRF52: through ECK (VIA command 30), which hands the image to ECK's nRF52 app; PFU's nRF52
bootloader installs it at the next nRF52 reset. Commands:
  nrf FILE.hfb      install the nRF52 part of a .hfb (ECK's, or PFU's to go back to stock);
                    --recovery stages it through the nRF52's recovery mode (for an app whose
                    Bluetooth side is broken; needs an app with link v6)
  stm32 FILE        write ECK (.hfb or .bin) to Bank 2 and its boot tag; refuses unless Bank 1
                    holds the boot stub (--force overrides)
  boot              write ECK's boot stub to Bank 1 (replaces PFU's updater; enables recovery);
                    the first time, saves Bank 1 and the data EEPROM to files for going back to stock
  restore-stm32     write stock Bank 2 (from a PFU .hfb) and Bank 1 (from a backup), last
  backup DIR        save the data EEPROM (keymap, macros, settings) and Bank 2 (ECK) to DIR
  restore-eeprom F  write a saved data EEPROM back (keeps the current boot tag; for a rollback, also
                    write the matching Bank 2 with stm32)
Run: uv run --with hidapi python3 tools/hybrid_update.py <command> ...
"""
import argparse
import hashlib
import os
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_hfb import compute_crc16_mcrf4xx  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STUB = os.path.join(ROOT, "stm32/boot/_build/hybrid_boot.bin")
BANK1, BANK2, SECTION = 0x08000000, 0x08010000, 0x10000
EEPROM, EEPROM_SIZE = 0x08080000, 0x1800
TAG_VALID, TAG_CRC = 0x08080360, 0x08080370
DFU_ID = "0483:df11"
NRF_MD5, NRF_IMAGE, NRF_SIZE = 0x10010, 0x10020, 0x35000  # in a .hfb
NRF_BASE, NRF_APP = 0x1000, 0x26000
VIA_SET, VIA_GET, NRF_UPDATE_ID = 0x07, 0x08, 30
UPD_START, UPD_DATA, UPD_FINISH, UPD_RESTART, UPD_RECOVERY = 1, 2, 3, 4, 5
ST_ERASING, ST_RECEIVING, ST_STAGED = 1, 2, 3
ST_ERRORS = {0x81: "image size refused", 0x82: "data out of order", 0x83: "MD5 mismatch after staging"}
CHUNK = 22


def dfu(*args):
    r = subprocess.run(["dfu-util", "-d", DFU_ID, *args], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"dfu-util {' '.join(args)} failed:\n{r.stdout}{r.stderr}")
    return r.stdout


def dfu_present():
    r = subprocess.run(["dfu-util", "-l", "-d", DFU_ID], capture_output=True, text=True)
    return "Found DFU" in r.stdout


def open_keyboard(required=True):
    try:
        import hid
    except ImportError:
        sys.exit("run with: uv run --with hidapi python3 ...")
    devs = [d for d in hid.enumerate() if d["usage_page"] == 0xFF60 and d["usage"] == 0x61
            and "HHKB" in (d["product_string"] or "")]
    if not devs:
        if required:
            sys.exit("no HHKB running ECK found on USB")
        return None
    h = hid.device()
    h.open_path(devs[0]["path"])
    return h


def via(h, cmd, payload=b""):
    req = bytes([cmd, 0, NRF_UPDATE_ID]) + payload
    h.write(b"\x00" + req + bytes(32 - len(req)))
    resp = bytes(h.read(32, 1000))
    if len(resp) < 10 or resp[0] != cmd:
        sys.exit(f"no VIA answer from the keyboard (got {resp.hex()})")
    return resp


def nrf_status(h):
    r = via(h, VIA_GET)
    return r[3], int.from_bytes(r[4:8], "little"), r[8], r[9]


def nrf_recovery(h):
    """Restart the nRF52 with PB6 high: its app starts only the link and the update receiver."""
    req = bytes([VIA_SET, 0, NRF_UPDATE_ID, UPD_RECOVERY])
    h.write(b"\x00" + req + bytes(32 - len(req)))
    resp = bytes(h.read(32, 1000))
    if not resp or resp[0] != VIA_SET:
        sys.exit("the keyboard firmware predates nRF52 recovery mode: update the STM32 first (stm32 FILE)")
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        time.sleep(0.2)
        if via(h, VIA_GET)[10]:
            print("nRF52: in recovery mode")
            return
    via(h, VIA_SET, bytes([UPD_RESTART]))
    sys.exit("the nRF52 did not report recovery mode (its app may predate it: link v6); restarted it normally")


def nrf_submit(h, payload, what):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if via(h, VIA_SET, payload)[3] == 0:
            return
        time.sleep(0.001)
    sys.exit(f"{what}: the keyboard stayed busy")


def nrf_image(path):
    data = open(path, "rb").read()
    if not path.endswith(".hfb") or len(data) != NRF_IMAGE + NRF_SIZE + 16:
        sys.exit(f"{path}: expected a Hybrid .hfb ({NRF_IMAGE + NRF_SIZE + 16} bytes)")
    image, md5 = data[NRF_IMAGE:NRF_IMAGE + NRF_SIZE], data[NRF_MD5:NRF_IMAGE]
    if hashlib.md5(image).digest() != md5:
        sys.exit(f"{path}: nRF52 MD5 does not match")
    app = NRF_APP - NRF_BASE
    sp, reset = int.from_bytes(image[app:app + 4], "little"), int.from_bytes(image[app + 4:app + 8], "little")
    if sp >> 24 != 0x20 or not NRF_APP <= reset < NRF_BASE + NRF_SIZE:
        sys.exit(f"{path}: nRF52 app vectors look wrong (SP {sp:#x}, reset {reset:#x})")
    return image, md5


def nrf_update(path, recovery=False):
    image, md5 = nrf_image(path)
    h = open_keyboard()
    try:  # every exit below restarts the nRF52 normally (and drops PB6)
        if recovery:
            nrf_recovery(h)
        nrf_submit(h, bytes([UPD_START]) + len(image).to_bytes(4, "little") + md5, "start")
        print("nRF52: Bluetooth stopped, erasing the staging area ...")
        deadline = time.monotonic() + 20
        while True:
            state, staged, pending, failed = nrf_status(h)
            if state == ST_RECEIVING and not pending:
                break
            if state in ST_ERRORS or failed or time.monotonic() > deadline:
                sys.exit(f"nRF52 did not start the update (state {state:#x}, failed {failed})")
            time.sleep(0.1)
        t0 = time.monotonic()
        for off in range(0, len(image), CHUNK):
            chunk = image[off:off + CHUNK]
            nrf_submit(h, bytes([UPD_DATA]) + off.to_bytes(4, "little") + bytes([len(chunk)]) + chunk, f"offset {off}")
            if off % (CHUNK * 500) == 0:
                state, staged, _, failed = nrf_status(h)
                if state != ST_RECEIVING or failed:
                    sys.exit(f"nRF52 update failed at {off} (state {state:#x}: {ST_ERRORS.get(state, '')}, "
                             f"link failed {failed})")
                print(f"\r  {staged * 100 // len(image):3d} %", end="", flush=True)
        nrf_submit(h, bytes([UPD_FINISH]), "finish")
        deadline = time.monotonic() + 10
        while True:
            state, staged, pending, failed = nrf_status(h)
            if state == ST_STAGED:
                break
            if state in ST_ERRORS or failed or time.monotonic() > deadline:
                sys.exit(f"\nnRF52 staging failed (state {state:#x}: {ST_ERRORS.get(state, '')}, staged {staged})")
            time.sleep(0.1)
        print(f"\r  100 %, {len(image)} bytes in {time.monotonic() - t0:.0f} s, MD5 checked")
    except (SystemExit, KeyboardInterrupt):  # record erased at START: a restart runs the current app
        try:
            via(h, VIA_SET, bytes([UPD_RESTART]))
            print("nRF52 restarted on its current app", file=sys.stderr)
        except SystemExit:
            pass
        raise
    via(h, VIA_SET, bytes([UPD_RESTART]))
    print("nRF52 restarted: PFU's bootloader installs the image (about 10 s)")
    time.sleep(15)


def enter_dfu():
    if dfu_present():
        return
    h = open_keyboard(required=False)
    if h is None:
        sys.exit("no HHKB running ECK found on USB and no DFU device: press the bootloader key "
                 "(or hold the power switch while plugging in USB)")
    try:
        h.write(b"\x00\x0b" + bytes(31))  # VIA id_bootloader_jump
    except OSError:
        pass  # the device may vanish before the write returns
    for _ in range(50):
        time.sleep(0.2)
        if dfu_present():
            return
    sys.exit("the keyboard did not come back as a DFU device")


def write(alt, addr, data, leave=False):
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
        f.write(data)
    try:
        dfu("-a", str(alt), "-s", f"{addr:#010x}" + (":leave" if leave else ""), "-D", f.name)
    finally:
        os.unlink(f.name)


def read(alt, addr, n):
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "r.bin")
        dfu("-a", str(alt), "-s", f"{addr:#010x}:{n}", "-U", path)
        return open(path, "rb").read()


def write_eeprom(data):
    """Write a data EEPROM image, keeping the chip's boot tag (it belongs to the Bank 2 now installed).
    Only changed blocks, 128 bytes at a time: the ROM loader can fail dfu-util's status poll on EEPROM
    writes that still complete, so each block is judged by reading it back."""
    cur = bytearray(read(2, EEPROM, EEPROM_SIZE))
    data = bytearray(data)
    for tag in (TAG_VALID, TAG_CRC):
        o = tag - EEPROM
        data[o:o + 4] = cur[o:o + 4]
    blk, written = 128, 0
    for off in range(0, EEPROM_SIZE, blk):
        for _ in range(3):
            if cur[off:off + blk] == data[off:off + blk]:
                break
            with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
                f.write(data[off:off + blk])
            try:
                subprocess.run(["dfu-util", "-d", DFU_ID, "-a", "2", "-t", str(blk), "-s", f"{EEPROM + off:#010x}",
                                "-D", f.name], capture_output=True, timeout=30)
            finally:
                os.unlink(f.name)
            cur[off:off + blk] = read(2, EEPROM + off, blk)
            written += 1
        else:
            sys.exit(f"data EEPROM: block at {EEPROM + off:#010x} did not verify (still in DFU mode; run again)")
    print(f"data EEPROM: written and verified ({written} block writes, boot tag kept)")


def write_verified(alt, addr, data, what):
    write(alt, addr, data)
    if read(alt, addr, len(data)) != data:
        sys.exit(f"{what}: verify failed (the chip is still in DFU mode; run the command again)")
    print(f"{what}: written and verified ({len(data)} bytes at {addr:#010x})")


def is_boot_stub(bank1):
    """ECK's stub fits in 4 KB; PFU's Bank 1 is a full 64 KB firmware."""
    return all(b == 0xFF for b in bank1[0x1000:])


def has_boot_stub(head):
    """head: the first 8 KB of Bank 1. The stub's vectors point into its first 4 KB; the rest is erased."""
    sp, reset = int.from_bytes(head[0:4], "little"), int.from_bytes(head[4:8], "little")
    return len(head) >= 0x2000 and sp >> 24 == 0x20 and BANK1 <= reset < BANK1 + 0x1000 and is_boot_stub(head)


def require_boot_stub():
    """Without the stub, a failed write leaves no USB recovery: PFU's Bank 1 cannot talk to ECK's nRF52 app."""
    if not has_boot_stub(read(0, BANK1, 0x2000)):
        leave()
        sys.exit("Bank 1 does not hold ECK's boot stub: install it first (hybrid_update.py boot), "
                 "or pass --force to write Bank 2 without USB recovery")


def stm32_section(path):
    data = open(path, "rb").read()
    if path.endswith(".hfb"):
        if len(data) < 4 + SECTION or int.from_bytes(data[2:4], "little") != compute_crc16_mcrf4xx(data[4:4 + SECTION]):
            sys.exit(f"{path}: not a valid .hfb (size or STM32 CRC)")
        sec = data[4:4 + SECTION]
    else:
        if len(data) > SECTION:
            sys.exit(f"{path}: {len(data)} bytes exceeds the 64 KB Bank 2")
        sec = data + b"\xff" * (SECTION - len(data))
    sp, reset = int.from_bytes(sec[0:4], "little"), int.from_bytes(sec[4:8], "little")
    if sp >> 24 != 0x20 or not BANK2 <= reset < BANK2 + SECTION:
        sys.exit(f"{path}: Bank 2 vectors look wrong (SP {sp:#x}, reset {reset:#x})")
    return sec


def write_bank2(sec):
    write_verified(2, TAG_CRC, bytes(4), "boot tag cleared")  # a half-written Bank 2 never boots
    write_verified(0, BANK2, sec, "Bank 2")
    crc = compute_crc16_mcrf4xx(sec)
    write_verified(2, TAG_VALID, (1).to_bytes(4, "little"), "boot flag")
    write_verified(2, TAG_CRC, crc.to_bytes(4, "little"), f"boot tag (CRC {crc:#06x})")


def leave():
    """The loader jumps to Bank 1 (ECK's stub then resets the chip). dfu-util can wait for the
    vanished device, so give it a few seconds and check the loader is gone."""
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
        f.write(read(2, TAG_VALID, 4))
    try:
        subprocess.run(["dfu-util", "-d", DFU_ID, "-a", "2", "-s", f"{TAG_VALID:#010x}:leave", "-D", f.name],
                       capture_output=True, timeout=10)
    except subprocess.TimeoutExpired:
        pass
    finally:
        os.unlink(f.name)
    time.sleep(1)
    if dfu_present():
        print("the loader is still running: unplug and replug USB to restart the keyboard")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("nrf")
    p.add_argument("file")
    p.add_argument("--recovery", action="store_true", help="stage through the nRF52's recovery mode (no Bluetooth)")
    p = sub.add_parser("stm32")
    p.add_argument("file")
    p.add_argument("--force", action="store_true", help="write even if Bank 1 lacks the boot stub")
    sub.add_parser("boot").add_argument("--stub", default=STUB)
    p = sub.add_parser("restore-stm32")
    p.add_argument("--hfb", required=True, help="PFU firmware file, e.g. HHKB800_FW_A048.hfb")
    p.add_argument("--bank1", required=True, help="backup of the original Bank 1 (64 KB)")
    sub.add_parser("dfu")
    sub.add_parser("backup").add_argument("dir")
    sub.add_parser("restore-eeprom").add_argument("file")
    a = ap.parse_args()

    if a.cmd == "nrf":
        nrf_update(a.file, a.recovery)
    elif a.cmd == "stm32":
        sec = stm32_section(a.file)
        enter_dfu()
        if not a.force:
            require_boot_stub()
        write_bank2(sec)
        leave()
    elif a.cmd == "boot":
        stub = open(a.stub, "rb").read()
        if len(stub) > 0x1000:
            sys.exit(f"{a.stub}: {len(stub)} bytes, expected the small ECK boot stub")
        enter_dfu()
        bank1 = read(0, BANK1, SECTION)
        if not is_boot_stub(bank1):  # first time: keep what going back to stock needs
            names = ("hybrid-bank1-backup.bin", "hybrid-eeprom-backup.bin")
            for name in names:
                if os.path.exists(name):
                    sys.exit(f"{name} exists: move it away first (or keep it: it may be your only stock copy)")
            eeprom = read(2, EEPROM, EEPROM_SIZE)
            for name, data in zip(names, (bank1, eeprom)):
                open(name, "wb").write(data)
            print("saved hybrid-bank1-backup.bin and hybrid-eeprom-backup.bin: keep them to go back to stock")
        write_verified(0, BANK1, stub + b"\xff" * (SECTION - len(stub)), "boot stub (Bank 1)")
        leave()
    elif a.cmd == "restore-stm32":
        bank1 = open(a.bank1, "rb").read()
        if len(bank1) != SECTION or int.from_bytes(bank1[4:8], "little") >> 16 != 0x0800:
            sys.exit(f"{a.bank1}: expected a 64 KB Bank 1 image")
        if is_boot_stub(bank1):
            sys.exit(f"{a.bank1}: this is ECK's boot stub, not PFU's Bank 1")
        sec = stm32_section(a.hfb)
        enter_dfu()
        write_bank2(sec)
        write_verified(0, BANK1, bank1, "stock Bank 1")
        print("Stock STM32 firmware restored. Unplug USB and switch the keyboard off and on.")
        leave()
    elif a.cmd == "backup":
        os.makedirs(a.dir, exist_ok=True)
        enter_dfu()
        for name, alt, addr, n in (("eeprom.bin", 2, EEPROM, EEPROM_SIZE), ("bank2.bin", 0, BANK2, SECTION)):
            data = read(alt, addr, n)
            if len(data) != n:
                sys.exit(f"{name}: read {len(data)} bytes, expected {n}")
            open(os.path.join(a.dir, name), "wb").write(data)
            print(f"saved {os.path.join(a.dir, name)} ({n} bytes)")
        leave()
    elif a.cmd == "restore-eeprom":
        data = open(a.file, "rb").read()
        if len(data) != EEPROM_SIZE:
            sys.exit(f"{a.file}: {len(data)} bytes, expected a {EEPROM_SIZE}-byte data EEPROM backup")
        enter_dfu()
        write_eeprom(data)
        leave()
    elif a.cmd == "dfu":
        enter_dfu()
        print("ST's USB DFU loader is running")
    print("done")


if __name__ == "__main__":
    main()
