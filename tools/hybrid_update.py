#!/usr/bin/env python3
"""Update or restore the HHKB Professional Hybrid's firmware over USB.

STM32: through ST's USB DFU loader (needs dfu-util). ECK's bootloader key (or VIA's bootloader
command, sent here) starts the loader in the chip's ROM; holding the power switch while
plugging in USB does the same once the ECK boot stub is in Bank 1.
nRF52: through ECK (VIA command 30), which hands the image to ECK's nRF52 app; PFU's nRF52
bootloader installs it at the next nRF52 reset. Commands:
  nrf FILE.hfb      install the nRF52 part of a .hfb (ECK's, or PFU's to go back to stock);
                    --recovery stages it through the nRF52's recovery mode (for an app whose
                    Bluetooth side is broken; needs an app with link v6); refuses an image of the
                    other layout (ANSI or JIS; --force overrides); --bluetooth sends it over
                    Bluetooth (only changed 4 KB pages; typing pauses meanwhile)
  stm32 FILE        write ECK (.hfb or .bin) to Bank 2 and its boot tag; refuses unless Bank 1
                    holds the boot stub and the image's layout (ANSI or JIS) matches the
                    keyboard's (--force overrides)
  boot              write ECK's boot stub to Bank 1 (replaces PFU's updater; enables recovery);
                    the first time, saves Bank 1 and the data EEPROM to files for going back to stock
  restore-stm32     write stock Bank 2 (from a PFU .hfb) and Bank 1 (from a backup), last
  backup DIR        save the data EEPROM (keymap, macros, settings) and Bank 2 (ECK) to DIR
  restore-eeprom F  write a saved data EEPROM back (keeps the current boot tag; for a rollback, also
                    write the matching Bank 2 with stm32)
  dfu               start ST's USB DFU loader and leave it running
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
from via_hid import hid_module, open_keyboard, send, via as via_request  # noqa: E402

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


def via(h, cmd, payload=b""):
    return via_request(h, bytes([cmd, 0, NRF_UPDATE_ID]) + payload, min_len=10)


def nrf_status(h):
    r = via(h, VIA_GET)
    return r[3], int.from_bytes(r[4:8], "little"), r[8], r[9]


def nrf_recovery(h):
    """Restart the nRF52 with PB6 high: its app starts only the link and the update receiver."""
    resp = send(h, [VIA_SET, 0, NRF_UPDATE_ID, UPD_RECOVERY])
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


def nrf_update(path, recovery=False, force=False):
    image, md5 = nrf_image(path)
    if not force:  # the JIS nRF52 app reports the JIS IDs
        check_layout(hfb_is_jis(open(path, "rb").read()))
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


def stm32_update_ble(path, force=False, install=True):
    """Stages ECK's STM32 image on the nRF52 over Bluetooth; the STM32 pulls it over SPI and checks it
    against the .hfb's Bank 2 CRC, then restarts; the boot stub (from v0.9.0) copies Bank 1 to Bank 2."""
    sec = stm32_section(path)
    crc = compute_crc16_mcrf4xx(sec)
    image = sec.rstrip(b"\xff")
    image += b"\xff" * (-len(image) % 4)
    if len(image) > STM_STAGE_MAX:
        sys.exit(f"{path}: the STM32 image is {len(image)} bytes; Bluetooth staging holds {STM_STAGE_MAX}")
    u = BleUpdate()
    if not force and path.endswith(".hfb") and u.jis != hfb_is_jis(open(path, "rb").read()):
        u.h.close()
        sys.exit("the image's layout (ANSI or JIS) differs from the keyboard's: pass --force to write it anyway")
    pages = (len(image) + PAGE - 1) // PAGE
    t0 = time.monotonic()
    try:
        print(f"STM32: {len(image)} bytes ({pages} pages); erasing the nRF52's staging area (typing paused) ...")
        u.ask(BU_BEGIN, len(image).to_bytes(4, "little") + hashlib.md5(image).digest() + b"\x01" +
              crc.to_bytes(2, "little"))
        u.wait(BU_READY, timeout=120, poll=0.3)
        t1 = time.monotonic()
        step = u.size - 6
        for p in range(pages):
            for off in range(p * PAGE, min((p + 1) * PAGE, len(image)), step):
                chunk = image[off:min(off + step, (p + 1) * PAGE, len(image))]
                u.send(BU_DATA, off.to_bytes(3, "little") + bytes([len(chunk)]) + chunk)
            u.send(BU_COMMIT, p.to_bytes(2, "little") + b"\x00")
            u.wait(BU_READY, done=p + 1, timeout=10, poll=0.05)
            print(f"\r  page {p + 1}/{pages}", end="", flush=True)
        print(f"\r  {pages} pages in {time.monotonic() - t1:.1f} s; the STM32 pulls and checks it ...")
        u.ask(BU_FINISH)
        t2 = time.monotonic()
        u.wait(BU_STM_DONE, timeout=60, poll=0.2)
        print(f"STM32 staged and checked the image in Bank 1 (CRC {crc:#06x}) in {time.monotonic() - t2:.1f} s; "
              f"{time.monotonic() - t0:.0f} s in all")
        if not install:
            u.send(BU_ABORT)  # left staged, not installed: typing resumes
            return
        u.send(BU_INSTALL)  # the STM32 restarts; its boot stub copies Bank 1 to Bank 2 (~6 s)
        print("the STM32 restarts and its boot stub installs it ...")
    except BaseException:  # any failure, so typing resumes
        try:
            u.send(BU_ABORT)
        except Exception:
            pass
        raise
    finally:
        u.h.close()
    if install:
        time.sleep(5)
        end, got = time.monotonic() + 40, None
        while time.monotonic() < end and got != crc:
            try:
                u = BleUpdate(quiet=True)
                try:
                    got = u.stm32_crc()
                finally:
                    u.h.close()
            except (OSError, SystemExit):
                pass
            if got != crc:
                time.sleep(1)
        if got != crc:
            sys.exit(f"the keyboard runs firmware with CRC {got if got is None else hex(got)}, not the image's "
                     f"{crc:#06x}: the install did not happen (USB still updates it)")
        print(f"installed: the keyboard runs the new firmware (CRC {crc:#06x}; {time.monotonic() - t0:.0f} s in all)")


