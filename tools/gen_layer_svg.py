#!/usr/bin/env python3
"""Draws the HHKB default layers and the keymap-independent chords (docs/layers.svg, from
stm32/boards/hhkb/default_keymap.h and hhkb_positions.h), the DIP switches (docs/dip-switches.svg),
the same two for JIS (docs/layers-jis.svg, docs/dip-switches-jis.svg) and the LED patterns
(docs/led-patterns.svg).
usage: gen_layer_svg.py [out_dir]"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
out_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "docs"

# ---- geometry: key widths per row (flat keymap order), x offset of each row ----
ANSI_ROWS = [([1.0] * 15, 0.0),
             ([1.5] + [1.0] * 12 + [1.5], 0.0),
             ([1.75] + [1.0] * 11 + [2.25], 0.0),
             ([2.25] + [1.0] * 10 + [1.75, 1.0], 0.0),
             ([1.0, 1.5, 6.0, 1.5, 1.0], 1.5)]
JIS_ROWS = [([1.0] * 15, 0.0),
            ([1.5] + [1.0] * 12 + [1.5], 0.0),  # Enter (L-shaped) reaches into the next row
            ([1.75] + [1.0] * 12, 0.0),
            ([2.0] + [1.0] * 13, 0.0),
            ([1.0] * 5 + [3.0] + [1.0] * 7, 0.0)]


def layout_keys(rows):
    keys = []  # (flat index, x, y, w)
    for y, (widths, x) in enumerate(rows):
        for w in widths:
            keys.append((len(keys), x, float(y), w))
            x += w
    return keys


# ---- keymap: the JIS block (#ifdef HHKB_JIS) and the ANSI one (#else) ----
src = (ROOT / "stm32/boards/hhkb/default_keymap.h").read_text()
jis_src, ansi_src = src.split("#ifdef HHKB_JIS", 1)[1].split("\n#else\n", 1)
KEY = r"MO\(\w+\)|K_\w+|___"


def ansi_layers():
    layers = {}
    for name, block in re.findall(r"^    \[(\w+)\] = \{(.*?)\n    \}", ansi_src, re.S | re.M):
        block = re.sub(r"//.*", "", block)
        layers[name] = re.findall(KEY, block)
    return layers


def jis_layers():
    def macro(name):
        body = re.search(rf"#define {name}(?:\(\w+\))? \\\n(.*?)\n\n", jis_src, re.S)[1]
        return re.findall(KEY, body.replace("MO(fn)", "MO(HHKB)"))
    return {"BASE": macro("JIS_BASE_LAYER"), "HHKB": macro("JIS_FN_LAYER")}


def positions(jis):
    """Flat indices of the keys the chords and DIP switches find by position (hhkb_positions.h)."""
    text = (ROOT / "stm32/boards/hhkb/hhkb_positions.h").read_text()
    jis_part, ansi_part = text.split("#ifdef HHKB_JIS", 1)[1].split("#else", 1)
    return {k: int(v) for k, v in re.findall(r"POS_(\w+) = (\d+)", jis_part if jis else ansi_part)}


NAMES = {"ESC": "Esc", "MINS": "-", "EQL": "=", "BKSL": "\\", "GRAV": "`", "TAB": "Tab", "LBRC": "[",
         "RBRC": "]", "BSPC": "Delete", "LCTL": "Ctrl", "SCLN": ";", "QUOT": "'", "ENT": "Return", "LSFT": "Shift",
         "COMM": ",", "DOT": ".", "SLSH": "/", "RSFT": "Shift", "LALT": "Alt", "LGUI": "◇", "SPC": "",
         "RGUI": "◇", "RALT": "Alt", "POWER": "Power", "INS": "Ins", "DEL": "Del", "CAPS": "Caps",
         "PSCR": "PrtSc", "SLCK": "ScrLk", "PAUS": "Pause", "UP": "↑", "DOWN": "↓", "LEFT": "←", "RGHT": "→",
         "VOLU": "Vol+", "VOLD": "Vol-", "MUTE": "Mute", "PAST": "*", "PSLS": "/", "PPLS": "+", "PMNS": "-",
         "PENT": "Enter", "HOME": "Home", "END": "End", "PGUP": "PgUp", "PGDN": "PgDn"}
JIS_NAMES = {"INT3": "¥", "EQL": "^", "LBRC": "@", "RBRC": "[", "NUHS": "]", "QUOT": ":", "INT1": "ろ",
             "GRAV": "半角\n全角", "INT5": "無変換", "INT4": "変換", "INT2": "かな", "BSPC": "BS", "ENT": "Enter",
             "EJCT": "Eject", "RCTL": "Ctrl"}
GROUP_OF = {"POWER": "system", "CAPS": "system", "INS": "system", "DEL": "system", "BSPC": "system",
            "PSCR": "system", "SLCK": "system", "PAUS": "system", "VOLU": "media", "VOLD": "media",
            "MUTE": "media", "PAST": "keypad", "PSLS": "keypad", "PPLS": "keypad", "PMNS": "keypad",
            "PENT": "keypad", "UP": "nav", "DOWN": "nav", "LEFT": "nav", "RGHT": "nav", "HOME": "nav",
            "END": "nav", "PGUP": "nav", "PGDN": "nav", "EJCT": "media", "RSFT": "mod", "RGUI": "mod",
            "RCTL": "mod"}


def label(code, jis=False):
    if code.startswith("MO("):
        return "Fn"
    name = code[2:]
    if jis and name in JIS_NAMES:
        return JIS_NAMES[name]
    return NAMES.get(name, name)


def group(code):
    if code.startswith("MO("):
        return "fn"
    name = code[2:]
    if re.fullmatch(r"F\d+", name):
        return "fkey"
    return GROUP_OF.get(name, "key")


def chords(pos):
    """Chords by key position, whatever the keymap holds: flat index -> (label, group)."""
    c = {pos["1"]: ("BT 1", "link"), pos["1"] + 1: ("BT 2", "link"), pos["1"] + 2: ("BT 3", "link"),
         pos["4"]: ("BT 4", "link"), pos["0"]: ("USB", "link"), pos["Q"]: ("Pairing", "pair"),
         pos["X"]: ("Cancel", "pair"), pos["Z"]: ("Forget\nall", "pair"), pos["DEL"]: ("Forget", "pair"),
         pos["W"]: ("Win/\nMac", "mode"), pos["V"]: ("VIA\nmode", "mode"), pos["CTRL"]: ("Ctrl", "hold"),
         pos["FN"]: ("Fn", "fn")}
    if "FN_LEFT" in pos:  # JIS: either Fn key
        c[pos["FN_LEFT"]] = ("Fn", "fn")
    return c


GROUPS = {  # fill, text, legend
    "fkey": ("#dde3ea", "#2b3640", "F-keys"), "media": ("#cfe8d4", "#1d4d2a", "Media"),
    "nav": ("#cfe0f5", "#173a63", "Navigation"), "keypad": ("#f8e4c4", "#6b4510", "Keypad"),
    "system": ("#e3d9f3", "#3d2766", "System / editing"), "key": ("#dde3ea", "#2b3640", "Other key"),
    "link": ("#cfe0f5", "#173a63", "Connection (Hybrid)"), "pair": ("#f8d4e4", "#6e1f45", "Pairing (Hybrid)"),
    "mode": ("#d3f0ec", "#14524a", "Mode toggle"), "mod": ("#ece4d4", "#4a3c22", "Modifier"),
}
LEGEND = {1: ["fkey", "media", "nav", "keypad", "system"], 2: ["link", "pair", "mode"]}

U, PAD, LH = 44, 2, 56  # px per key unit, key gap, panel title height
LINE = 17  # text line pitch
W = int(15 * U) + 40  # both layouts are 15 units wide
TITLES = ["Layer 0: base. Fn is held for layer 1",
          "Layer 1: Fn held. Blank keys fall through to layer 0",
          "Chords: hold Fn + Ctrl, then the key (never sent to the host)"]
def notes(delete):
    return [["Layers 2 and 3 (System 2) start as copies of 0 and 1. DIP switches can change some keys (see the DIP switch picture)."],
            [],
            ["Hybrid only:",
             "• Pair a slot: Fn + Ctrl + Q (blue blinks twice a second), then Fn + Ctrl + 1-4.",
             "• Cancel pairing: Fn + Ctrl + X.",
             f"• Forget a slot: Fn + Ctrl + Q, then hold Fn + Ctrl + {delete} and press 1-4.",
             f"• Forget all: Fn + Ctrl + Q, then hold Fn + Ctrl + {delete} and press Z."]]
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
# JIS (dip_modes.c, PFU's JIS manuals): SW1 alone picks the mode, no HHK mode
JIS_MODES = [("OFF", "Win", "No extras"),
             ("ON", "Mac", "半角/全角 is Caps Lock, 無変換 is 英数, 変換 is かな, かな is right ◇ (Command)")]
JIS_DIPS = [("SW2", "Control is Ctrl, left Fn is Fn", "Control is 英数, left Fn is Ctrl (also for the Fn + Ctrl chords)"),
            ("SW3", "BS key is Backspace\nFn + BS is Delete", "BS key is Delete\nFn + BS is Backspace"),
            ("SW4", "Arrow keys are arrows", "↑ is Shift, ↓ is ◇, ← is Delete, → is Ctrl"),
            ("SW5", "Left ◇ and left Alt in their stock places", "Left ◇ and left Alt swapped"),
            ("SW6", "Hybrid: power saving (keys don't wake the computer), auto power-off on batteries\nType-S: normal",
             "Hybrid: keys wake the computer, no auto power-off\nType-S: Bank 1 recovery")]
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


def key_shape(px, py, w, enter, fill):
    if not enter:
        return (f'<rect x="{px:.1f}" y="{py:.1f}" width="{w * U - 2 * PAD:.1f}" height="{U - 2 * PAD:.1f}" '
                f'rx="4" fill="{fill}" stroke="#c8c8c8"/>')
    # JIS Enter: 1.5 units wide in its row, 1.25 in the row below
    x0, x1, xr, y1, y2 = px, px + 0.25 * U, px + w * U - 2 * PAD, py + U - 2 * PAD, py + 2 * U - 2 * PAD
    return (f'<path d="M{x0:.1f},{py:.1f} H{xr:.1f} V{y2:.1f} H{x1:.1f} V{y1:.1f} H{x0:.1f} Z" '
            f'fill="{fill}" stroke="#c8c8c8" stroke-linejoin="round"/>')


def draw_layout(jis):
    keys = layout_keys(JIS_ROWS if jis else ANSI_ROWS)
    layers = jis_layers() if jis else ansi_layers()
    assert all(len(v) == len(keys) for v in layers.values()), "keymap size does not match the layout"
    pos = positions(jis)
    chord = chords(pos)
    enter = 28 if jis else None
    note = notes("BS" if jis else "Delete")
    legend = {1: LEGEND[1] + (["mod"] if jis else []), 2: LEGEND[2]}
    suffix = "-jis" if jis else ""
    y = 0
    for n in range(3):
        oy = y
        text(20, oy + 28, TITLES[n], size=17, ink="#222", bold=True)
        for idx, x, y0, w in keys:
            base = layers["BASE"][idx]
            fill, ink, lab = "#f2f2f2", "#333", label(base, jis)
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
                    lab = "Bksp" if code == "K_BSPC" else label(code, jis)
                    fill, ink = GROUPS[group(code)][0], GROUPS[group(code)][1]
            else:
                fill, ink = "#fafafa", "#bbb"
                if idx in chord:
                    lab, g = chord[idx]
                    fill, ink = ("#444", "#fff") if g in ("fn", "hold") else GROUPS[g][:2]
            px, py = 20 + x * U + PAD, oy + LH + y0 * U + PAD
            svg.append(key_shape(px, py, w, idx == enter, fill))
            lines = lab.split("\n")  # two-line labels for words too wide for one key
            longest = max(len(t) for t in lines)
            fs = 12 if longest <= 6 else 10 if longest <= 8 else 9
            for i, t in enumerate(lines):
                ty = py + (U - 2 * PAD) / 2 + 4 + (i - (len(lines) - 1) / 2) * (fs + 2)
                svg.append(f'<text x="{px + (w * U - 2 * PAD) / 2:.1f}" y="{ty:.1f}" '
                           f'font-size="{fs}" text-anchor="middle" fill="{ink}">{esc(t)}</text>')
        ly = oy + LH + len(ANSI_ROWS) * U + 10
        if n in legend:
            lx = 20
            for g in legend[n]:
                fill, _, name = GROUPS[g]
                svg.append(f'<rect x="{lx}" y="{ly}" width="14" height="14" rx="3" fill="{fill}" stroke="#c8c8c8"/>')
                text(lx + 20, ly + 11, name, ink="#222")
                lx += 34 + 7 * len(name)
            ly += 26
        for i, line in enumerate(note[n]):
            text(20, ly + 11 + LINE * i, line)
        y = ly + LINE * len(note[n]) + 4
    save(f"layers{suffix}.svg", y + 10)

    text(20, 28, "DIP switches (back of the keyboard, read at power-on)", size=17, ink="#222", bold=True)
    if jis:
        text(20, 56, "SW1 selects the mode. Fn + Ctrl + W switches Mac ↔ Win until power-off.")
        y = table(84, [(20, 6, "SW1"), (70, 16, "Mode"), (190, 76, "What changes")], [list(m) for m in JIS_MODES])
        dips = JIS_DIPS
    else:
        text(20, 56, "SW1 and SW2 select the mode. Fn + Ctrl + W switches Mac ↔ Win (or HHK) until power-off.")
        y = table(84, [(20, 6, "SW1"), (70, 6, "SW2"), (120, 16, "Mode"), (240, 70, "What changes")],
                  [list(m) for m in MODES])
        dips = DIPS
    y += 26
    y = table(y, [(20, 6, "Switch"), (90, 42, "OFF"), (390, 42, "ON")], [list(d) for d in dips])
    y += 22
    text(20, y, "The switches change only keys that still hold their default keycodes, so Vial / VIA remaps are kept.")
    save(f"dip-switches{suffix}.svg", y + 16)


draw_layout(jis=False)
draw_layout(jis=True)

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
                  ("Host forgot keyboard", "5 quick blinks: the slot's host rejected its pairing; pair the slot again", blinks(5, 200)),
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
