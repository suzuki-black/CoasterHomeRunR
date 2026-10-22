#!/usr/bin/env python3
"""タイトル画面の絵を SCREEN 11(V9958 の YJK + YAE。自然画にパレットの色の文字を重ねる)の VRAM の並びにする。

  gen_title.py art.webp out.bin title.h [preview.png]

- 絵(data/title_art.webp)を 4:3 に切って 256×212 に縮め、YJK で表す(横 4 ドットで色 J・K を共有し、明るさ Y は 1 ドットごと。
  YAE なので Y は 4 ビット)。明るさは誤差拡散でならす。
- 文字は絵に書き込まず、パレットの色のドット(A = 1)にする。YJK の色の共有でにじまない。
    上: COASTER / HOME RUN(2 行で大きく。今までのタイトルと同じ 3×5 のブロックフォントを、立体感のあるタイルにして)
    中: PUSH SPACE KEY(点滅させる。文字の無い 2 ページ目を作り、表示するページを切り替える)
    下: © 2026 suzuki-black
- out.bin = 1 ページ目(文字あり、256×212 = 54272 バイト)+ 文字の無い PUSH SPACE KEY の帯(2 ページ目の同じ行に書く)。
  title.h = 帯の行と大きさ、パレット(16 色、0RRR0BBB / 00000GGG)。
- preview.png は VRAM の値を V9958 と同じ式で色に戻した絵(4:3 に引き伸ばして)。
"""
import sys
from PIL import Image

W, H = 256, 212
TILE_FONT = {   # tools/gen_data.py の FONT(3×5)と同じ形
    'A': ['.#.', '#.#', '###', '#.#', '#.#'], 'C': ['.##', '#..', '#..', '#..', '.##'],
    'E': ['###', '#..', '##.', '#..', '###'], 'H': ['#.#', '#.#', '###', '#.#', '#.#'],
    'K': ['#.#', '#.#', '##.', '#.#', '#.#'], 'M': ['#.#', '###', '###', '#.#', '#.#'],
    'N': ['##.', '#.#', '#.#', '#.#', '#.#'], 'O': ['.#.', '#.#', '#.#', '#.#', '.#.'],
    'P': ['##.', '#.#', '##.', '#..', '#..'], 'R': ['##.', '#.#', '##.', '#.#', '#.#'],
    'S': ['.##', '#..', '.#.', '..#', '##.'], 'T': ['###', '.#.', '.#.', '.#.', '.#.'],
    'U': ['#.#', '#.#', '#.#', '#.#', '###'], 'Y': ['#.#', '#.#', '.#.', '.#.', '.#.'],
    '0': ['###', '#.#', '#.#', '#.#', '###'], '2': ['##.', '..#', '.#.', '#..', '###'],
    '6': ['.##', '#..', '###', '#.#', '###'], '-': ['...', '...', '###', '...', '...'],
    ' ': ['...', '...', '...', '...', '...'],
    # 小文字(タイトルの著作権表示のためだけ。同じ 3×5 の調子で)
    's': ['...', '.##', '#..', '..#', '##.'], 'u': ['...', '#.#', '#.#', '#.#', '.##'],
    'z': ['...', '###', '.#.', '#..', '###'], 'k': ['#..', '#.#', '##.', '#.#', '#.#'],
    'i': ['.#.', '...', '.#.', '.#.', '.#.'], 'b': ['#..', '#..', '##.', '#.#', '##.'],
    'l': ['##.', '.#.', '.#.', '.#.', '###'], 'a': ['...', '.##', '#.#', '#.#', '.##'],
    'c': ['...', '.##', '#..', '#..', '.##'],
}
COPY_MARK = ['..#####..', '.#.....#.', '#..###..#', '#.#.....#', '#.#.....#',
             '#.#.....#', '#..###..#', '.#.....#.', '..#####..']

# パレット(YAE の文字の色。0RRR0BBB, 00000GGG)。0 は使わない
PAL = {
    1: (0, 0, 0),       # 縁取り
    2: (7, 7, 1),       # COASTER の面(黄)
    3: (6, 4, 0),       # COASTER のタイルの影
    4: (7, 4, 0),       # HOME RUN の面(橙)
    5: (5, 1, 0),       # HOME RUN のタイルの影
    6: (7, 7, 7),       # PUSH SPACE KEY・著作権表示の面(白)
    7: (4, 4, 5),       # 白い文字のタイルの影
    8: (5, 5, 6),       # 著作権表示(明るい灰色。タイトルより目立たせない)
}


