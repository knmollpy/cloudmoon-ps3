#!/usr/bin/env python3
"""Generate a compact 512x512, 24-bit RGB CloudMoon preview icon (stdlib only)."""
import math
import struct
import zlib

W = H = 512
rows = bytearray()

def inside_circle(x, y, cx, cy, r):
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r

for y in range(H):
    rows.append(0)  # PNG filter: None
    for x in range(W):
        glow = max(0.0, 1.0 - math.sqrt((x - 250) ** 2 + (y - 245) ** 2) / 520.0)
        r = int(7 + 8 * glow)
        g = int(14 + 22 * glow)
        b = int(34 + 44 * glow)

        cloud = (
            inside_circle(x, y, 170, 285, 62)
            or inside_circle(x, y, 245, 245, 91)
            or inside_circle(x, y, 335, 285, 65)
            or inside_circle(x, y, 250, 310, 76)
        )
        if cloud:
            r, g, b = 75, 205, 244

        # A dark crescent cutout gives the mark a moon/cloud identity.
        if inside_circle(x, y, 310, 160, 67) and not inside_circle(x, y, 340, 135, 67):
            r, g, b = 7, 14, 34

        rows.extend((r, g, b))

def chunk(kind, payload):
    body = kind + payload
    return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body) & 0xffffffff)

png = (
    b"\x89PNG\r\n\x1a\n"
    + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
    + chunk(b"IDAT", zlib.compress(bytes(rows), 9))
    + chunk(b"IEND", b"")
)
with open("sce_sys/icon0.png", "wb") as output:
    output.write(png)
