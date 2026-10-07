#!/usr/bin/env python3
"""frame_diff.py — 作りを変えたとき(絵が 1 ビット単位では一致しない変更)の確かめ方

    python3 tools/frame_diff.py 前/frames.txt 後/frames.txt [出力の PNG] [並べる枚数]

frame_dump.tcl の結果 2 つ(同じフレーム番号)の、表示される VRAM の中身(64×48 ブロック)を比べ、
フレームごとに違うブロックの数と、そのうち「前の絵の色の境目」(上下左右のどれかが違う色のブロック)から
1 ブロックより離れているものの数を出す。境目から離れた違いは、形の誤差ではなく、ものが消えた・増えたなどの
大きな違いの可能性があるので、数が 0 でなければ画像で確かめる。
PNG を指定すると、違うブロックの多い順に、前・後・違い(赤)を並べた画像を書く(パレットは frames.txt の 5 番目の欄)。
"""
import sys


def load(p):
    d = {}
    for l in open(p):
        f = l.split()
        d[int(f[0])] = f
    return d


def blocks(h):
    d = bytes.fromhex(h)
    out = bytearray(3072)
    for y in range(48):
        for c in range(32):
            v = d[(y >> 3) * 256 + c * 8 + (y & 7)]
            out[y * 64 + c * 2] = v >> 4
            out[y * 64 + c * 2 + 1] = v & 15
    return out


def edge_mask(a):
    """前の絵で、自分か上下左右(1 ブロック以内)に色の境目があるブロック"""
    e = bytearray(3072)
    for y in range(48):
        for x in range(64):
            v = a[y * 64 + x]
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                xx, yy = x + dx, y + dy
                if 0 <= xx < 64 and 0 <= yy < 48 and a[yy * 64 + xx] != v:
                    e[y * 64 + x] = 1
                    break
    near = bytearray(3072)
    for y in range(48):
        for x in range(64):
            if any(0 <= x + dx < 64 and 0 <= y + dy < 48 and e[(y + dy) * 64 + x + dx]
                   for dx in (-1, 0, 1) for dy in (-1, 0, 1)):
                near[y * 64 + x] = 1
    return near


def pal_rgb(h):
    b = bytes.fromhex(h)
    out = []
    for i in range(16):
        rb, g = b[2 * i], b[2 * i + 1]
        out.append((((rb >> 4) & 7) * 255 // 7, (g & 7) * 255 // 7, (rb & 7) * 255 // 7))
    return out


def main():
    A, B = load(sys.argv[1]), load(sys.argv[2])
    res = []
    for fr in sorted(set(A) & set(B)):
        a, b = blocks(A[fr][2]), blocks(B[fr][2])
        diff = [i for i in range(3072) if a[i] != b[i]]
        if not diff:
            res.append((fr, 0, 0))
            continue
        near = edge_mask(a)
        far = sum(1 for i in diff if not near[i])
        res.append((fr, len(diff), far))
    n = len(res)
    nd = [r for r in res if r[1]]
    print("%d 枚: 違うフレーム %d、違うブロック 平均 %.1f・最大 %d、境目から離れた違い 合計 %d(最大 %d / フレーム)" % (
        n, len(nd), sum(r[1] for r in res) / max(1, n), max([r[1] for r in res] or [0]),
        sum(r[2] for r in res), max([r[2] for r in res] or [0])))
    if len(sys.argv) > 3 and nd:
        from PIL import Image, ImageDraw
        k = int(sys.argv[4]) if len(sys.argv) > 4 else 6
        top = sorted(nd, key=lambda r: (r[2], r[1]), reverse=True)[:k]
        S = 4
        W, H = 64 * S, 48 * S
        img = Image.new('RGB', (W * 3 + 20, (H + 10) * len(top)), (255, 255, 255))
        dr = ImageDraw.Draw(img)
        for row, (fr, nb, far) in enumerate(top):
            pal = pal_rgb(A[fr][4]) if len(A[fr]) > 4 else [(i * 16, i * 16, i * 16) for i in range(16)]
            a, b = blocks(A[fr][2]), blocks(B[fr][2])
            y0 = row * (H + 10)
            for i in range(3072):
                y, x = divmod(i, 64)
                for col, src in ((0, a), (1, b)):
                    dr.rectangle([col * (W + 10) + x * S, y0 + y * S, col * (W + 10) + x * S + S - 1, y0 + y * S + S - 1], fill=pal[src[i]])
                c = (230, 30, 30) if a[i] != b[i] else tuple(v // 3 + 140 for v in pal[a[i]])
                dr.rectangle([2 * (W + 10) + x * S, y0 + y * S, 2 * (W + 10) + x * S + S - 1, y0 + y * S + S - 1], fill=c)
            print("  フレーム %d: 違うブロック %d、境目から離れた違い %d" % (fr, nb, far))
        img.save(sys.argv[3])


if __name__ == '__main__':
    main()