# ---------- nRF52 over Bluetooth (nrf52/main.c upd_cmd) ----------

BU_CMD, BU_STATUS, BU_BEGIN, BU_CRC, BU_DATA, BU_COMMIT, BU_FINISH, BU_INSTALL, BU_ABORT, BU_COPY = (
    0xE6, 1, 2, 3, 4, 5, 6, 7, 8, 9)
BU_READY, BU_STAGED, BU_ERROR, BU_STM_READY, BU_STM_DONE = 2, 5, 6, 7, 8
BU_ERRORS = {1: "image size refused", 2: "erase failed", 3: "write failed", 4: "MD5 mismatch",
             5: "out of order", 6: "busy", 7: "the STM32 rejected the image",
             8: "the boot stub is too old to install it: update the boot stub over USB (hybrid_update.py boot)",
             9: "not an ECK Bluetooth image (vectors)", 10: "the image is for another model or layout",
             11: "not allowed: press Fn+Ctrl+U on the keyboard (allows updates for 5 minutes), then run this again"}
STM_STAGE_MAX = 0xEF80  # stm32/hybrid/hybrid_ble.c
PAGE = 0x1000


class BleUpdate:
    """The keyboard's update channel over Bluetooth: the 244-byte update service when the host has
    discovered it (paired since it was added), else the 32-byte VIA service."""

    def __init__(self, quiet=False):
        hid = hid_module()
        devs = [d for d in hid.enumerate(0xCAFE) if d.get("bus_type") == 2 and d["usage_page"] == 0xFF60]
        upd = [d for d in devs if d["usage"] == 0x62]
        raw = [d for d in devs if d["usage"] == 0x61]
        if not upd and not raw:
            sys.exit("no HHKB on Bluetooth (pair it, or select its slot)")
        dev, self.size = (upd[0], 244) if upd else (raw[0], 32)
        self.jis = dev["product_id"] == 0x4012  # the nRF52's PnP ID, as the USB product ID
        self.h = hid.device()
        self.h.open_path(dev["path"])
        if upd and raw and not self.answers():  # a host that can't write 244-byte reports (ATT MTU)
            self.h.close()
            dev, self.size = raw[0], 32
            self.h = hid.device()
            self.h.open_path(dev["path"])
        if not quiet:
            print(f"Bluetooth: {dev['product_string']}, {'update' if self.size == 244 else 'VIA'} channel "
                  f"({self.size}-byte reports)")

    def answers(self):
        try:
            self.send(BU_STATUS)
        except OSError:
            return False
        end = time.monotonic() + 2
        while time.monotonic() < end:
            r = bytes(self.h.read(32, 300))
            if r[:2] == bytes([BU_CMD, BU_STATUS]):
                return True
        return False

    def stm32_crc(self):
        """The running STM32's Bank 2 CRC as it told the nRF52 at start-up, or None."""
        r = self.ask(BU_STATUS)
        return int.from_bytes(r[13:15], "little") if len(r) >= 15 and r[12] else None

    def send(self, op, payload=b""):
        msg = bytes([BU_CMD, op]) + payload
        self.h.write(b"\x00" + msg + bytes(self.size - len(msg)))

    def ask(self, op, payload=b"", timeout=3.0):
        self.send(op, payload)
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            r = bytes(self.h.read(32, 500))
            if len(r) >= 6 and r[0] == BU_CMD and r[1] == op:
                return r
        sys.exit(f"no reply to update command {op}")

    def status(self):
        r = self.ask(BU_STATUS)
        return r[2], r[3], int.from_bytes(r[4:6], "little")

    def wait(self, want, done=None, timeout=60.0, poll=0.02):
        end = time.monotonic() + timeout
        while True:
            state, err, d = self.status()
            if state == BU_ERROR:
                sys.exit(f"\nupdate failed: {BU_ERRORS.get(err, err)} (page {d})")
            if state == want and (done is None or d >= done):
                return
            if time.monotonic() > end:
                sys.exit(f"\nupdate timed out (state {state}, page {d})")
            time.sleep(poll)


