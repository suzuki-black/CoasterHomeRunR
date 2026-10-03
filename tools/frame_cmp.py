#!/usr/bin/env python3
"""frame_cmp.py — frame_dump.tcl の結果 2 つを比べる(変更の前後で絵が同じか)

    python3 tools/frame_cmp.py 前/frames.txt 後/frames.txt

フレームごとの位置が一致するか、地形だけの絵(敵・コイン・HUD を重ねる前)と、表示される VRAM の中身が
それぞれ何ブロック違うかを出す。地形だけの絵は、1 ブロック 1 バイトの版(3072 バイト)と詰めた形の版(1536 バイト、
VRAM と同じ並び)のどちらでも、64×48 の並びに直して比べる。
"""
import sys


def load(p):
    return [l.split() for l in open(p)]


def canon(h):
    """描画バッファを 64×48 のブロックの並び(各 0〜15)に直す"""
    d = bytes.fromhex(h)
    if len(d) == 3072:
        return bytes(v & 15 for v in d)
    out = bytearray(3072)
    for y in range(48):
        for c in range(32):
            v = d[(y >> 3) * 256 + c * 8 + (y & 7)]
            out[y * 64 + c * 2] = v >> 4
            out[y * 64 + c * 2 + 1] = v & 15
    return bytes(out)


def diff(a, b, conv=False):
    if a == '-' or b == '-':
        return None
    da, db = (canon(a), canon(b)) if conv else (bytes.fromhex(a), bytes.fromhex(b))
    w = 64 if conv else 32
    return [(i // w, i % w, da[i], db[i]) for i in range(min(len(da), len(db))) if da[i] != db[i]]


def main():
    A, B = load(sys.argv[1]), load(sys.argv[2])
    n = min(len(A), len(B))
    pos_bad = env_bad = full_bad = 0
    for a, b in zip(A, B):
        if a[1] != b[1]:
            pos_bad += 1
            continue
        de = diff(a[3], b[3], True) if len(a) > 3 and len(b) > 3 else None
        df = diff(a[2], b[2])
        if df and full_bad < 5:
            print(f"VRAM が違う: フレーム {a[0]} 位置 {a[1]} {len(df)} バイト {df[:6]}")
        if de:
            env_bad += 1
            if env_bad <= 5:
                print(f"地形だけの絵が違う: フレーム {a[0]} 位置 {a[1]} {len(de)} ブロック {de[:6]}")
        if df:
            full_bad += 1
    print(f"{n} 枚: 位置の不一致 {pos_bad}、地形だけの絵の不一致 {env_bad}、VRAM の不一致 {full_bad}")


if __name__ == '__main__':
    main()
