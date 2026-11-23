#!/usr/bin/env python3
"""Write the HHKB JIS VIA definition (stm32/via/via_jis.json) from the ANSI one (stm32/via/via.json,
for the settings menu and custom keycodes) and boards/hhkb/board_config.h (JIS matrix positions).
Run after changing either: python3 tools/make_jis_via_json.py"""
import json
import os
import re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
ANSI = os.path.join(ROOT, "stm32/via/via.json")
CONFIG = os.path.join(ROOT, "stm32/boards/hhkb/board_config.h")
OUT = os.path.join(ROOT, "stm32/via/via_jis.json")
ENTER = "Enter"

# Physical rows in LAYOUT order (board_config.h): (legend, width)
ROWS = [
    [("Esc", 1), ("!\n1", 1), ("\"\n2", 1), ("#\n3", 1), ("$\n4", 1), ("%\n5", 1), ("&\n6", 1), ("'\n7", 1),
     ("(\n8", 1), (")\n9", 1), ("0", 1), ("=\n-", 1), ("~\n^", 1), ("|\n¥", 1), ("BS", 1)],
    [("Tab", 1.5), ("Q", 1), ("W", 1), ("E", 1), ("R", 1), ("T", 1), ("Y", 1), ("U", 1), ("I", 1), ("O", 1),
     ("P", 1), ("`\n@", 1), ("{\n[", 1), (ENTER, 1.5)],
    [("Ctrl", 1.75), ("A", 1), ("S", 1), ("D", 1), ("F", 1), ("G", 1), ("H", 1), ("J", 1), ("K", 1), ("L", 1),
     ("+\n;", 1), ("*\n:", 1), ("}\n]", 1)],
    [("Shift", 2), ("Z", 1), ("X", 1), ("C", 1), ("V", 1), ("B", 1), ("N", 1), ("M", 1), ("<\n,", 1), (">\n.", 1),
     ("?\n/", 1), ("_\n\\", 1), ("↑", 1), ("Shift", 1)],
    [("Fn", 1), ("半角/全角", 1), ("◇", 1), ("Alt", 1), ("無変換", 1), ("Space", 3), ("変換", 1), ("かな", 1),
     ("Alt", 1), ("Fn", 1), ("←", 1), ("↓", 1), ("→", 1)],
]


def layout_positions():
    text = open(CONFIG).read()
    jis = text[text.index("#ifdef HHKB_JIS\n/* --- JIS matrix"):]
    body = jis[jis.index("#define LAYOUT {"):jis.index("#define LAYOUT_DEFAULT")]
    return [(int(r), int(c)) for r, c in re.findall(r"\{(\d+), (\d+)\}", body)]


def keymap_rows(positions):
    rows, i = [], 0
    for row in ROWS:
        out = []
        for legend, w in row:
            if legend == ENTER:  # ISO-shaped: 1.5 wide on this row, 1.25 below it
                out.append({"x": 0.25, "w": 1.25, "h": 2, "w2": 1.5, "h2": 1, "x2": -0.25})
            elif w != 1:
                out.append({"w": w})
            r, c = positions[i]
            out.append(f"{r},{c}\n{legend}")
            i += 1
        rows.append(out)
    assert i == len(positions), f"{i} keys in ROWS, {len(positions)} in LAYOUT"
    return rows


def main():
    ansi = json.load(open(ANSI))
    positions = layout_positions()
    assert len(set(positions)) == len(positions) == 69
    out = {
        "name": "HHKB Professional Classic / Hybrid JIS",
        "vendorId": ansi["vendorId"],
        "productId": "0x4012",
        "customKeycodes": ansi["customKeycodes"],
        "matrix": {"rows": 5, "cols": 15},
        "menus": ansi["menus"],
        "layouts": {"keymap": keymap_rows(positions)},
    }
    with open(OUT, "w") as f:
        json.dump(out, f, indent=2, ensure_ascii=False)
        f.write("\n")
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
