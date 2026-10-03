#!/usr/bin/env python3
"""pcprof_cmp.py — pcprof.tcl をフレーム番号の範囲(PROF_FRAMES)で取った結果 2 つを、関数ごとの 1 フレームあたり ms で並べる

    python3 tools/pcprof_cmp.py 前/pcprof.txt 後/pcprof.txt [表示する数(既定 30)]

ゲームの進みはフレーム単位で決まるので、同じフレームの範囲なら変更の前後で同じ場面を描いている。
割合 × 1 フレームの時間で ms に直し、差の大きい順ではなく「前」の重い順に出す。
"""
import re
import sys


def load(path):
    ms = None
    funcs = {}
    for line in open(path):
        m = re.search(r'1 フレーム ([0-9.]+) ms', line)
        if m:
            ms = float(m.group(1))
            continue
        if line.startswith('== ラベルごと'):
            break
        m = re.match(r'\s*([0-9.]+)%\s+(\S+)', line)
        if m:
            funcs[m.group(2)] = float(m.group(1))
    return ms, {k: v * ms / 100 for k, v in funcs.items()}


def main():
    ta, a = load(sys.argv[1])
    tb, b = load(sys.argv[2])
    n = int(sys.argv[3]) if len(sys.argv) > 3 else 30
    print(f"1 フレーム {ta:.2f} → {tb:.2f} ms({tb - ta:+.2f})")
    print(f"{'関数':<24}{'前 ms':>8}{'後 ms':>8}{'差':>8}")
    for k in sorted(a, key=lambda k: -a[k])[:n]:
        print(f"{k:<24}{a[k]:8.2f}{b.get(k, 0):8.2f}{b.get(k, 0) - a[k]:+8.2f}")


if __name__ == '__main__':
    main()