def nrf_update_ble(path, force=False):
    import zlib
    image, md5 = nrf_image(path)
    pages = (len(image) + PAGE - 1) // PAGE
    u = BleUpdate()
    jis = hfb_is_jis(open(path, "rb").read())
    if not force and u.jis != jis:
        u.h.close()
        sys.exit(f"the keyboard is {'JIS' if u.jis else 'ANSI'} but the image is for the "
                 f"{'JIS' if jis else 'ANSI'} layout: pass --force to write it anyway")
    t0 = time.monotonic()
    try:
        r = u.ask(BU_STATUS)
        if r[6] != 1:
            sys.exit(f"the keyboard's update protocol is {r[6]}, this tool speaks 1")
        same = set()
        for first in range(0, pages, 6):
            n = min(6, pages - first)
            r = u.ask(BU_CRC, bytes([first, n]))
            for i in range(n):
                p = first + i
                live = int.from_bytes(r[8 + 4 * i:12 + 4 * i], "little")
                if (p + 1) * PAGE <= len(image) and live == zlib.crc32(image[p * PAGE:(p + 1) * PAGE]):
                    same.add(p)
        print(f"nRF52: {pages - len(same)} of {pages} pages changed; erasing the staging area (typing paused) ...")
        u.ask(BU_BEGIN, len(image).to_bytes(4, "little") + md5 + b"\x00\x00\x00")  # target nRF52
        u.wait(BU_READY, timeout=120, poll=0.3)
        t1 = time.monotonic()
        step = u.size - 6
        p = 0
        while p < pages:
            if p in same:  # a run of unchanged pages: the keyboard copies them itself
                n = 1
                while p + n in same:
                    n += 1
                u.send(BU_COPY, p.to_bytes(2, "little") + n.to_bytes(2, "little"))
                u.wait(BU_READY, done=p + n, timeout=10 + n, poll=0.1)
            else:
                for off in range(p * PAGE, min((p + 1) * PAGE, len(image)), step):
                    chunk = image[off:min(off + step, (p + 1) * PAGE, len(image))]
                    u.send(BU_DATA, off.to_bytes(3, "little") + bytes([len(chunk)]) + chunk)
                u.send(BU_COMMIT, p.to_bytes(2, "little") + b"\x00")
                u.wait(BU_READY, done=p + 1, timeout=10, poll=0.05)
                n = 1
            p += n
            print(f"\r  page {p}/{pages}", end="", flush=True)
        print(f"\r  {pages} pages in {time.monotonic() - t1:.1f} s; checking the MD5 ...")
        u.ask(BU_FINISH)
        u.wait(BU_STAGED, timeout=10)
        u.send(BU_INSTALL)  # it restarts within ~1.5 s, often before its reply arrives
    except BaseException:  # any failure, so typing resumes
        try:
            u.send(BU_ABORT)  # typing resumes; nothing is installed without a record
        except Exception:
            pass
        raise
    finally:
        u.h.close()
    print(f"staged and checked in {time.monotonic() - t0:.0f} s; the nRF52 restarts and PFU's bootloader installs it")
    hid = hid_module()
    end = time.monotonic() + 90
    gone = False
    while time.monotonic() < end:  # Bluetooth drops for the install, then reconnects
        up = any(d.get("bus_type") == 2 for d in hid.enumerate(0xCAFE))
        if not up:
            gone = True
        elif gone:
            print(f"Bluetooth back after {time.monotonic() - t0:.0f} s in all")
            return
        time.sleep(0.5)
    print("Bluetooth not back within 90 s: check the keyboard (USB still updates it)", file=sys.stderr)


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