def text_width(s, t):
    return (len(s) * 4 - 1) * t


def draw_tiles(mask, s, x0, y0, t, face, shade):
    """3×5 の文字を t×t のタイルで。タイルの右と下の 1 ドットは影の色(t >= 3 のとき)"""
    x = x0
    for ch in s:
        g = TILE_FONT[ch]
        for gy, row in enumerate(g):
            for gx, c in enumerate(row):
                if c != '#':
                    continue
                for yy in range(t):
                    for xx in range(t):
                        px, py = x + gx * t + xx, y0 + gy * t + yy
                        edge = t >= 3 and (xx == t - 1 or yy == t - 1)
                        mask[py][px] = shade if edge else face
        x += 4 * t


def draw_bitmap(mask, rows, x0, y0, color):
    for gy, row in enumerate(rows):
        for gx, c in enumerate(row):
            if c == '#':
                mask[y0 + gy][x0 + gx] = color


def outline(mask, x0, y0, x1, y1, w):
    """文字の外側 w ドットを縁取りの色(1)に"""
    src = [r[:] for r in mask]
    for y in range(max(0, y0 - w), min(H, y1 + w)):
        for x in range(max(0, x0 - w), min(W, x1 + w)):
            if src[y][x]:
                continue
            near = any(src[yy][xx] not in (0, 1)
                       for yy in range(max(0, y - w), min(H, y + w + 1))
                       for xx in range(max(0, x - w), min(W, x + w + 1)))
            if near:
                mask[y][x] = 1


