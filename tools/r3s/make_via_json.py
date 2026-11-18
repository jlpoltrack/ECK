#!/usr/bin/env python3
"""Write the Realforce R3S VIA definition (stm32/boards/r3s/via.json) from the HHKB's
(stm32/via/via.json, for the settings menu) and boards/r3s/board_config.h (matrix positions).
Run after changing either: python3 tools/r3s/make_via_json.py"""
import copy
import json
import os
import re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HHKB = os.path.join(ROOT, "stm32/via/via.json")
CONFIG = os.path.join(ROOT, "stm32/boards/r3s/board_config.h")
OUT = os.path.join(ROOT, "stm32/boards/r3s/via.json")
MAX_SCAN_RATE = 3  # 500 Hz: the scan takes ~1.6 ms (docs/r3s-hardware.md)

# Physical rows in LAYOUT order (board_config.h): (legend, width, gap before)
ROWS = [
    [("Esc", 1, 0), ("F1", 1, 1), ("F2", 1, 0), ("F3", 1, 0), ("F4", 1, 0), ("F5", 1, 0.5), ("F6", 1, 0), ("F7", 1, 0),
     ("F8", 1, 0), ("F9", 1, 0.5), ("F10", 1, 0), ("F11", 1, 0), ("F12", 1, 0), ("PrtSc", 1, 0.25), ("ScrLk", 1, 0),
     ("Pause", 1, 0)],
    [("~\n`", 1, 0), ("!\n1", 1, 0), ("@\n2", 1, 0), ("#\n3", 1, 0), ("$\n4", 1, 0), ("%\n5", 1, 0), ("^\n6", 1, 0),
     ("&\n7", 1, 0), ("*\n8", 1, 0), ("(\n9", 1, 0), (")\n0", 1, 0), ("_\n-", 1, 0), ("+\n=", 1, 0),
     ("Backspace", 2, 0), ("Insert", 1, 0.25), ("Home", 1, 0), ("PgUp", 1, 0)],
    [("Tab", 1.5, 0), ("Q", 1, 0), ("W", 1, 0), ("E", 1, 0), ("R", 1, 0), ("T", 1, 0), ("Y", 1, 0), ("U", 1, 0),
     ("I", 1, 0), ("O", 1, 0), ("P", 1, 0), ("{\n[", 1, 0), ("}\n]", 1, 0), ("|\n\\", 1.5, 0), ("Delete", 1, 0.25),
     ("End", 1, 0), ("PgDn", 1, 0)],
    [("Caps Lock", 1.75, 0), ("A", 1, 0), ("S", 1, 0), ("D", 1, 0), ("F", 1, 0), ("G", 1, 0), ("H", 1, 0),
     ("J", 1, 0), ("K", 1, 0), ("L", 1, 0), (":\n;", 1, 0), ("\"\n'", 1, 0), ("Enter", 2.25, 0)],
    [("Shift", 2.25, 0), ("Z", 1, 0), ("X", 1, 0), ("C", 1, 0), ("V", 1, 0), ("B", 1, 0), ("N", 1, 0), ("M", 1, 0),
     ("<\n,", 1, 0), (">\n.", 1, 0), ("?\n/", 1, 0), ("Shift", 2.75, 0), ("↑", 1, 1.25)],
    [("Ctrl", 1.25, 0), ("Win", 1.25, 0), ("Alt", 1.25, 0), ("", 6.25, 0), ("Alt", 1.25, 0), ("Menu", 1.25, 0),
     ("Fn", 1.25, 0), ("Ctrl", 1.25, 0), ("←", 1, 0.25), ("↓", 1, 0), ("→", 1, 0)],
]


def layout_positions():
    text = open(CONFIG).read()
    body = text[text.index("#define LAYOUT {"):text.index("#define LAYOUT_DEFAULT")]
    return [(int(r), int(c)) for r, c in re.findall(r"\{(\d+), (\d+)\}", body)]


def keymap_rows(positions):
    rows, i = [], 0
    for n, row in enumerate(ROWS):
        out = [{"y": 0.5}] if n == 1 else []  # gap below the function row
        for legend, w, gap in row:
            props = {k: v for k, v in (("x", gap), ("w", w)) if v not in (0, 1)}
            if props:
                out.append(props)
            r, c = positions[i]
            out.append(f"{r},{c}\n{legend}" if legend else f"{r},{c}")
            i += 1
        rows.append(out)
    assert i == len(positions), f"{i} keys in ROWS, {len(positions)} in LAYOUT"
    return rows


def menus(hhkb_menus):
    m = copy.deepcopy(hhkb_menus)
    top = m[0]
    top["label"] = "Settings"
    for section in top["content"]:
        if section["label"] == "Scan Rate":
            keep = []
            for item in section["content"]:
                ident = item.get("content", [None])[0]
                if ident == "id_hhkb_ble_scan_rate" or "Hybrid" in str(item.get("content")):
                    continue  # Bluetooth only
                if ident == "id_hhkb_scan_rate":
                    item["options"] = [o for o in item["options"] if o[1] <= MAX_SCAN_RATE]
                keep.append(item)
            section["content"] = keep
        if section["label"] == "LED":
            led = next(i for i in section["content"] if i["content"][0] == "id_hhkb_led_1")
            led["label"] = "Simple Mode LED"
            led["options"] = [o for o in led["options"] if "Bluetooth" not in o[0]]
            section["content"] = [led]
    return m


def main():
    hhkb = json.load(open(HHKB))
    positions = layout_positions()
    assert len(set(positions)) == len(positions) == 87
    out = {
        "name": "Realforce R3S (ECK)",
        "vendorId": hhkb["vendorId"],
        "productId": "0x4011",
        "customKeycodes": hhkb["customKeycodes"],
        "matrix": {"rows": 6, "cols": 16},
        "menus": menus(hhkb["menus"]),
        "layouts": {"keymap": keymap_rows(positions)},
    }
    with open(OUT, "w") as f:
        json.dump(out, f, indent=2, ensure_ascii=False)
        f.write("\n")
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
