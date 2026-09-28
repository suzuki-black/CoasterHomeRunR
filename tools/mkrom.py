#!/usr/bin/env python3
"""ASCII8 メガロム(128KB = 8KB × 16 バンク)を組み立てる。配置は src/boot/boot.s の冒頭を参照。

  mkrom.py boot.ihx engine.ihx engine.noi out.rom env.ihx env.noi game1.ihx game1.noi ... game5.ihx game5.noi

- bank0     : ローダー(0x4000-0x5FFF)
- bank1     : ページ0に置く表とコード(エリア _PAGE0 = RAM 0x2C80-0x3FFF。起動時に sys_page0_data が読み込む)
- bank2-3   : エンジンの常駐部(RAM ページ1 0x4000-0x7FFF へローダーが写す)
- bank4-5   : 地形モジュール(0x8000 にリンク。起動時にマッパーのセグメント 5 へ読み込む)
- bank6-15  : ステージ 1〜5 のゲームモジュール(0x8000 にリンク。ステージの始めにページ2 = セグメント 1 へ読み込む)

モジュール(オーバーレイ、仕様書 7.3)はどれも 0x8000-0xBBFF にコードと定数、0xBC00-0xBFFF に変数(読み込み後に 0 で埋める。
初期値つきの変数は置けない)。常駐部の変数(DATA/BSS)が 0xCC00-0xDDFF に収まっているかも検査する。
"""
import re
import sys

BANK = 0x2000
ROM_BANKS = 16
DATA_BASE, DATA_LIMIT = 0xCC00, 0xDE00   # 0xDE00-0xDFFF は行の左右端(raster.s span_a/span_b)
CODE_LIMIT = 0x8000   # 常駐部はページ1だけ(ページ2はモジュール)
MOD_BASE, MOD_BSS, MOD_END = 0x8000, 0xBC00, 0xC000
ENV_BANK = 4
def GAME_BANK(n): return 4 + 2 * n
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




def check_module(name, ihx, noi):
    """モジュールのイメージ(16KB)を作る。コードと定数は 0x8000-0xBBFF、変数は 0xBC00-0xBFFF、初期値つきの変数は無いこと"""
    img = read_ihx(ihx)
    syms = read_noi(noi)
    lo, hi = min(img), max(img)
    if lo != MOD_BASE or hi >= MOD_BSS:
        sys.exit(f'mkrom: {name} のコードが 0x{MOD_BASE:04X}-0x{MOD_BSS - 1:04X} に収まっていない (0x{lo:04X}-0x{hi:04X})')
    dstart, heap = syms.get('s__DATA', 0), syms.get('s__HEAP', 0)
    if dstart < MOD_BSS or heap > MOD_END:
        sys.exit(f'mkrom: {name} の変数 0x{dstart:04X}-0x{heap:04X} が 0x{MOD_BSS:04X}-0x{MOD_END - 1:04X} に収まっていない')
    if syms.get('l__INITIALIZER', 0) or syms.get('l__GSINIT', 0):
        sys.exit(f'mkrom: {name} に初期値つきの変数がある(モジュールでは 0 で始まる変数しか使えない)')
    return bytes(img.get(MOD_BASE + i, 0xFF) for i in range(0x4000)), f'{name}={hi - MOD_BASE + 1}B+{heap - dstart}B'


def main(boot_ihx, eng_ihx, eng_noi, out_path, env_ihx, env_noi, *game_args):
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
        sys.exit(f'mkrom: エンジンの常駐部が 0x{CODE_LIMIT:04X} を超えた: 0x{max(code):04X}')
    image = bytes(eng.get(0x4000 + i, 0xFF) for i in range(0x4000))

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

    mods = []
    envimg, info = check_module('env', env_ihx, env_noi)
    rom[ENV_BANK * BANK:(ENV_BANK + 2) * BANK] = envimg
    mods.append(info)
    if len(game_args) % 2:
        sys.exit('mkrom: ゲームモジュールは (ihx, noi) の組で渡す')
    for k in range(len(game_args) // 2):
        n = k + 1
        bank = GAME_BANK(n)
        if bank + 1 >= ROM_BANKS:
            sys.exit(f'mkrom: ステージ {n} のバンク {bank} が範囲外')
        img, info = check_module(f'stage{n}', game_args[k * 2], game_args[k * 2 + 1])
        rom[bank * BANK:(bank + 2) * BANK] = img
        mods.append(info)
    with open(out_path, 'wb') as f:
        f.write(rom)
    print(f'mkrom: {out_path} {len(rom)} B (ASCII8) '
          f'loader {max(boot) - 0x4000 + 1} B, resident {max(code) - 0x4000 + 1} B / {CODE_LIMIT - 0x4000}, '
          f'page0 {p0d_len} B / {P0D_LIMIT - P0D_BASE}, '
          f'vars 0x{dstart:04X}-0x{heap:04X} ({heap - dstart} B / {DATA_LIMIT - DATA_BASE}), '
          f'modules {" ".join(mods)} (各 {MOD_BSS - MOD_BASE} B + {MOD_END - MOD_BSS} B まで)')


if __name__ == '__main__':
    main(*sys.argv[1:])
