#!/usr/bin/env python3
"""Crop a board photo and mark pads: circle and label each one (docs pictures such as SWD pads).
Usage: uv run -q --with pillow python3 tools/annotate_photo.py IN OUT X0,Y0,X1,Y1 NAME@X,Y[@COLOUR] ...
Coordinates are in the full photo's pixels; HEIF input needs converting first (macOS: sips -s format jpeg)."""
import sys

from PIL import Image, ImageDraw, ImageFont

src, out, box = sys.argv[1], sys.argv[2], [int(v) for v in sys.argv[3].split(",")]
img = Image.open(src).convert("RGB").crop(box)
draw = ImageDraw.Draw(img)
try:
    font = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 30)
except OSError:
    font = ImageFont.load_default()
for spec in sys.argv[4:]:
    name, xy, *colour = spec.split("@")
    x, y = (int(v) for v in xy.split(","))
    x, y = x - box[0], y - box[1]
    c = colour[0] if colour else "#ffd400"
    draw.ellipse((x - 34, y - 34, x + 34, y + 34), outline=c, width=5)
    tw = draw.textlength(name, font=font)
    tx = x - 44 - tw if x > (box[2] - box[0]) / 2 else x + 44  # label outside the pad, away from the edge
    draw.rectangle((tx - 6, y - 20, tx + tw + 6, y + 20), fill="#000000c0")
    draw.text((tx, y - 17), name, fill=c, font=font)
img.save(out, quality=88)
print(f"wrote {out} ({img.width}x{img.height})")
