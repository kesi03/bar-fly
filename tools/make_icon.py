#!/usr/bin/env python3
"""Draw the BAR-FLY icon as a 960x540 PNG using only the Python stdlib.

Run from the repository root:  python tools/make_icon.py
Writes firmware/bar-fly/icon.png
"""

import math
import struct
import zlib
from pathlib import Path

W, H = 960, 540
SS = 2  # supersampling for smooth edges
SW, SH = W * SS, H * SS

BG = (20, 13, 10)
STRIPE = (36, 24, 18)
BROWN = (90, 50, 28)
DARK = (11, 7, 5)
ORANGE = (255, 140, 26)
GOLD = (242, 193, 78)
CREAM = (243, 229, 200)
AVO = (143, 164, 75)
RED = (201, 78, 63)


def blank():
    px = bytearray(SW * SH * 3)
    for y in range(SH):
        t = y / SH
        base = (BG[0] + int(10 * t), BG[1] + int(5 * t), BG[2] + int(2 * t))
        row = bytes(base)
        o = y * SW * 3
        px[o:o + SW * 3] = row * SW
    return px


def put(px, x, y, color):
    if 0 <= x < SW and 0 <= y < SH:
        o = (y * SW + x) * 3
        px[o:o + 3] = bytes(color)


def circle(px, cx, cy, r, color, inner=None):
    ellipse(px, cx, cy, r, r, color, inner=inner)


def ellipse(px, cx, cy, rx, ry, color, angle=0.0, inner=None, inner_angle=None):
    cx, cy, rx, ry = cx * SS, cy * SS, rx * SS, ry * SS
    a = angle if inner_angle is None else inner_angle
    ca, sa = math.cos(a), math.sin(a)
    r2 = rx * rx, ry * ry
    irx = iry = 0
    if inner is not None:
        irx = rx * inner[0]
        iry = ry * inner[1]
    reach = max(rx, ry) + 2
    x0, x1 = max(0, int(cx - reach)), min(SW, int(cx + reach + 1))
    y0, y1 = max(0, int(cy - reach)), min(SH, int(cy + reach + 1))
    for y in range(y0, y1):
        dy = y - cy
        for x in range(x0, x1):
            dx = x - cx
            u = dx * ca + dy * sa
            v = -dx * sa + dy * ca
            d = (u / r2[0]) ** 2 + (v / r2[1]) ** 2
            if d <= 1.0:
                col = color
                if inner is not None:
                    ia = a if inner_angle is None else inner_angle
                    ic, isd = math.cos(ia), math.sin(ia)
                    iu = dx * ic + dy * isd
                    iv = -dx * isd + dy * ic
                    if (iu / irx) ** 2 + (iv / iry) ** 2 < 1.0:
                        col = inner[2]
                put(px, x, y, col)


def ring(px, cx, cy, r, thick, color):
    cx, cy, r, thick = cx * SS, cy * SS, r * SS, thick * SS
    lo, hi = (r - thick) ** 2, (r + thick) ** 2
    for y in range(max(0, int(cy - r - thick)), min(SH, int(cy + r + thick + 1))):
        dy = y - cy
        for x in range(max(0, int(cx - r - thick)), min(SW, int(cx + r + thick + 1))):
            dx = x - cx
            d = dx * dx + dy * dy
            if lo <= d <= hi:
                put(px, x, y, color)


def line(px, x0, y0, x1, y1, width, color):
    x0, y0, x1, y1 = x0 * SS, y0 * SS, x1 * SS, y1 * SS
    w = width * SS
    dx, dy = x1 - x0, y1 - y0
    ln2 = dx * dx + dy * dy
    reach = w + 2
    for y in range(max(0, int(min(y0, y1) - reach)), min(SH, int(max(y0, y1) + reach + 1))):
        for x in range(max(0, int(min(x0, x1) - reach)), min(SW, int(max(x0, x1) + reach + 1))):
            t = 0.0 if ln2 == 0 else max(0.0, min(1.0, ((x - x0) * dx + (y - y0) * dy) / ln2))
            qx, qy = x0 + t * dx, y0 + t * dy
            if (x - qx) ** 2 + (y - qy) ** 2 <= w * w:
                put(px, x, y, color)


