#!/usr/bin/env python3
"""perf_pos_cmp.py — perf_pos.tcl の結果 2 つを、レール上の同じ位置どうしで比べる

    python3 tools/perf_pos_cmp.py 前/perf_pos.txt 後/perf_pos.txt [区間の長さ m(既定 8)]

位置を区間に分け、両方にフレームがある区間だけで 1 フレームの時間と地形の描画の時間の平均を出し、区間ごとの差と全体の平均を表示する。
"""
import sys


def load(path, w):
    b = {}
    for line in open(path):
        t = line.split()
        if len(t) < 3:
            continue
        m, fr, env = float(t[0]), float(t[1]), float(t[2])
        b.setdefault(int(m // w), []).append((fr, env))
    return b


def main():
    w = float(sys.argv[3]) if len(sys.argv) > 3 else 8.0
    a, b = load(sys.argv[1], w), load(sys.argv[2], w)
    keys = sorted(set(a) & set(b))
    sa = sb = ea = eb = 0.0
    print(f"{'位置(m)':>10} {'前 ms':>8} {'後 ms':>8} {'差':>7}   {'前 地形':>8} {'後 地形':>8}")
    for k in keys:
        fa = sum(x[0] for x in a[k]) / len(a[k]); fb = sum(x[0] for x in b[k]) / len(b[k])
        xa = sum(x[1] for x in a[k]) / len(a[k]); xb = sum(x[1] for x in b[k]) / len(b[k])
        sa += fa; sb += fb; ea += xa; eb += xb
        print(f"{k*w:6.0f}-{(k+1)*w:<4.0f}{fa:8.2f} {fb:8.2f} {fb-fa:+7.2f}   {xa:8.2f} {xb:8.2f}")
    n = len(keys)
    print(f"\n区間 {n} 個の平均: 1 フレーム {sa/n:.2f} → {sb/n:.2f} ms({(sb-sa)/n:+.2f})、"
          f"地形の描画 {ea/n:.2f} → {eb/n:.2f} ms({(eb-ea)/n:+.2f})、fps {1000*n/sa:.1f} → {1000*n/sb:.1f}")


if __name__ == '__main__':
    main()
