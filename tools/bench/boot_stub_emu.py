#!/usr/bin/env python3
"""Runs the Hybrid boot stub (stm32/boot/_build/hybrid_boot.elf's binary) in Unicorn against a model of
the STM32L0 flash controller (RM0376 ch. 3): erased flash reads 0, programming needs erased words,
half-page programming aborts on a flash fetch and faults on a flash data read, locks and keys.
Checks the Bluetooth install (stm32/hybrid/bank1_stage.h), bad images, recovery and power loss.

    uv run --with unicorn python3 tools/bench/boot_stub_emu.py [build/hybrid/hhkb_ec_hybrid.hfb]
"""
import os
import random
import struct
import sys

from unicorn import (UC_ARCH_ARM, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MODE_MCLASS,
                     UC_MODE_THUMB, Uc, UcError)
from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_SP

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from make_hfb import compute_crc16_mcrf4xx as crc16  # noqa: E402

STUB = os.path.join(ROOT, "stm32/boot/_build/hybrid_boot.bin")
FLASH0, FLASH_N = 0x08000000, 0x20000
BANK2, SECTION = 0x08010000, 0x10000
EEPROM0, EEPROM_N = 0x08080000, 0x1800
TAG_VALID, TAG_CRC = 0x08080360, 0x08080370
STAGE_BASE, STAGE_MAX, STAGE_HDR, STAGE_MAGIC = 0x08001000, 0xEF80, 0x0800FF80, 0x55534345
FLASH_REGS, RCC, GPIOC, SCB = 0x40022000, 0x40021000, 0x50000800, 0xE000ED00
PELOCK, PRGLOCK, PROG, ERASE, FPRG = 1, 2, 1 << 3, 1 << 9, 1 << 10
BSY, EOP, NOTZERO, FWW, WRP, PGA = 1, 2, 1 << 16, 1 << 17, 1 << 8, 1 << 9


class PowerLoss(Exception):
    pass