def downsample(px):
    out = bytearray(W * H * 3)
    n = SS * SS
    for y in range(H):
        for x in range(W):
            r = g = b = 0
            for yy in range(SS):
                row = (y * SS + yy) * SW
                for xx in range(SS):
                    o = (row + x * SS + xx) * 3
                    r += px[o]
                    g += px[o + 1]
                    b += px[o + 2]
            o = (y * W + x) * 3
            out[o] = r // n
            out[o + 1] = g // n
            out[o + 2] = b // n
    return out


def png(rows):
    raw = b''.join(b'\x00' + bytes(row) for row in rows)

    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data +
                struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff))

    return (b'\x89PNG\r\n\x1a\n' +
            chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def main():
    px = blank()

    # 70s lounge stripes and a faint vinyl halo behind the motif.
    for y, h, col in ((0, 26, STRIPE), (H - 30, 30, STRIPE),
                      (74, 6, BROWN), (H - 88, 6, BROWN)):
        for yy in range(y * SS, (y + h) * SS):
            o = yy * SW * 3
            px[o:o + SW * 3] = bytes(col) * SW
    cx, cy = 400, 270
    ring(px, cx, cy, 196, 3, (78, 52, 36))
    ring(px, cx, cy, 176, 2, (64, 42, 29))
    ring(px, cx, cy, 156, 2, (52, 34, 23))

    # wings: two big translucent-looking gold lobes, one per side.
    ellipse(px, 302, 176, 138, 78, GOLD, angle=-0.62)
    ellipse(px, 498, 176, 138, 78, GOLD, angle=0.62)
    ellipse(px, 306, 182, 108, 54, ORANGE, angle=-0.62)
    ellipse(px, 494, 182, 108, 54, ORANGE, angle=0.62)
    line(px, 330, 216, 222, 122, 3, BROWN)
    line(px, 470, 216, 578, 122, 3, BROWN)
    line(px, 336, 226, 246, 176, 2, DARK)
    line(px, 464, 226, 554, 176, 2, DARK)

    # body: striped abdomen, orange with dark bands.
    ellipse(px, cx, 310, 62, 108, ORANGE)
    ellipse(px, cx, 310, 48, 94, GOLD)
    for by, bw in ((266, 56), (294, 58), (322, 56), (350, 48)):
        ellipse(px, cx, by, bw, 7, DARK)
    line(px, cx - 9, 400, cx - 19, 452, 5, GOLD)
    line(px, cx + 9, 400, cx + 19, 452, 5, GOLD)

    # head and eyes: the face that reads at tile size.
    circle(px, cx - 28, 178, 40, CREAM)
    circle(px, cx + 28, 178, 40, CREAM)
    circle(px, cx - 28, 178, 17, DARK)
    circle(px, cx + 28, 178, 17, DARK)
    circle(px, cx - 33, 172, 5, CREAM)
    circle(px, cx + 23, 172, 5, CREAM)
    line(px, cx - 38, 140, cx - 74, 104, 4, CREAM)
    line(px, cx + 38, 140, cx + 74, 104, 4, CREAM)
    circle(px, cx - 78, 100, 7, GOLD)
    circle(px, cx + 78, 100, 7, GOLD)

    # a couple of bar-fly accent dots: olive and a red weekender.
    circle(px, 790, 190, 24, GOLD)
    circle(px, 848, 268, 13, AVO)
    circle(px, 790, 346, 19, RED)

    small = downsample(px)
    rows = [small[y * W * 3:(y + 1) * W * 3] for y in range(H)]
    out = Path(__file__).resolve().parent.parent / 'firmware' / 'bar-fly' / 'icon.png'
    out.write_bytes(png(rows))
    print(f'wrote {out} ({out.stat().st_size} bytes)')


if __name__ == '__main__':
    main()
