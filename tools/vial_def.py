#!/usr/bin/env python3
"""Builds the Vial keyboard definition from stm32/via/via.json (layout, matrix, custom keycodes;
VIA's menus dropped), xz-compressed as the Vial app expects, as a C header."""
import json
import lzma
import sys

src, out = sys.argv[1], sys.argv[2]
via = json.load(open(src))
vial = {k: via[k] for k in ("name", "vendorId", "productId", "matrix", "layouts", "customKeycodes") if k in via}
vial["layouts"] = {"keymap": via["layouts"]["keymap"]}
blob = lzma.compress(json.dumps(vial, separators=(",", ":")).encode())
body = ",".join(f"0x{b:02X}" for b in blob)
with open(out, "w") as f:
    f.write("#pragma once\n#include <stdint.h>\n")
    f.write(f"static const uint8_t vial_definition[{len(blob)}] = {{{body}}};\n")