class Chip:
    """Flash, EEPROM and the few registers the stub touches. ops_left: power fails after that many NVM ops."""

    def __init__(self, flash, eeprom, recovery=False):
        self.flash, self.eeprom, self.recovery = bytearray(flash), bytearray(eeprom), recovery
        self.ops = 0
        self.ops_left = None
        self.violations = []

    def boot(self, max_steps=60_000_000):
        uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        uc.mem_map(FLASH0, FLASH_N)
        uc.mem_map(EEPROM0, 0x2000)
        uc.mem_map(0x20000000, 0x5000)
        uc.mem_map(0x40000000, 0x30000)  # APB and AHB peripherals (DFU entry resets several)
        uc.mem_map(0x50000000, 0x1000)
        uc.mem_map(0xE000E000, 0x1000)
        uc.mem_map(0x1FF00000, 0x10000)  # system ROM: entering it = the DFU loader
        for a, v in ((0x1FF00000, 0x200014D0), (0x1FF01A80, 0x25014C06), (0x1FF01A98, 0xFB16F7FE),
                     (0x1FF00364, 0xFF62F7FF), (0x1FF00368, 0xFECEF7FF), (0x1FF01A9C, 0x1FF01A81)):
            uc.mem_write(a, struct.pack("<I", v))  # stm32/hybrid/rom_dfu.h's ROM fingerprint
        uc.mem_write(FLASH0, bytes(self.flash))
        uc.mem_write(EEPROM0, bytes(self.eeprom))
        self.uc, self.pecr, self.sr, self.pekey, self.prgkey = uc, PELOCK | PRGLOCK | 4, 0, 0, 0
        self.half, self.half_base, self.fix, self.result, self.cfgr = [], None, [], None, 0
        uc.hook_add(UC_HOOK_CODE, self.on_code)
        uc.hook_add(UC_HOOK_MEM_WRITE, self.on_write)
        uc.hook_add(UC_HOOK_MEM_READ, self.on_read)
        sp, pc = struct.unpack_from("<II", self.flash, 0)
        uc.reg_write(UC_ARM_REG_SP, sp)
        try:
            uc.emu_start(pc, 0xFFFFFFFF, count=max_steps)
        except PowerLoss:
            self.result = "power lost"
        except UcError as e:
            if self.result is None:
                self.result = f"fault: {e} at {uc.reg_read(UC_ARM_REG_PC):#x}"
        if self.result is None:
            self.result = "hung"
        return self.result

    # ---- hooks ----
    def on_code(self, uc, addr, size, _):
        for a, v in self.fix:  # undo the plain store a flash write hook modelled
            uc.mem_write(a, v)
            if FLASH0 <= a < FLASH0 + FLASH_N:
                self.flash[a - FLASH0:a - FLASH0 + len(v)] = v
        self.fix = []
        if self.half and FLASH0 <= addr < FLASH0 + FLASH_N:
            self.violations.append(f"fetch at {addr:#x} inside a half-page write (FWWERR)")
            self.half, self.sr = [], self.sr | FWW
        if BANK2 <= addr < BANK2 + SECTION:
            self.result = "app"
            uc.emu_stop()
        elif 0x1FF00000 <= addr < 0x1FF10000:
            self.result = "dfu"
            uc.emu_stop()

    def nvm_op(self):
        self.ops += 1
        if self.ops_left is not None:
            self.ops_left -= 1
            if self.ops_left < 0:
                raise PowerLoss()

    def on_read(self, uc, access, addr, size, value, _):
        if self.half and FLASH0 <= addr < FLASH0 + FLASH_N:
            self.violations.append(f"data read at {addr:#x} inside a half-page write (hard fault)")
        reg = None
        if addr == FLASH_REGS + 0x04:
            reg = self.pecr
        elif addr == FLASH_REGS + 0x18:
            reg = self.sr | 0x8  # READY
        elif addr == RCC + 0x00:
            reg = 0x200 | 0x4 | 0x2  # MSIRDY, HSIRDY
        elif addr == RCC + 0x0C:
            reg = self.cfgr | ((self.cfgr & 3) << 2)
        elif addr == GPIOC + 0x10:
            reg = 0 if self.recovery else 0xFFFF
        if reg is not None:
            uc.mem_write(addr, struct.pack("<I", reg))

    def on_write(self, uc, access, addr, size, value, _):
        if FLASH0 <= addr < FLASH0 + FLASH_N:
            return self.flash_write(uc, addr, size, value)
        if EEPROM0 <= addr < EEPROM0 + EEPROM_N:
            if self.pecr & PELOCK:
                self.violations.append(f"EEPROM write at {addr:#x} while locked")
            else:
                self.nvm_op()
                self.eeprom[addr - EEPROM0:addr - EEPROM0 + size] = value.to_bytes(size, "little")
            return
        v = value & 0xFFFFFFFF
        if addr == FLASH_REGS + 0x04:
            keep = self.pecr & (PELOCK | PRGLOCK | 4)
            self.pecr = (v & ~(PELOCK | PRGLOCK | 4)) | keep
            if v & PELOCK:
                self.pecr = PELOCK | PRGLOCK | 4  # PELOCK relocks all and clears modes
            if v & PRGLOCK:
                self.pecr |= PRGLOCK
        elif addr == FLASH_REGS + 0x0C:
            seq = [0x89ABCDEF, 0x02030405]
            if not self.pecr & PELOCK or v != seq[self.pekey]:
                self.violations.append(f"bad PEKEYR write {v:#x} (hard fault)")
            self.pekey += 1
            if self.pekey == 2:
                self.pecr &= ~PELOCK
                self.pekey = 0
        elif addr == FLASH_REGS + 0x10:
            seq = [0x8C9DAEBF, 0x13141516]
            if self.pecr & PELOCK or not self.pecr & PRGLOCK or v != seq[self.prgkey]:
                self.violations.append(f"bad PRGKEYR write {v:#x} (hard fault)")
            self.prgkey += 1
            if self.prgkey == 2:
                self.pecr &= ~PRGLOCK
                self.prgkey = 0
        elif addr == FLASH_REGS + 0x18:
            self.sr &= ~v
        elif addr == RCC + 0x0C:
            self.cfgr = v
        elif addr == SCB + 0x0C and v >> 16 == 0x05FA and v & 4:
            self.result = "reset"
            uc.emu_stop()

    def flash_write(self, uc, addr, size, value):
        off = addr - FLASH0
        self.fix.append((addr, bytes(self.flash[off:off + size])))  # nothing lands unless modelled below
        if off < 0x1000:
            self.violations.append(f"write to the stub's page at {addr:#x}")
        if self.pecr & (PELOCK | PRGLOCK):
            self.sr |= WRP
            return
        if size != 4:
            self.violations.append(f"{size}-byte flash write at {addr:#x}")
            return
        if self.pecr & ERASE and self.pecr & PROG:
            self.nvm_op()
            page = off & ~127
            self.fix[-1] = (FLASH0 + page, bytes(128))
            self.sr |= EOP
        elif self.pecr & FPRG and self.pecr & PROG:
            if not self.half:
                if off & 63:
                    self.sr |= PGA
                    return
                self.half_base = off
            self.half.append(value & 0xFFFFFFFF)
            self.fix.pop()
            self.fix.append((addr, bytes(self.flash[off:off + 4])))
            if len(self.half) == 16:
                base, words, self.half = self.half_base, self.half, []
                self.nvm_op()
                if any(self.flash[base:base + 64]):
                    self.sr |= NOTZERO
                    return
                self.fix[-1] = (FLASH0 + base, struct.pack("<16I", *words))
                self.sr |= EOP
        else:
            self.nvm_op()
            if any(self.flash[off:off + 4]):
                self.sr |= NOTZERO
                return
            self.fix[-1] = (addr, value.to_bytes(4, "little"))
            self.sr |= EOP


