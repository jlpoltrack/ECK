#!/usr/bin/env python3
"""Draws how the Realforce R3S indicator LEDs are wired and driven (docs/r3s-leds.svg), from the
pins in stm32/boards/r3s/r3s_leds.c. usage: gen_r3s_led_svg.py [out.svg]"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
out = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "docs/r3s-leds.svg"

SELECTS = [("PB6", "Num Lock"), ("PB5", "Caps Lock"), ("PB4", "Scroll Lock"), ("PB3", "Simple Mode")]
COLOURS = [("PB9", "red", "#d93a3a"), ("PB8", "green", "#2f9e44"), ("PB7", "blue", "#2f6fd6")]
W = 700
svg = []


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;")


def text(x, y, s, size=12, ink="#444", bold=False, anchor="start"):
    weight = ' font-weight="bold"' if bold else ""
    svg.append(f'<text x="{x}" y="{y}" font-size="{size}"{weight} fill="{ink}" text-anchor="{anchor}">{esc(s)}</text>')


def line(x1, y1, x2, y2, ink="#555", width=2):
    svg.append(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{ink}" stroke-width="{width}"/>')


text(20, 28, "R3S Indicator LEDs", size=17, ink="#222", bold=True)
text(20, 50, "Each RGB LED lights a colour when its select line AND that colour line are high (both active high).")
text(20, 66, "Drawn from the firmware's pin use; the driver transistors between the chip and the LEDs aren't known.")

# ---- wiring: select lines across, colour lines down, an LED die at each crossing ----
MCU_X, MCU_W, TOP = 20, 120, 100
ROW_Y = [TOP + 46 + 64 * i for i in range(4)]
COL_X = [330 + 90 * j for j in range(3)]
mcu_h = ROW_Y[-1] - TOP + 50
svg.append(f'<rect x="{MCU_X}" y="{TOP}" width="{MCU_W}" height="{mcu_h}" rx="6" fill="#eef1f5" stroke="#8a94a3"/>')
text(MCU_X + MCU_W / 2, TOP + 22, "STM32L072", size=13, ink="#222", bold=True, anchor="middle")
BUS_Y = ROW_Y[-1] + 74  # colour lines leave the chip's bottom and run up the columns
for i, (pin, name) in enumerate(SELECTS):
    y = ROW_Y[i]
    text(MCU_X + MCU_W - 8, y + 4, pin, anchor="end", ink="#222", bold=True)
    line(MCU_X + MCU_W, y, COL_X[-1] + 40, y)
    text(COL_X[-1] + 50, y + 4, name, ink="#222", bold=True)
    text(MCU_X + MCU_W + 10, y - 6, "select", size=10, ink="#888")
for j, (pin, name, ink) in enumerate(COLOURS):
    x = COL_X[j]
    px = MCU_X + 22 + 38 * j
    text(px, TOP + mcu_h - 10, pin, size=11, ink=ink, bold=True, anchor="middle")
    line(px, TOP + mcu_h, px, BUS_Y + 10 * j, ink)
    line(px, BUS_Y + 10 * j, x, BUS_Y + 10 * j, ink)
    line(x, BUS_Y + 10 * j, x, ROW_Y[0] - 22, ink)
    text(x, ROW_Y[0] - 28, name, size=11, ink=ink, bold=True, anchor="middle")
    for i in range(4):  # LED die: branch down from the select line, then right into the colour line
        y = ROW_Y[i]
        bx, dy = x - 44, y + 22
        sx = bx - 14  # down from the select line left of the diode, then right into its anode
        svg.append(f'<polyline points="{sx},{y} {sx},{dy} {bx},{dy}" fill="none" stroke="#555" stroke-width="2"/>')
        svg.append(f'<circle cx="{sx}" cy="{y}" r="3" fill="#555"/>')
        svg.append(f'<polygon points="{bx},{dy - 8} {bx},{dy + 8} {bx + 14},{dy}" fill="{ink}" opacity="0.85"/>')
        line(bx + 14, dy - 8, bx + 14, dy + 8, ink)
        line(bx + 14, dy, x, dy, ink)
        svg.append(f'<circle cx="{x}" cy="{dy}" r="3" fill="{ink}"/>')
y = BUS_Y + 50

# ---- multiplexing: one LED per 1 ms slot ----
text(20, y, "Multiplexing (ECK): one LED at a time, so each has its own colour", size=14, ink="#222", bold=True)
y += 20
text(20, y, "TIM21 runs at 1 kHz. Each 1 ms slot lights the next LED (Num, Caps, Scroll, Simple Mode) in its colour;")
text(20, y + 16, "a compare ends it early for brightness (20 %: lit 0.8 ms of every 4 ms). Example: Caps red, Simple Mode blue.")
y += 40
TX, SLOT = 130, 34  # timeline x, px per ms
example = {1: "#d93a3a", 3: "#2f6fd6"}
for i, (pin, name) in enumerate(SELECTS):
    yy = y + 26 * i
    text(TX - 10, yy + 13, name, anchor="end", ink="#222")
    svg.append(f'<rect x="{TX}" y="{yy + 7}" width="{SLOT * 16}" height="4" rx="2" fill="#dde1e6"/>')
    for k in range(16):
        if k % 4 == i and i in example:
            svg.append(f'<rect x="{TX + SLOT * k:.1f}" y="{yy + 1}" width="{SLOT * 0.8:.1f}" height="16" rx="2" fill="{example[i]}"/>')
yy = y + 26 * 4 + 6
for k in range(0, 17, 4):
    text(TX + SLOT * k, yy + 6, f"{k} ms", size=10, ink="#888", anchor="middle")
y = yy + 30
text(20, y, "Confirmation blinks (VIA mode) light Simple Mode alone in blue. Before a firmware update the chip stops")
text(20, y + 16, "multiplexing and leaves Simple Mode lit magenta (red + blue) while ST's loader runs.")
y += 34
head = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{y}" font-family="Helvetica, Arial, sans-serif">',
        '<rect width="100%" height="100%" fill="#ffffff"/>']
out.write_text("\n".join(head + [s for s in svg if s] + ["</svg>"]) + "\n")
print(f"wrote {out}")