STUB_MAX = 0xC00  # the stub's page is 0xFF-padded above this; Bluetooth updates stage from 0x1000


def is_boot_stub(bank1):
    """ECK's stub is under 3 KB, 0xFF-padded to 4 KB; PFU's Bank 1 is a full 64 KB firmware."""
    return len(bank1) >= 0x1000 and all(b == 0xFF for b in bank1[STUB_MAX:0x1000])


def has_boot_stub(head):
    """head: the start of Bank 1. The stub's vectors point into its first 4 KB, padded with 0xFF."""
    sp, reset = int.from_bytes(head[0:4], "little"), int.from_bytes(head[4:8], "little")
    return sp >> 24 == 0x20 and BANK1 <= reset < BANK1 + 0x1000 and is_boot_stub(head)


STAGE_HDR, STAGE_MAGIC = 0x0800FF80, 0x55534345  # stm32/hybrid/bank1_stage.h


def clear_stage_header():
    """A Bluetooth install marker the boot stub didn't act on (recovery): the USB write must win.
    Only under ECK's stub: PFU's Bank 1 may hold code there."""
    if has_boot_stub(read(0, BANK1, 0x1000)) and any(read(0, STAGE_HDR, 16)):
        write_verified(0, STAGE_HDR, bytes(128), "Bluetooth install marker cleared")


def refresh_boot_stub(path=STUB):
    """An ECK stub without Bluetooth installs (no STAGE_MAGIC) gets the built one, its 4 KB page only;
    never the reverse (a stale local build must not downgrade it)."""
    if not os.path.exists(path):
        return
    stub = open(path, "rb").read()
    page = stub + b"\xff" * (0x1000 - len(stub))
    head = read(0, BANK1, 0x1000)
    magic = STAGE_MAGIC.to_bytes(4, "little")
    installs = lambda b: any(b[i:i + 4] == magic for i in range(0, STUB_MAX, 4))
    if len(stub) <= STUB_MAX and has_boot_stub(head) and not installs(head) and installs(page):
        write_verified(0, BANK1, page, "boot stub updated (Bank 1, 4 KB)")


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


