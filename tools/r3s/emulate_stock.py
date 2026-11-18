#!/usr/bin/env python3
"""Run the stock R3S Bank 2 image in Unicorn with stand-in peripherals and log GPIO/ADC/timer writes.
Used to read the R3S's scan wiring (pins, mux, ADC channel) from stock firmware; see docs/deferred.md.
Usage: uv run -q --with unicorn python3 -I tools/r3s/emulate_stock.py STOCK.rfb OUT_DIR [instructions]"""
import struct
import sys

from unicorn import UC_ARCH_ARM, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MODE_MCLASS, UC_MODE_THUMB, Uc, UcError
from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_SP

rfb, out = sys.argv[1], sys.argv[2]
count = int(sys.argv[3]) if len(sys.argv) > 3 else 3_000_000
img = open(rfb, "rb").read()[4:4 + 0x10000]
eeprom = open(sys.argv[4], "rb").read() if len(sys.argv) > 4 else b"\0" * 0x1800

mu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
mu.mem_map(0x08000000, 0x20000)
mu.mem_write(0x08010000, img)
mu.mem_map(0x08080000, 0x2000)
mu.mem_write(0x08080000, eeprom)
mu.mem_map(0x1FF00000, 0x100000)
mu.mem_map(0x20000000, 0x5000)
mu.mem_map(0x40000000, 0x30000)
mu.mem_map(0x50000000, 0x2000)
mu.mem_map(0xE0000000, 0x100000)

NAMES = {0x50000000: "GPIOA", 0x50000400: "GPIOB", 0x50000800: "GPIOC", 0x50000C00: "GPIOD", 0x50001C00: "GPIOH",
         0x40012400: "ADC", 0x40000000: "TIM2", 0x40000400: "TIM3", 0x40010800: "TIM21", 0x40011400: "TIM22",
         0x40001000: "TIM6", 0x40001400: "TIM7", 0x40021000: "RCC", 0x40022000: "FLASH", 0x40007000: "PWR"}
REGS = {"GPIO": ["MODER", "OTYPER", "OSPEEDR", "PUPDR", "IDR", "ODR", "BSRR", "LCKR", "AFRL", "AFRH", "BRR"],
        "ADC": {0: "ISR", 4: "IER", 8: "CR", 0xC: "CFGR1", 0x10: "CFGR2", 0x14: "SMPR", 0x28: "CHSELR", 0x308: "CCR"}}
log = open(f"{out}/r3s_emulated_writes.log", "w")


def name(addr):
    for base, n in NAMES.items():
        if base <= addr < base + 0x400:
            off = addr - base
            if n.startswith("GPIO"):
                return f"{n}.{REGS['GPIO'][off // 4] if off // 4 < 11 else hex(off)}"
            if n == "ADC":
                return f"ADC.{REGS['ADC'].get(off, hex(off))}"
            return f"{n}+{off:#x}"
    return None


def on_write(uc, access, addr, size, value, _):
    n = name(addr)
    if n and not n.startswith("RCC") and not n.startswith("FLASH") and not n.startswith("PWR"):
        log.write(f"{uc.reg_read(UC_ARM_REG_PC):08x} {n} = {value:#x}\n")


def on_read(uc, access, addr, size, value, _):
    # Ready/status flags the HAL polls: report them set (or clear for "busy" bits).
    cur = struct.unpack("<I", uc.mem_read(addr & ~3, 4))[0]
    if addr == 0x40021000:  # RCC_CR: each RDY bit follows its ON bit (HSI, MSI, HSE, PLL)
        for on, rdy in ((0, 2), (8, 9), (16, 17), (24, 25)):
            cur = cur | (1 << rdy) if cur & (1 << on) else cur & ~(1 << rdy)
    elif addr == 0x4002100C:  # RCC_CFGR: SWS follows SW
        cur = (cur & ~0xC) | ((cur & 3) << 2)
    elif addr == 0x40021008:  # RCC_CRRCR: HSI48RDY
        cur |= 2
    elif addr == 0x40012400:  # ADC_ISR: ADRDY, EOC, EOS, EOCAL
        cur |= 0x80F
    elif addr == 0x40012408:  # ADC_CR: calibration/start/stop/disable finished
        cur &= ~((1 << 31) | (1 << 4) | (1 << 2) | (1 << 1))
    elif addr == 0x40007004:  # PWR_CSR: VOSF clear
        cur &= ~(1 << 4)
    elif addr == 0x40022018:  # FLASH_SR: not busy, ready
        cur = (cur & ~1) | 8
    else:
        return
    uc.mem_write(addr & ~3, struct.pack("<I", cur))


mu.hook_add(UC_HOOK_MEM_WRITE, on_write, begin=0x40000000, end=0x5FFFFFFF)
mu.hook_add(UC_HOOK_MEM_READ, on_read, begin=0x40000000, end=0x5FFFFFFF)
mu.reg_write(UC_ARM_REG_SP, struct.unpack_from("<I", img, 0)[0])
reset = struct.unpack_from("<I", img, 4)[0]
try:
    mu.emu_start(reset, 0xFFFFFFFF, count=count)
except UcError as e:
    print(f"stopped: {e} at {mu.reg_read(UC_ARM_REG_PC):#010x}")
print(f"pc {mu.reg_read(UC_ARM_REG_PC):#010x}")
open(f"{out}/r3s_emulated_ram.bin", "wb").write(mu.mem_read(0x20000000, 0x5000))
log.close()
