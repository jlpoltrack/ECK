#!/usr/bin/env python3
"""Draws the HHKB default layers and the keymap-independent chords (docs/layers.svg, from
stm32/boards/hhkb/default_keymap.h), the DIP switches (docs/dip-switches.svg) and the LED
patterns (docs/led-patterns.svg).
usage: gen_layer_svg.py [out_dir]"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
out_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "docs"

# ---- HHKB geometry: key widths per row (flat keymap order), x offset of each row ----
ROWS = [([1.0] * 15, 0.0),
        ([1.5] + [1.0] * 12 + [1.5], 0.0),
        ([1.75] + [1.0] * 11 + [2.25], 0.0),
        ([2.25] + [1.0] * 10 + [1.75, 1.0], 0.0),
        ([1.0, 1.5, 6.0, 1.5, 1.0], 1.5)]
keys = []  # (flat index, x, y, w)
for y, (widths, x) in enumerate(ROWS):
    for w in widths:
        keys.append((len(keys), x, float(y), w))
        x += w

# ---- keymap ----
src = (ROOT / "stm32/boards/hhkb/default_keymap.h").read_text()
layers = {}
for name, block in re.findall(r"^    \[(\w+)\] = \{(.*?)\n    \}", src, re.S | re.M):
    block = re.sub(r"//.*", "", block)
    layers[name] = re.findall(r"MO\(\w+\)|K_\w+|___", block)
assert all(len(v) == len(keys) for v in layers.values()), "keymap size does not match the layout"

NAMES = {"ESC": "Esc", "MINS": "-", "EQL": "=", "BKSL": "\\", "GRAV": "`", "TAB": "Tab", "LBRC": "[",
         "RBRC": "]", "BSPC": "Delete", "LCTL": "Ctrl", "SCLN": ";", "QUOT": "'", "ENT": "Return", "LSFT": "Shift",
         "COMM": ",", "DOT": ".", "SLSH": "/", "RSFT": "Shift", "LALT": "Alt", "LGUI": "◇", "SPC": "",
         "RGUI": "◇", "RALT": "Alt", "POWER": "Power", "INS": "Ins", "DEL": "Del", "CAPS": "Caps",
         "PSCR": "PrtSc", "SLCK": "ScrLk", "PAUS": "Pause", "UP": "↑", "DOWN": "↓", "LEFT": "←", "RGHT": "→",
         "VOLU": "Vol+", "VOLD": "Vol-", "MUTE": "Mute", "PAST": "*", "PSLS": "/", "PPLS": "+", "PMNS": "-",
         "PENT": "Enter", "HOME": "Home", "END": "End", "PGUP": "PgUp", "PGDN": "PgDn"}
GROUP_OF = {"POWER": "system", "CAPS": "system", "INS": "system", "DEL": "system", "BSPC": "system",
            "PSCR": "system", "SLCK": "system", "PAUS": "system", "VOLU": "media", "VOLD": "media",
            "MUTE": "media", "PAST": "keypad", "PSLS": "keypad", "PPLS": "keypad", "PMNS": "keypad",
            "PENT": "keypad", "UP": "nav", "DOWN": "nav", "LEFT": "nav", "RGHT": "nav", "HOME": "nav",
            "END": "nav", "PGUP": "nav", "PGDN": "nav"}


def label(code):
    if code.startswith("MO("):
        return "Fn"
    name = code[2:]
    return NAMES.get(name, name)


def group(code):
    if code.startswith("MO("):
        return "fn"
    name = code[2:]
    if re.fullmatch(r"F\d+", name):
        return "fkey"
    return GROUP_OF.get(name, "key")


# Chords by key position (hhkb_positions.h), whatever the keymap holds: flat index -> (label, group).
POS = {"1": 1, "2": 2, "3": 3, "4": 4, "0": 10, "Q": 16, "W": 17, "DEL": 28, "Z": 43, "X": 44, "V": 46,
       "CTRL": 29, "FN": 54}
CHORDS = {POS["1"]: ("BT 1", "link"), POS["2"]: ("BT 2", "link"), POS["3"]: ("BT 3", "link"),
          POS["4"]: ("BT 4", "link"), POS["0"]: ("USB", "link"), POS["Q"]: ("Pairing", "pair"),
          POS["X"]: ("Cancel", "pair"), POS["Z"]: ("Forget\nall", "pair"), POS["DEL"]: ("Forget", "pair"),
          POS["W"]: ("Win/\nMac", "mode"), POS["V"]: ("VIA\nmode", "mode"), POS["CTRL"]: ("Ctrl", "hold"),
          POS["FN"]: ("Fn", "fn")}

GROUPS = {  # fill, text, legend
    "fkey": ("#dde3ea", "#2b3640", "F-keys"), "media": ("#cfe8d4", "#1d4d2a", "Media"),
    "nav": ("#cfe0f5", "#173a63", "Navigation"), "keypad": ("#f8e4c4", "#6b4510", "Keypad"),
    "system": ("#e3d9f3", "#3d2766", "System / editing"), "key": ("#dde3ea", "#2b3640", "Other key"),
    "link": ("#cfe0f5", "#173a63", "Connection (Hybrid)"), "pair": ("#f8d4e4", "#6e1f45", "Pairing (Hybrid)"),
    "mode": ("#d3f0ec", "#14524a", "Mode toggle"),
}
LEGEND = {1: ["fkey", "media", "nav", "keypad", "system"], 2: ["link", "pair", "mode"]}

U, PAD, LH = 44, 2, 56  # px per key unit, key gap, panel title height
LINE = 17  # text line pitch
kw = max(x + w for _, x, _, w in keys)
W = int(kw * U) + 40
TITLES = ["Layer 0: base. Fn is held for layer 1",
          "Layer 1: Fn held. Blank keys fall through to layer 0",
          "Chords: hold Fn + Ctrl, then the key (never sent to the host)"]
NOTES = [["Layers 2 and 3 (System 2) start as copies of 0 and 1. DIP switches can change some keys (see the DIP switch picture)."],
         [],
         ["Hybrid only:",
          "• Pair a slot: Fn + Ctrl + Q (blue blinks twice a second), then Fn + Ctrl + 1-4.",
          "• Cancel pairing: Fn + Ctrl + X.",
          "• Forget a slot: Fn + Ctrl + Q, then hold Fn + Ctrl + Delete and press 1-4.",
          "• Forget all: Fn + Ctrl + Q, then hold Fn + Ctrl + Delete and press Z."]]
# DIP switches (docs/config.md), read at power-on: (switch, OFF, ON)
MODES = [("OFF", "OFF", "HHK (default)", "Fn + right ◇ sends Stop"),
         ("ON", "OFF", "Win", "No extras"),
         ("OFF", "ON", "Mac", "Delete is Backspace, Fn + Delete is forward Delete (SW3 ignored)"),
         ("ON", "ON", "HHK", "As OFF/OFF")]
DIPS = [("SW3", "Delete key is Delete\nFn + Delete is Backspace", "Delete key is Backspace\nFn + Delete is Delete"),
        ("SW4", "Left ◇ is ◇", "Left ◇ is Fn"),
        ("SW5", "Alt and ◇ in their stock places", "Alt and ◇ swapped on both sides"),
        ("SW6", "Hybrid: power saving (keys don't wake the computer), auto power-off on batteries\nClassic: normal",
         "Hybrid: keys wake the computer, no auto power-off\nClassic: Bank 1 recovery")]
svg = []


def save(name, height):
    head = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{height}" '
            'font-family="Helvetica, Arial, sans-serif">', '<rect width="100%" height="100%" fill="#ffffff"/>']
    (out_dir / name).write_text("\n".join(head + svg + ["</svg>"]) + "\n")
    print(f"wrote {out_dir / name}")
    svg.clear()


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;")


def text(x, y, s, size=12, ink="#555", bold=False):
    weight = ' font-weight="bold"' if bold else ""
    svg.append(f'<text x="{x}" y="{y}" font-size="{size}"{weight} fill="{ink}">{esc(s)}</text>')


def wrap(s, chars):
    if "\n" in s:  # explicit line breaks, each part wrapped on its own
        return [line for part in s.split("\n") for line in wrap(part, chars)]
    lines, cur = [], ""
    for word in s.split():
        if cur and len(cur) + 1 + len(word) > chars:
            lines.append(cur)
            cur = word
        else:
            cur = f"{cur} {word}".strip()
    return lines + [cur]


def table(y, cols, rows):
    """cols: [(x, width_chars, header)]; rows: lists of cell strings. Returns the y below the table."""
    for x, _, head in cols:
        text(x, y, head, ink="#222", bold=True)
    y += 10
    for i, row in enumerate(rows):
        cells = [wrap(c, cols[j][1]) for j, c in enumerate(row)]
        h = 16 * max(len(c) for c in cells) + 12
        if i % 2 == 0:
            svg.append(f'<rect x="14" y="{y}" width="{W - 28}" height="{h}" rx="3" fill="#f4f6f8"/>')
        for j, c in enumerate(cells):
            for k, line in enumerate(c):
                text(cols[j][0], y + 20 + 16 * k, line, ink="#222" if j == 0 else "#444", bold=j == 0)
        y += h
    return y


y = 0
for n in range(3):
    oy = y
    text(20, oy + 28, TITLES[n], size=17, ink="#222", bold=True)
    for idx, x, y0, w in keys:
        base = layers["BASE"][idx]
        fill, ink, lab = "#f2f2f2", "#333", label(base)
        if n == 0:
            if group(base) == "fn":
                fill, ink = "#444", "#fff"
        elif n == 1:
            code = layers["HHKB"][idx]
            if code == "___":
                fill, ink = "#fafafa", "#bbb"
                if group(base) == "fn":
                    fill, ink = "#444", "#fff"
            else:
                lab = "Bksp" if code == "K_BSPC" else label(code)
                fill, ink = GROUPS[group(code)][0], GROUPS[group(code)][1]
        else:
            fill, ink = "#fafafa", "#bbb"
            if idx in CHORDS:
                lab, g = CHORDS[idx]
                fill, ink = ("#444", "#fff") if g in ("fn", "hold") else GROUPS[g][:2]
        px, py = 20 + x * U + PAD, oy + LH + y0 * U + PAD
        svg.append(f'<rect x="{px:.1f}" y="{py:.1f}" width="{w * U - 2 * PAD:.1f}" height="{U - 2 * PAD:.1f}" '
                   f'rx="4" fill="{fill}" stroke="#c8c8c8"/>')
        lines = lab.split("\n")  # two-line labels for words too wide for one key
        longest = max(len(t) for t in lines)
        fs = 12 if longest <= 6 else 10 if longest <= 8 else 9
        for i, t in enumerate(lines):
            ty = py + (U - 2 * PAD) / 2 + 4 + (i - (len(lines) - 1) / 2) * (fs + 2)
            svg.append(f'<text x="{px + (w * U - 2 * PAD) / 2:.1f}" y="{ty:.1f}" '
                       f'font-size="{fs}" text-anchor="middle" fill="{ink}">{esc(t)}</text>')
    ly = oy + LH + len(ROWS) * U + 10
    if n in LEGEND:
        lx = 20
        for g in LEGEND[n]:
            fill, _, name = GROUPS[g]
            svg.append(f'<rect x="{lx}" y="{ly}" width="14" height="14" rx="3" fill="{fill}" stroke="#c8c8c8"/>')
            text(lx + 20, ly + 11, name, ink="#222")
            lx += 34 + 7 * len(name)
        ly += 26
    for i, line in enumerate(NOTES[n]):
        text(20, ly + 11 + LINE * i, line)
    y = ly + LINE * len(NOTES[n]) + 4
save("layers.svg", y + 10)

text(20, 28, "DIP switches (back of the keyboard, read at power-on)", size=17, ink="#222", bold=True)
text(20, 56, "SW1 and SW2 select the mode. Fn + Ctrl + W switches Mac ↔ Win (or HHK) until power-off.")
y = table(84, [(20, 6, "SW1"), (70, 6, "SW2"), (120, 16, "Mode"), (240, 70, "What changes")],
          [list(m) for m in MODES])
y += 26
y = table(y, [(20, 6, "Switch"), (90, 42, "OFF"), (390, 42, "ON")], [list(d) for d in DIPS])
y += 22
text(20, y, "The switches change only keys that still hold their default keycodes, so Vial / VIA remaps are kept.")
save("dip-switches.svg", y + 16)

# ---- LED patterns (hybrid_power.c, hybrid_ble.c, indicators_system.c): (title, detail, lit spans in ms) ----
BLUE, ORANGE = ("#2f6fd6", "Blue"), ("#e8891c", "Orange")


def blinks(n, half):
    return [(2 * i * half, (2 * i + 1) * half) for i in range(n)]


SPAN = 3000  # ms drawn per row
LEDS = [(BLUE, [("Turning on", "Lit while the power switch is held, then 1 s", [(0, 1000)]),
                ("Bluetooth slot 1-4", "Fn + Ctrl + 1-4, or a light press of the switch: 1-4 blinks (4 shown)", blinks(4, 300)),
                ("USB", "Fn + Ctrl + 0, or a light press of the switch on USB", [(0, 1000)]),
                ("Pairing standby", "After Fn + Ctrl + Q, up to 10 min", blinks(6, 250)),
                ("Pairing", "After Fn + Ctrl + 1-4 in standby, or while the host shows a passkey", blinks(12, 125)),
                ("Pairing cancelled", "Fn + Ctrl + X", [(0, 1000)]),
                ("Win / Mac", "Fn + Ctrl + W: 1 blink Win (or HHK), 2 blinks Mac (Mac shown)", blinks(2, 300)),
                ("VIA mode", "Fn + Ctrl + V: 2 blinks into VIA mode, 1 back to Vial (VIA shown)", blinks(2, 300))]),
        (ORANGE, [("Turning off", "Power switch held 2 s", [(0, 1000)]),
                  ("Pairings forgotten", "Fn + Ctrl + Q, then Fn + Ctrl + Delete + 1-4 or Z: always 3 blinks", blinks(3, 300)),
                  ("Batteries low", "One blink every ~33 s (on batteries, using Bluetooth)", blinks(1, 600)),
                  ("Replace batteries", "Two blinks every ~16.5 s (on batteries, using Bluetooth)", blinks(2, 600)),
                  ("Batteries empty", "At power-on, then the keyboard turns off", [(0, 2000)]),
                  ("Firmware update", "Solid while the main chip is being updated", [(0, SPAN)]),
                  ("Bluetooth update", "Two blinks a second while the Bluetooth chip updates or recovers", blinks(6, 250))])]
TX, TW = 470, 200  # timeline x and width (px for SPAN ms)
text(20, 28, "LED Patterns", size=17, ink="#222", bold=True)
y = 48
for (colour, name), rows in LEDS:
    y += 14
    text(20, y + 12, name, size=14, ink="#222", bold=True)
    for t in range(0, SPAN + 1, 1000):
        x = TX + TW * t / SPAN
        text(x - 6, y + 12, f"{t // 1000} s", size=10, ink="#888")
    y += 22
    for i, (title, detail, spans) in enumerate(rows):
        h = 40 + 13 * (len(wrap(detail, 70)) - 1)
        if i % 2 == 0:
            svg.append(f'<rect x="14" y="{y}" width="{W - 28}" height="{h}" rx="3" fill="#f4f6f8"/>')
        text(20, y + 17, title, ink="#222", bold=True)
        for k, line in enumerate(wrap(detail, 70)):
            text(20, y + 32 + 13 * k, line, size=11)
        svg.append(f'<rect x="{TX}" y="{y + 18}" width="{TW}" height="4" rx="2" fill="#dde1e6"/>')
        for a, b in spans:
            svg.append(f'<rect x="{TX + TW * a / SPAN:.1f}" y="{y + 12}" width="{TW * (b - a) / SPAN:.1f}" '
                       f'height="16" rx="3" fill="{colour}"/>')
        y += h
save("led-patterns.svg", y + 14)
