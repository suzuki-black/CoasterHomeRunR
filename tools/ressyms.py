#!/usr/bin/env python3
"""常駐部のシンボルを、モジュール(ページ2に読み込むゲーム・地形)のリンク用の asm にする。

  ressyms.py engine.noi out.s

engine.noi の DEF のうち、C から見える名前(_ で始まるもの)を `_name == 0xADDR` の大域シンボルとして出す。
モジュールはこれを一緒にリンクして、常駐部の関数・変数(ライブラリの __mulint なども)を番地で呼ぶ。
リンカが作る区画の名前(s__ / l__)は出さない。
"""
import re
import sys


def main(noi, out):
    lines = ['; tools/ressyms.py が engine.noi から生成。手で編集しない', '\t.module\tressyms']
    n = 0
    for line in open(noi):
        m = re.match(r'DEF (_\S+) 0x([0-9A-Fa-f]+)', line)
        if not m:
            continue
        name, addr = m.group(1), int(m.group(2), 16)
        if name.startswith(('s__', 'l__')):
            continue
        lines.append(f'{name} == 0x{addr:04X}')
        n += 1
    with open(out, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    print(f'ressyms: {n} シンボル')


if __name__ == '__main__':
    main(*sys.argv[1:])
