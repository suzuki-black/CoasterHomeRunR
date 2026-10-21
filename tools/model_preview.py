#!/usr/bin/env python3
"""敵の絵(src/engine/models.c の MRect の表)を、ゲームと同じブロックの粗さで画像に描き出す(絵を詰めるための道具)。

  model_preview.py out.png [models.c] [名前 ...]

名前(m_e01 など)を省くと全部。各モデルを 2m・4m・8m の距離で、ゲームと同じように描く:
  1/16m あたりのブロック数 = 512 / z(z は 1/256m)、端は四捨五入、縁取りは白で上下左右に 1 ブロック広げて先に塗る。
  部位 3(飾り)は縁取りの回に塗らず、約 7m より遠いと描かない(game.c の DECO_EN_Z)。
"""
import re
import sys
from PIL import Image, ImageDraw

# 固定色 0-8(main.c の pal_fixed。0RRR0BBB, 00000GGG を 0-255 に)
def rgb(rb, g):
    return (((rb >> 4) & 7) * 255 // 7, (g & 7) * 255 // 7, (rb & 7) * 255 // 7)


COL = {
    'C_BLACK': rgb(0x00, 0), 'C_WHITE': rgb(0x77, 7), 'C_RED': rgb(0x71, 1), 'C_BROWN': rgb(0x51, 3),
    'C_PURPLE': rgb(0x56, 2), 'C_YELLOW': rgb(0x71, 7), 'C_ORANGE': rgb(0x70, 4), 'C_CYAN': rgb(0x37, 6),
}
BG = (70, 90, 70)
DIST = (512, 1024, 2048)       # 2m・4m・8m(1/256m)
DECO_Z = 1800                   # game.c の DECO_EN_Z: 飾り(部位 3)はこれより近いときだけ


def parse(path):
    """MRect の表を名前ごとに(同じ名前が #if で 2 つあるときは後のもの)"""
    src = open(path, encoding='utf-8').read()
    models = {}
    for m in re.finditer(r'static const MRect (\w+)\[\] = \{(.*?)\n\};', src, re.S):
        rects = []
        for r in re.finditer(r'\{\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(C_\w+),\s*(\d)\s*\}', m.group(2)):
            x0, y0, x1, y1 = (int(v) for v in r.groups()[:4])
            rects.append((x0, y0, x1, y1, r.group(5), int(r.group(6))))
        models[m.group(1)] = rects
    return models


def blk(v, k, c, lim):
    """geom.s の dm_cx / dm_cy: 画面座標(12.4)= c + v * k、ブロックへ四捨五入して 0..lim に"""
    s = c + (abs(v) * k >> 8) * (1 if v >= 0 else -1)
    return max(0, min(lim, (s + 7) >> 4))


def render(rects, z, W=40, H=46):
    k = (1 << 21) // z
    thin = (z - 1) >> 10 if z > 3000 else 0
    cx, cy = W * 8, (H - 2) * 16                      # 足元を下の中央に
    img = [[None] * W for _ in range(H)]
    boxes = []
    for x0, y0, x1, y1, col, part in rects:
        if part == 3 and z >= DECO_Z:
            continue
        if min(x1 - x0, y1 - y0) <= thin:
            continue
        bx0, bx1 = blk(x0, k, cx, W), blk(x1, k, cx, W)
        by0 = max(0, min(H, (cy - (abs(y1) * k >> 8) * (1 if y1 >= 0 else -1) + 7) >> 4))
        by1 = max(0, min(H, (cy - (abs(y0) * k >> 8) * (1 if y0 >= 0 else -1) + 7) >> 4))
        if bx1 > bx0 and by1 > by0:
            boxes.append((bx0, by0, bx1, by1, col, part))
    for bx0, by0, bx1, by1, col, part in boxes:  # 縁取り
        if part == 3:
            continue
        for y in range(max(0, by0 - 1), min(H, by1 + 1)):
            for x in range(max(0, bx0 - 1), min(W, bx1 + 1)):
                img[y][x] = COL['C_WHITE']
    for bx0, by0, bx1, by1, col, part in boxes:
        for y in range(by0, by1):
            for x in range(bx0, bx1):
                img[y][x] = COL[col]
    return img


def main():
    out = sys.argv[1]
    path = sys.argv[2] if len(sys.argv) > 2 else 'src/engine/models.c'
    models = parse(path)
    names = sys.argv[3:] or list(models)
    S, W, H = 5, 40, 46
    cw = W * S * len(DIST) + 12
    sheet = Image.new('RGB', (cw * min(2, len(names)), (H * S + 16) * ((len(names) + 1) // 2)), (30, 30, 30))
    d = ImageDraw.Draw(sheet)
    for i, n in enumerate(names):
        ox, oy = (i % 2) * cw, (i // 2) * (H * S + 16)
        d.text((ox + 2, oy + 2), f'{n} ({len(models[n])})', fill=(255, 255, 255))
        for j, z in enumerate(DIST):
            img = render(models[n], z, W, H)
            for y in range(H):
                for x in range(W):
                    c = img[y][x] or BG
                    d.rectangle([ox + j * W * S + x * S, oy + 14 + y * S, ox + j * W * S + x * S + S - 1, oy + 14 + y * S + S - 1], fill=c)
    sheet.save(out)


if __name__ == '__main__':
    main()
