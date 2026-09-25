#!/usr/bin/env python3
"""ASCII8 メガロム(128KB = 8KB × 16 バンク)を組み立てる。配置は src/boot/boot.s の冒頭を参照。

  mkrom.py boot.ihx engine.ihx engine.noi out.rom

- bank0     : ローダー(0x4000-0x5FFF)
- bank1     : 予備
- bank2-5   : エンジン(RAM 0x4000-0xBFFF へ写される)
- bank6-15  : 予備(ステージデータ等。常駐ローダーで読む)

変数(DATA/BSS)が 0xCC00-0xDFFF に収まっているかも検査する。
"""
import re
import sys

BANK = 0x2000
ROM_BANKS = 16
DATA_BASE, DATA_LIMIT = 0xCC00, 0xDE00   # 0xDE00-0xDFFF は行の左右端(raster.s span_a/span_b)
CODE_LIMIT = 0x9B00   # 0x9B00-0x9BFF は塗りの展開ブロック(raster.s FILLBLK)、0x9C00-0xBFFF は辺キャッシュ


def read_ihx(path):
    mem = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line.startswith(':'):
                continue
            n = int(line[1:3], 16)
            addr = int(line[3:7], 16)
            typ = int(line[7:9], 16)
            if typ == 1:
                break
            if typ != 0:
                continue
            for i, b in enumerate(bytes.fromhex(line[9:9 + n * 2])):
                mem[addr + i] = b
    return mem


def read_noi(path):
    syms = {}
    with open(path) as f:
        for line in f:
            m = re.match(r'DEF (\S+) 0x([0-9A-Fa-f]+)', line)
            if m:
                syms[m.group(1)] = int(m.group(2), 16)
    return syms


def main(boot_ihx, eng_ihx, eng_noi, out_path):
    boot = read_ihx(boot_ihx)
    if min(boot) < 0x4000 or max(boot) >= 0x6000:
        sys.exit('mkrom: ローダーが bank0(0x4000-0x5FFF)に収まっていない')
    bank0 = bytes(boot.get(0x4000 + i, 0xFF) for i in range(BANK))

    eng = read_ihx(eng_ihx)
    code = [a for a in eng if a < 0xC000 and not 0x9B00 <= a < 0x9C00]
    data = [a for a in eng if a >= 0xC000]
    if data:
        sys.exit(f'mkrom: エンジンのイメージに 0x{min(data):04X} 以降(RAM 変数域)のデータがある')
    if min(code) < 0x4000:
        sys.exit('mkrom: エンジンが 0x4000 より前にある')
    if max(code) >= CODE_LIMIT:
        sys.exit(f'mkrom: エンジンが 0x{CODE_LIMIT:04X} を超えた: 0x{max(code):04X}')
    if 0x9B00 not in eng:
        sys.exit('mkrom: 塗りの展開ブロック(0x9B00)が無い')
    image = bytes(eng.get(0x4000 + i, 0xFF) for i in range(0x8000))

    syms = read_noi(eng_noi)
    heap = syms.get('s__HEAP', 0)
    dstart = syms.get('s__DATA', 0)
    if dstart < DATA_BASE or heap > DATA_LIMIT:
        sys.exit(f'mkrom: 変数域 0x{dstart:04X}-0x{heap:04X} が 0x{DATA_BASE:04X}-0x{DATA_LIMIT:04X} を超えた')

    rom = bank0 + b'\xFF' * BANK + image
    rom += b'\xFF' * (ROM_BANKS * BANK - len(rom))
    with open(out_path, 'wb') as f:
        f.write(rom)
    print(f'mkrom: {out_path} {len(rom)} B (ASCII8) '
          f'loader {max(boot) - 0x4000 + 1} B, engine code {max(code) - 0x4000 + 1} B / {CODE_LIMIT - 0x4000}, '
          f'vars 0x{dstart:04X}-0x{heap:04X} ({heap - dstart} B / {DATA_LIMIT - DATA_BASE})')


if __name__ == '__main__':
    main(*sys.argv[1:])