def layout():
    """文字の配置。戻り値: 1 ページ目の文字(各ドットのパレット番号、0 = 絵)、2 ページ目(PUSH SPACE KEY 無し)、帯の行"""
    m = [[0] * W for _ in range(H)]
    # タイトルは 2 行で大きく(2026-10-23 ユーザー: 「タイトルはドンと」)。COASTER は黄、HOME RUN は橙。
    # 2 行の間をあけて主人公の顔(絵の 31〜60 行目。目は 44〜49 行目)を見せる(ユーザー: 「間あけたら主人公の顔はいるよね」)
    t = 7
    s1, s2 = 'COASTER', 'HOME RUN'
    y1 = 3
    draw_tiles(m, s1, (W - text_width(s1, t)) // 2, y1, t, 2, 3)
    y2 = 84
    draw_tiles(m, s2, (W - text_width(s2, t)) // 2, y2, t, 4, 5)
    outline(m, 0, y1, W, y1 + 5 * t, 2)
    outline(m, 0, y2, W, y2 + 5 * t, 2)
    # 著作権表示(一番下)
    cs = ' 2026 suzuki-black'
    ct = 2
    cw = 9 + 2 + text_width(cs.strip(), ct) + 4 * ct
    cx = (W - cw) // 2
    cy = H - 5 * ct - 6
    draw_bitmap(m, COPY_MARK, cx, cy + (5 * ct - 9) // 2, 8)
    draw_tiles(m, '2026 suzuki-black', cx + 9 + 2 + 4 * ct - ct, cy, ct, 8, 8)
    outline(m, 0, cy - 1, W, cy + 5 * ct + 1, 1)
    page2 = [r[:] for r in m]
    # PUSH SPACE KEY(中段。HOME RUN の下と著作権表示の間の中ほど)
    ps = 'PUSH SPACE KEY'
    pt = 3
    py = 160
    draw_tiles(m, ps, (W - text_width(ps, pt)) // 2, py, pt, 6, 7)
    outline(m, 0, py, W, py + 5 * pt, 1)
    band = (py - 2, py + 5 * pt + 2)
    return m, page2, band


def clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


def yjk_decode(y, j, k):
    """V9958: R = Y + J、G = Y + K、B = (5Y - 2J - K) / 4(5 ビット、0〜31 に切る)"""
    return (clamp(y + j, 0, 31), clamp(y + k, 0, 31), clamp((5 * y - 2 * j - k) // 4, 0, 31))


def encode(img, mask):
    """img: 256×212 の RGB(0〜255)。mask: パレット番号(0 = 絵)。戻り値: VRAM のバイト列"""
    px = img.load()
    rgb = [[[px[x, y][c] * 31 / 255 for c in range(3)] for x in range(W)] for y in range(H)]
    err = [[0.0] * (W + 2) for _ in range(H + 1)]       # 明るさの誤差拡散(Y の段数のずれ)
    out = bytearray(W * H)
    for y in range(H):
        for g in range(0, W, 4):
            idx = [x for x in range(g, g + 4) if not mask[y][x]]
            # 色(J・K)は絵のドットの平均から
            if idx:
                ys = [(4 * rgb[y][x][2] + 2 * rgb[y][x][0] + rgb[y][x][1]) / 8 for x in idx]
                j = round(sum(rgb[y][x][0] - yy for x, yy in zip(idx, ys)) / len(idx))
                k = round(sum(rgb[y][x][1] - yy for x, yy in zip(idx, ys)) / len(idx))
            else:
                j = k = 0
            j, k = clamp(j, -32, 31), clamp(k, -32, 31)
            ju, ku = j & 63, k & 63
            low = [ku & 7, ku >> 3, ju & 7, ju >> 3]
            for i, x in enumerate(range(g, g + 4)):
                if mask[y][x]:
                    out[y * W + x] = (mask[y][x] << 4) | 8 | low[i]
                    continue
                tr, tg, tb = rgb[y][x]
                e = err[y][x + 1]
                best, bd, bv = 0, 1e9, 0
                for y4 in range(16):                    # YAE: Y は 4 ビット(5 ビットの偶数)
                    yy = y4 * 2
                    r, gg, b = yjk_decode(yy, j, k)
                    lum_t = (2 * tr + 4 * tg + tb) / 7 + e
                    lum = (2 * r + 4 * gg + b) / 7
                    d = (r - tr) ** 2 + (gg - tg) ** 2 + (b - tb) ** 2 + 3 * (lum - lum_t) ** 2
                    if d < bd:
                        bd, best, bv = d, y4, lum_t - lum
                out[y * W + x] = (best << 4) | low[i]
                # Floyd–Steinberg(明るさだけ)
                err[y][x + 2] += bv * 7 / 16
                err[y + 1][x] += bv * 3 / 16
                err[y + 1][x + 1] += bv * 5 / 16
                err[y + 1][x + 2] += bv * 1 / 16
    return out


def decode(vram):
    """VRAM の値を色に戻す(確かめ用)"""
    img = Image.new('RGB', (W, H))
    p = img.load()
    for y in range(H):
        for g in range(0, W, 4):
            b = vram[y * W + g: y * W + g + 4]
            k = (b[0] & 7) | ((b[1] & 7) << 3)
            j = (b[2] & 7) | ((b[3] & 7) << 3)
            k = k - 64 if k >= 32 else k
            j = j - 64 if j >= 32 else j
            for i in range(4):
                v = b[i]
                if v & 8:
                    r, gg, bb = PAL[v >> 4]
                    p[g + i, y] = (r * 255 // 7, gg * 255 // 7, bb * 255 // 7)
                else:
                    r, gg, bb = yjk_decode((v >> 4) * 2, j, k)
                    p[g + i, y] = (r * 255 // 31, gg * 255 // 31, bb * 255 // 31)
    return img


def main(art, out_bin, out_h, preview=None):
    src = Image.open(art).convert('RGB')
    sw, sh = src.size
    cw = sh * 4 // 3                                   # 4:3 に切る(左右を同じだけ)
    x0 = (sw - cw) // 2
    img = src.crop((x0, 0, x0 + cw, sh)).resize((W, H), Image.LANCZOS)
    mask, mask2, band = layout()
    page1 = encode(img, mask)
    page2 = encode(img, mask2)
    b0, b1 = band
    data = bytes(page1) + bytes(page2[b0 * W: b1 * W])
    open(out_bin, 'wb').write(data)
    with open(out_h, 'w') as f:
        f.write('/* tools/gen_title.py が作る。タイトル画面の絵(SCREEN 11)の配置とパレット */\n')
        f.write(f'#define TITLE_BAND_Y   {b0}      /* PUSH SPACE KEY の帯の先頭の行(2 ページ目に文字の無い帯を書く) */\n')
        f.write(f'#define TITLE_BAND_LEN {(b1 - b0) * W}  /* 帯のバイト数(1 ページ目の後ろに続く) */\n')
        f.write(f'#define TITLE_BYTES    {len(data)}\n')
        pal = []
        for i in range(16):
            r, g, b = PAL.get(i, (0, 0, 0))
            pal += [f'0x{r:x}{b:x}', f'{g}']
        f.write('#define TITLE_PAL { ' + ', '.join(pal) + ' }\n')
    if preview:
        decode(page1).resize((W * 3 * 4 // 3 * 256 // 256 * 1, H * 3), Image.NEAREST).save(preview)


if __name__ == '__main__':
    main(*sys.argv[1:])
