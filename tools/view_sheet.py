#!/usr/bin/env python3
"""view_sheet.py — view_dump.tcl の結果を、位置ごとに前と後ろを並べた PNG にする

    python3 tools/view_sheet.py 出力/views.txt 出力.png [1 行に並べる位置の数]

1 ブロックを 3 ピクセルで描き、左に前、右に後ろの絵を置いて、上に位置[m]を書く。
"""
import sys
from PIL import Image, ImageDraw


def blocks(h):
    d = bytes.fromhex(h)
    out = bytearray(3072)
    for y in range(48):
        for c in range(32):
            v = d[(y >> 3) * 256 + c * 8 + (y & 7)]
            out[y * 64 + c * 2] = v >> 4
            out[y * 64 + c * 2 + 1] = v & 15
    return out


def palette(h):
    d = bytes.fromhex(h)
    p = []
    for i in range(16):
        rb, g = d[i * 2], d[i * 2 + 1]
        p.append(((rb >> 4 & 7) * 36, (g & 7) * 36, (rb & 7) * 36))
    return p


def frame(h, ph):
    b, p = blocks(h), palette(ph)
    im = Image.new('RGB', (64, 48))
    im.putdata([p[v] for v in b])
    return im.resize((192, 144), Image.NEAREST)


rows = {}
for l in open(sys.argv[1]):
    m, d, h, ph = l.split()[:4]
    rows.setdefault(int(m), {})[int(d)] = frame(h, ph)
ms = sorted(rows)
per = int(sys.argv[3]) if len(sys.argv) > 3 else 3
n = max(len(v) for v in rows.values())
cw, ch = 192 * n + 8, 160
sheet = Image.new('RGB', (cw * per, ch * ((len(ms) + per - 1) // per)), (255, 255, 255))
dr = ImageDraw.Draw(sheet)
for i, m in enumerate(ms):
    x, y = (i % per) * cw, (i // per) * ch
    dr.text((x + 2, y + 2), '%dm' % m, fill=(0, 0, 0))
    for j, d in enumerate(sorted(rows[m])):
        sheet.paste(rows[m][d], (x + j * 192, y + 16))
sheet.save(sys.argv[2])
