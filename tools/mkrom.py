#!/usr/bin/env python3
"""ASCII8 メガロム(128KB = 8KB × 16 バンク)を組み立てる。配置は src/boot/boot.s の冒頭を参照。

  mkrom.py boot.ihx engine.ihx engine.noi out.rom [--stage バンク stage.ihx ...]

- bank0     : ローダー(0x4000-0x5FFF)
- bank1     : ページ0に置く表とコード(エリア _PAGE0 = RAM 0x2C80-0x3FFF。起動時に sys_page0_data が読み込む)
- bank2-5   : エンジン(RAM 0x4000-0xBFFF へ写される)
- bank6-15  : ステージデータ(--stage。1 ステージ 2 バンク。0xB400-0xBFFF の 3KB を常駐ローダーが RAM へ写す)

変数(DATA/BSS)が 0xCC00-0xDFFF に収まっているかも検査する。
"""
import re
import sys

BANK = 0x2000
ROM_BANKS = 16
DATA_BASE, DATA_LIMIT = 0xCC00, 0xDE00   # 0xDE00-0xDFFF は行の左右端(raster.s span_a/span_b)
CODE_LIMIT = 0xB400   # 0xB400-0xBFFF はステージデータの読み込み先(塗りの展開ブロックと辺キャッシュはページ0)
P0D_BASE, P0D_LIMIT = 0x2C80, 0x4000   # ページ0の表とコード(0x1A00-0x2C7F は地形の表。読み込みは 0xB400-0xCBFF 経由)


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


STAGE_BASE, STAGE_SIZE = 0xB400, 0x0C00


def main(boot_ihx, eng_ihx, eng_noi, out_path, *stage_args):
    boot = read_ihx(boot_ihx)
    if min(boot) < 0x4000 or max(boot) >= 0x6000:
        sys.exit('mkrom: ローダーが bank0(0x4000-0x5FFF)に収まっていない')
    bank0 = bytes(boot.get(0x4000 + i, 0xFF) for i in range(BANK))

    eng = read_ihx(eng_ihx)
    p0d = [a for a in eng if a < 0x4000]
    code = [a for a in eng if 0x4000 <= a < 0xC000]
    data = [a for a in eng if a >= 0xC000]
    if data:
        sys.exit(f'mkrom: エンジンのイメージに 0x{min(data):04X} 以降(RAM 変数域)のデータがある')
    if p0d and (min(p0d) != P0D_BASE or max(p0d) >= P0D_LIMIT):
        sys.exit(f'mkrom: ページ0の表が 0x{P0D_BASE:04X}-0x{P0D_LIMIT - 1:04X} に収まっていない '
                 f'(0x{min(p0d):04X}-0x{max(p0d):04X})')
    if max(code) >= CODE_LIMIT:
        sys.exit(f'mkrom: エンジンが 0x{CODE_LIMIT:04X} を超えた: 0x{max(code):04X}')
    image = bytes(eng.get(0x4000 + i, 0xFF) for i in range(0x8000))

    syms = read_noi(eng_noi)
    heap = syms.get('s__HEAP', 0)
    dstart = syms.get('s__DATA', 0)
    if dstart < DATA_BASE or heap > DATA_LIMIT:
        sys.exit(f'mkrom: 変数域 0x{dstart:04X}-0x{heap:04X} が 0x{DATA_BASE:04X}-0x{DATA_LIMIT:04X} を超えた')

    p0d_len = syms.get('l__PAGE0', 0)
    if p0d and max(p0d) - P0D_BASE + 1 != p0d_len:
        sys.exit('mkrom: ページ0の表の大きさ(l__PAGE0)がイメージと合わない')
    bank1 = bytes(eng.get(P0D_BASE + i, 0xFF) for i in range(BANK))
    rom = bytearray(bank0 + bank1 + image)
    rom += b'\xFF' * (ROM_BANKS * BANK - len(rom))

    # ステージ: 読み込み先 0xB400-0xBFFF に収まっていること、先頭 2 バイトが Stage へのポインタ
    stages = []
    args = list(stage_args)
    while args:
        if args[0] != '--stage' or len(args) < 3:
            sys.exit('mkrom: 引数は --stage バンク stage.ihx')
        bank, path = int(args[1]), args[2]
        args = args[3:]
        st = read_ihx(path)
        lo, hi = min(st), max(st)
        if lo != STAGE_BASE or hi >= STAGE_BASE + STAGE_SIZE:
            sys.exit(f'mkrom: {path} が 0x{STAGE_BASE:04X}-0x{STAGE_BASE + STAGE_SIZE - 1:04X} に収まっていない '
                     f'(0x{lo:04X}-0x{hi:04X}。変数は置けない)')
        if bank < 6 or bank + 1 >= ROM_BANKS:
            sys.exit(f'mkrom: ステージのバンク {bank} が範囲外')
        data = bytes(st.get(STAGE_BASE + i, 0xFF) for i in range(STAGE_SIZE))
        rom[bank * BANK:bank * BANK + STAGE_SIZE] = data
        stages.append(f'bank{bank}={hi - STAGE_BASE + 1}B')
    with open(out_path, 'wb') as f:
        f.write(rom)
    print(f'mkrom: {out_path} {len(rom)} B (ASCII8) '
          f'loader {max(boot) - 0x4000 + 1} B, engine code {max(code) - 0x4000 + 1} B / {CODE_LIMIT - 0x4000}, '
          f'page0 tables {p0d_len} B / {P0D_LIMIT - P0D_BASE}, '
          f'vars 0x{dstart:04X}-0x{heap:04X} ({heap - dstart} B / {DATA_LIMIT - DATA_BASE}), '
          f'stages {" ".join(stages) or "-"} (各 {STAGE_SIZE} B まで)')


if __name__ == '__main__':
    main(*sys.argv[1:])
