#!/usr/bin/env python3
"""pcprof.tcl 用のシンボル表を作る: .noi のグローバルと、.rst(リンク後の番地が入ったリスト)のラベル。

  prof_syms.py build out.txt

出力の各行は「領域 番地 名前」。領域はページ2のどのコードかを分ける:
  R  常駐部(ページ0・ページ1)
  E  地形モジュール(ページ2、セグメント 5: env.c)
  G  ゲームモジュール(ページ2、セグメント 1: game.c・models.c・stageN.c)
アセンブラのローカルラベル(1$ など)は「直前のラベル+1$」として出す。C の static 関数も .rst のラベルから拾える。
"""
import glob
import os
import re
import sys

# ページ0(自前の RAM)の中身(sys.s・raster.s・mkrom.py の配置)
PAGE0 = [
    (0x0000, '(page0)'),
    (0x0038, '(ISR)'),
    (0x0100, 'fill+展開ブロック'),
    (0x0200, '(page0 辺キャッシュ)'),
    (0x1A00, '(page0 表・メニュー)'),
]


def region_of(rst):
    b = os.path.basename(rst)
    if b == 'env.rst':
        return 'E'
    if b in ('game.rst', 'models.rst') or b.startswith('stage'):
        return 'G'
    return 'R'


def main(build, out):
    syms = {'R': {}, 'E': {}, 'G': {}}
    for reg, noi in (('R', 'engine.noi'), ('E', 'envmod.noi'), ('G', 'game1.noi')):
        for line in open(os.path.join(build, noi)):
            m = re.match(r'DEF (\S+) 0x([0-9A-Fa-f]+)', line)
            if m and not m.group(1).startswith(('s__', 'l__')):
                a = int(m.group(2), 16)
                lo, hi = (0x4000, 0x8000) if reg == 'R' else (0x8000, 0xC000)
                if lo <= a < hi:
                    syms[reg][a] = m.group(1)
    for rst in glob.glob(os.path.join(build, '*.rst')):
        reg = region_of(rst)
        lo, hi = (0x4000, 0x8000) if reg == 'R' else (0x8000, 0xC000)
        cur = None
        for line in open(rst, errors='replace'):
            m = re.match(r'\s+([0-9A-F]{8})\s+\d+\s+([A-Za-z_.]\w*):{1,2}', line)
            m2 = re.match(r'\s+([0-9A-F]{8})\s+\d+\s+(\d+\$):', line)
            if m:
                cur = m.group(2)
                a = int(m.group(1), 16)
                if lo <= a < hi:
                    syms[reg].setdefault(a, cur)
            elif m2 and cur:
                a = int(m2.group(1), 16)
                if lo <= a < hi:
                    syms[reg].setdefault(a, f'{cur}+{m2.group(2)}')
    for a, n in PAGE0:
        syms['R'][a] = n
    with open(out, 'w') as f:
        for reg in 'REG':
            for a in sorted(syms[reg]):
                f.write(f'{reg} {a:04X} {syms[reg][a]}\n')


if __name__ == '__main__':
    main(*sys.argv[1:])
