#!/usr/bin/env python3
"""pcprof.tcl 用のシンボル表を作る: .noi のグローバルと、.map / .lst のローカルラベル。

  prof_syms.py build/engine.noi build out.txt

アセンブラのソースは .rel と同じ名前の .lst(sdasz80 -l)からローカルラベル(1$ など)の番地を拾い、
「関数名+ラベル」として出す。C の関数は .noi のグローバル名だけ。
"""
import glob
import os
import re
import sys


def main(noi, build, out):
    syms = {}
    for line in open(noi):
        m = re.match(r'DEF (\S+) 0x([0-9A-Fa-f]+)', line)
        if m and not m.group(1).startswith(('s__', 'l__')):
            a = int(m.group(2), 16)
            if 0x4000 <= a < 0xC000:
                syms[a] = m.group(1)
    # .rst(リンク後の番地が入ったリスト)からローカルラベルを拾う
    for rst in glob.glob(os.path.join(build, '*.rst')):
        cur = None
        for line in open(rst, errors='replace'):
            m = re.match(r'\s+([0-9A-F]{8})\s+\d+\s+([A-Za-z_.]\w*):{1,2}', line)
            m2 = re.match(r'\s+([0-9A-F]{8})\s+\d+\s+(\d+\$):', line)
            if m:
                cur = m.group(2)
                a = int(m.group(1), 16)
                if 0x4000 <= a < 0xC000:
                    syms.setdefault(a, cur)
            elif m2 and cur:
                a = int(m2.group(1), 16)
                if 0x4000 <= a < 0xC000:
                    syms.setdefault(a, f'{cur}+{m2.group(2)}')
    with open(out, 'w') as f:
        for a in sorted(syms):
            f.write(f'{a:04X} {syms[a]}\n')


if __name__ == '__main__':
    main(*sys.argv[1:])
