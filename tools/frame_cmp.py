#!/usr/bin/env python3
"""frame_cmp.py — frame_dump.tcl の結果 2 つを比べる(変更の前後で絵が同じか)

    python3 tools/frame_cmp.py 前/frames.txt 後/frames.txt

フレームごとの位置が一致するか、地形だけの絵(敵・コイン・HUD を重ねる前)と完成した絵がそれぞれ何ブロック違うかを出す。
地形の描画だけを変えたなら、地形だけの絵は全フレームで一致するはず。
"""
import sys


def load(p):
    return [l.split() for l in open(p)]


def diff(a, b):
    if a == '-' or b == '-':
        return None
    da, db = bytes.fromhex(a), bytes.fromhex(b)
    return [(i // 64, i % 64, da[i], db[i]) for i in range(len(da)) if da[i] != db[i]]


def main():
    A, B = load(sys.argv[1]), load(sys.argv[2])
    n = min(len(A), len(B))
    pos_bad = env_bad = full_bad = 0
    for a, b in zip(A, B):
        if a[1] != b[1]:
            pos_bad += 1
            continue
        de = diff(a[3], b[3]) if len(a) > 3 and len(b) > 3 else None
        df = diff(a[2], b[2])
        if de:
            env_bad += 1
            if env_bad <= 5:
                print(f"地形だけの絵が違う: フレーム {a[0]} 位置 {a[1]} {len(de)} ブロック {de[:6]}")
        if df:
            full_bad += 1
    print(f"{n} 枚: 位置の不一致 {pos_bad}、地形だけの絵の不一致 {env_bad}、完成した絵の不一致 {full_bad}")


if __name__ == '__main__':
    main()
