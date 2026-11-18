#!/usr/bin/env python3
"""Resize the staged PS3 XMB icon to the required 320x176 dimensions."""
import pathlib
import sys

try:
    from PIL import Image
except ImportError as exc:
    raise SystemExit("Pillow is required to prepare ICON0.PNG (install python3-pil)") from exc

path = pathlib.Path(sys.argv[1])
if not path.is_file():
    raise SystemExit(f"Missing staged ICON0.PNG: {path}")
with Image.open(path) as source:
    image = source.convert("RGBA")
    image = image.resize((320, 176), Image.Resampling.LANCZOS)
    image.save(path, format="PNG", optimize=True)
with Image.open(path) as check:
    if check.size != (320, 176):
        raise SystemExit(f"ICON0 resize failed: got {check.size}")
print("Prepared ICON0.PNG at 320x176")