def image_from(path):
    data = open(path, "rb").read()
    sec = data[4:4 + SECTION] if path.endswith(".hfb") else data.ljust(SECTION, b"\xff")
    img = sec.rstrip(b"\xff")
    return img + b"\xff" * (-len(img) % 4), crc16(sec), sec


def make_chip(stub, old_sec, staged=None, header=None, recovery=False):
    flash = bytearray(FLASH_N)  # erased = 0
    flash[:len(stub)] = stub
    flash[len(stub):0x1000] = b"\xff" * (0x1000 - len(stub))
    flash[SECTION:] = old_sec
    eeprom = bytearray(EEPROM_N)
    struct.pack_into("<I", eeprom, TAG_VALID - EEPROM0, 1)
    struct.pack_into("<I", eeprom, TAG_CRC - EEPROM0, crc16(old_sec))
    if staged:
        o = STAGE_BASE - FLASH0
        flash[o:o + len(staged)] = staged
        flash[o + len(staged):o + (len(staged) + 63) // 64 * 64] = b"\xff" * ((-len(staged)) % 64)
    if header:
        struct.pack_into("<4I", flash, STAGE_HDR - FLASH0, *header)
    return Chip(flash, eeprom, recovery)


def header_for(size, crc):
    return (STAGE_MAGIC, size, crc, ~(STAGE_MAGIC ^ size ^ (crc << 16)) & 0xFFFFFFFF)


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build/hybrid/hhkb_ec_hybrid.hfb")
    stub = open(STUB, "rb").read()
    new_img, new_crc, new_sec = image_from(path)
    old = bytearray(new_sec)
    old[0x2000:0x2100] = bytes(range(256))  # a different, valid "installed" firmware
    old_sec, old_crc = bytes(old), crc16(bytes(old))
    fails = []

    def check(name, ok, chip, detail=""):
        bad = chip.violations
        status = "PASS" if ok and not bad else "FAIL"
        if status == "FAIL":
            fails.append(name)
        print(f"{status}  {name}: {chip.result}, {chip.ops} NVM ops{detail}" + (f"; {bad[:3]}" if bad else ""))

    def installed(c, sec, crc):
        tags = struct.unpack_from("<I", c.eeprom, TAG_VALID - EEPROM0)[0] == 1 and \
            struct.unpack_from("<I", c.eeprom, TAG_CRC - EEPROM0)[0] & 0xFFFF == crc
        return bytes(c.flash[SECTION:]) == sec and tags

    def header_gone(c):
        return not any(c.flash[STAGE_HDR - FLASH0:STAGE_HDR - FLASH0 + 128])

    def stub_intact(c):
        return bytes(c.flash[:len(stub)]) == stub

    c = make_chip(stub, old_sec)
    r = c.boot()
    check("no header: boots the installed app", r == "app" and c.ops == 0 and installed(c, old_sec, old_crc), c)

    c = make_chip(stub, old_sec, new_img, header_for(len(new_img), new_crc))
    r = c.boot()
    ok = r == "app" and installed(c, new_sec, new_crc) and header_gone(c) and stub_intact(c)
    check("staged image installs", ok, c, f" ({len(new_img)} bytes, ~{c.ops * 3.2 / 1000:.1f} s of NVM time)")
    full_ops = c.ops

    bad = bytearray(new_img)
    bad[100] ^= 1
    c = make_chip(stub, old_sec, bytes(bad), header_for(len(new_img), new_crc))
    r = c.boot()
    check("corrupt staging: dropped, old app boots",
          r == "app" and installed(c, old_sec, old_crc) and header_gone(c), c)

    c = make_chip(stub, old_sec, new_img, (STAGE_MAGIC, len(new_img), new_crc, 0x1234))
    r = c.boot()
    check("bad header check: dropped, old app boots", r == "app" and installed(c, old_sec, old_crc) and header_gone(c), c)

    c = make_chip(stub, old_sec, new_img, header_for(STAGE_MAX + 4, new_crc))
    r = c.boot()
    check("oversized header: dropped", r == "app" and installed(c, old_sec, old_crc) and header_gone(c), c)

    c = make_chip(stub, old_sec, new_img, header_for(len(new_img), new_crc), recovery=True)
    r = c.boot()
    check("recovery with a header: DFU, nothing written", r == "dfu" and c.ops == 0, c)

    rng = random.Random(1)
    points = sorted({1, 2, 3, full_ops - 3, full_ops - 2, full_ops - 1} | {rng.randrange(1, full_ops) for _ in range(20)})
    for n in points:
        c = make_chip(stub, old_sec, new_img, header_for(len(new_img), new_crc))
        c.ops_left = n
        c.boot()
        cut = c.ops
        c.ops_left = None
        r = c.boot()  # power back
        ok = r == "app" and installed(c, new_sec, new_crc) and header_gone(c) and stub_intact(c)
        check(f"power lost after {cut} NVM ops, then restored", ok, c)

    print(f"\n{'all passed' if not fails else f'{len(fails)} FAILED'}")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