def hfb_is_jis(data):
    """The Hybrid .hfb trailer (last 16 bytes) starts with the model: AHJX01 is the Hybrid JIS."""
    return data[-16:-10] == b"AHJX01"


def check_layout(jis):
    """Refuse an ANSI image for a JIS keyboard or the reverse; the product string names the layout."""
    h = open_keyboard(required=False)
    if h is None:
        return  # already in DFU: nothing running to compare with
    try:
        product = h.get_product_string() or ""
    finally:
        h.close()
    if ("JIS" in product) != jis:
        sys.exit(f"the keyboard is {product!r} but the image is for the {'JIS' if jis else 'ANSI'} "
                 "layout: pass --force to write it anyway")


def check_bank2_layout(jis):
    """In the DFU loader: refuse when Bank 2 holds ECK for the other layout (as web/eck.js bank2Layout)."""
    bank2 = read(0, BANK2, SECTION)
    if b"HHKB Professional Hybrid" in bank2:
        installed, where = b"HHKB Professional Hybrid JIS" in bank2, "Bank 2 holds ECK"
    else:  # no ECK image: ECK's Vial record sits at 0x1608 (ANSI) or 0x1644 (JIS) in the EEPROM
        rec = lambda off: read(2, 0x08080000 + off, 8) == b"CMITVIAL"
        ansi, jis_rec = rec(0x1608), rec(0x1644)
        if ansi == jis_rec:
            return  # nothing tells the layout
        installed, where = jis_rec, "the EEPROM holds ECK settings"
    if installed != jis:
        leave()
        sys.exit(f"{where} for the {'JIS' if installed else 'ANSI'} layout but the image is for "
                 f"{'JIS' if jis else 'ANSI'}: pass --force to write it anyway")


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
    p.add_argument("--bluetooth", action="store_true", help="over Bluetooth instead of USB (needs firmware with Bluetooth updates)")
    p.add_argument("--force", action="store_true", help="write even if the layout differs")
    p = sub.add_parser("stm32")
    p.add_argument("file")
    p.add_argument("--force", action="store_true", help="write even if Bank 1 lacks the boot stub or the layout differs")
    p.add_argument("--bluetooth", action="store_true",
                   help="over Bluetooth (needs the boot stub with Bluetooth installs)")
    p.add_argument("--stage-only", action="store_true", help="with --bluetooth: stage and check, don't install")
    sub.add_parser("boot").add_argument("--stub", default=STUB)
    p = sub.add_parser("restore-stm32")
    p.add_argument("--hfb", required=True, help="PFU firmware file, e.g. HHKB800_FW_A048.hfb")
    p.add_argument("--bank1", required=True, help="backup of the original Bank 1 (64 KB)")
    sub.add_parser("dfu")
    sub.add_parser("backup").add_argument("dir")
    sub.add_parser("restore-eeprom").add_argument("file")
    a = ap.parse_args()

    if a.cmd == "nrf" and a.bluetooth:
        nrf_update_ble(a.file, a.force)
    elif a.cmd == "nrf":
        nrf_update(a.file, a.recovery, a.force)
    elif a.cmd == "stm32" and a.bluetooth:
        stm32_update_ble(a.file, a.force, not a.stage_only)
    elif a.cmd == "stm32":
        sec = stm32_section(a.file)
        if not a.force:
            check_layout(b"Hybrid JIS" in sec)
        enter_dfu()
        if not a.force:
            require_boot_stub()
            check_bank2_layout(b"Hybrid JIS" in sec)
        clear_stage_header()
        refresh_boot_stub()
        write_bank2(sec)
        leave()
    elif a.cmd == "boot":
        stub = open(a.stub, "rb").read()
        if len(stub) > STUB_MAX:
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
        for name, alt, addr, n in (("eeprom.bin", 2, EEPROM, EEPROM_SIZE), ("bank1.bin", 0, BANK1, SECTION),
                                   ("bank2.bin", 0, BANK2, SECTION)):
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
