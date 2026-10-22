#!/usr/bin/env python3
"""ASCII8 メガロム(512KB = 8KB × 64 バンク)を組み立てる。配置は src/boot/boot.s の冒頭を参照。

  mkrom.py boot.ihx engine.ihx engine.noi out.rom env1.ihx env1.noi ... env6.ihx env6.noi game1.ihx game1.noi ... game6.ihx game6.noi
           snd.ihx snd.noi songs.bin voicemod.bin title.bin

- bank0     : ローダー(0x4000-0x5FFF)
- bank1     : ページ0に置く表とコード(エリア _PAGE0 = RAM 0x1A00-0x3FFF。起動時に sys_page0_data が読み込む)
- bank2-3   : エンジンの常駐部(RAM ページ1 0x4000-0x7FFF へローダーが写す)
- bank4-5   : ステージ 1 の地形モジュール(0x8000 にリンク。ステージの始めにマッパーのセグメント 5 へ読み込む)
- bank6-17  : ステージ 1〜6 のゲームモジュール(0x8000 にリンク。ステージの始めにページ2 = セグメント 1 へ読み込む)
- bank18-27 : ステージ 2〜6 の地形モジュール(地形モジュールはステージごとに、その面の地形の処理だけを入れる)
- bank28-29 : 音のモジュール(0x8000- に効果音・PSG・FM の曲のドライバ(sndtick.c・sndfm.s)、SONG_BASE- に曲(tools/gen_music.py)。
              起動時にセグメント 8 へ読み込む)
- bank30-31 : 声のモジュール(内蔵 PCM の英語の声。tools/gen_voice.sh が作る src/sound/voicemod.bin。起動時にセグメント 9 へ)
- bank32-39 : タイトルの絵(SCREEN 11。tools/gen_title.py が作る title.bin。タイトル画面で 16KB ずつ RAM へ写して VRAM へ)
- bank40-63 : 空き

モジュール(オーバーレイ、仕様書 7.3)はどれも 0x8000-0xBBFF にコードと定数、0xBC00-0xBFFF に変数(読み込み後に 0 で埋める。
初期値つきの変数は置けない)。常駐部の変数(DATA/BSS)が 0xCC00-0xDDFF に収まっているかも検査する。
"""
import re
import sys

BANK = 0x2000
ROM_BANKS = 64
TITLE_BANK = 32                          # タイトルの絵(main.c の TITLE_BANK)
SND_BANK = 28
VOICE_BANK = 30
DATA_BASE, DATA_LIMIT = 0xCC00, 0xDE00   # 0xDE00-0xDFFF は行の左右端(raster.s span_a/span_b)
CODE_LIMIT = 0x8000   # 常駐部はページ1だけ(ページ2はモジュール)
MOD_BASE, MOD_BSS, MOD_END = 0x8000, 0xBC00, 0xC000
STAGES = 6
def ENV_BANK(n): return 4 if n == 1 else 14 + 2 * n     # game.h の ENV_BANK
def GAME_BANK(n): return 4 + 2 * n
P0D_BASE, P0D_LIMIT = 0x1A00, 0x2E00   # ページ0の表とコード(0x0200-0x19FF は辺キャッシュ、0x2E00-0x3FFF は地形の断面の表)
P0D_STAGE = 0x1800                      # 読み込みは 0xB400-0xCBFF 経由なので 6KB まで
ENV_CODE_LIMIT = 0xB600                 # 地形モジュールのコード(0xB600-0xBBFF は地形の色・飾り・種類の表)
SONG_BASE = 0x8C00                      # 音のモジュールの曲の表(tools/gen_music.py の BASE、sndfm.s の SONG_TAB)


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


def main(boot_ihx, eng_ihx, eng_noi, out_path, *game_args):
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
    if p0d_len > P0D_STAGE:
        sys.exit(f'mkrom: ページ0の表とコードが {P0D_STAGE} B(読み込みの一時置き場)を超えた: {p0d_len} B')
    bank1 = bytes(eng.get(P0D_BASE + i, 0xFF) for i in range(BANK))
    rom = bytearray(bank0 + bank1 + image)
    rom += b'\xFF' * (ROM_BANKS * BANK - len(rom))

    mods = []
    used = {}
    def place(bank, img, name):
        for b in (bank, bank + 1):
            if b in used or b < 4 or b >= SND_BANK:
                sys.exit(f'mkrom: {name} のバンク {b} が {used.get(b, "予約済みの領域")} に重なった')
            used[b] = name
        rom[bank * BANK:(bank + 2) * BANK] = img
    env_args, game_args = game_args[:STAGES * 2], game_args[STAGES * 2:]
    for k in range(STAGES):
        n = k + 1
        env_ihx, env_noi = env_args[k * 2], env_args[k * 2 + 1]
        envimg, info = check_module(f'env{n}', env_ihx, env_noi)
        env_hi = max(a for a in read_ihx(env_ihx))
        if env_hi >= ENV_CODE_LIMIT:
            sys.exit(f'mkrom: ステージ {n} の地形モジュールのコードが 0x{ENV_CODE_LIMIT:04X}(地形の表)に食い込んだ: 0x{env_hi:04X}')
        place(ENV_BANK(n), envimg, f'env{n}')
        mods.append(f'env{n}={env_hi - MOD_BASE + 1}B/{ENV_CODE_LIMIT - MOD_BASE}')
    snd_ihx, snd_noi, songs_bin, voice_bin, title_bin = game_args[-5:]
    game_args = game_args[:-5]
    if len(game_args) % 2:
        sys.exit('mkrom: ゲームモジュールは (ihx, noi) の組で渡す')
    for k in range(len(game_args) // 2):
        n = k + 1
        img, info = check_module(f'stage{n}', game_args[k * 2], game_args[k * 2 + 1])
        place(GAME_BANK(n), img, f'stage{n}')
        mods.append(info)
    sndimg, info = check_module('snd', snd_ihx, snd_noi)
    snd_hi = max(a for a in read_ihx(snd_ihx))
    if snd_hi >= SONG_BASE:
        sys.exit(f'mkrom: 音のモジュールのドライバが 0x{SONG_BASE:04X}(曲の表)に食い込んだ: 0x{snd_hi:04X}')
    songs = open(songs_bin, 'rb').read()
    if SONG_BASE + len(songs) > MOD_BSS:
        sys.exit(f'mkrom: 曲が {len(songs)} B で、0x{SONG_BASE:04X}-0x{MOD_BSS - 1:04X} に収まらない')
    sndimg = bytearray(sndimg)
    sndimg[SONG_BASE - MOD_BASE:SONG_BASE - MOD_BASE + len(songs)] = songs
    rom[SND_BANK * BANK:(SND_BANK + 2) * BANK] = bytes(sndimg)
    mods.append(f'snd={snd_hi - MOD_BASE + 1}B+songs={len(songs)}B')
    voice = open(voice_bin, 'rb').read()
    if len(voice) > MOD_BSS - MOD_BASE:
        sys.exit(f'mkrom: 声のモジュールが {len(voice)} B で、{MOD_BSS - MOD_BASE} B を超えた')
    rom[VOICE_BANK * BANK:VOICE_BANK * BANK + len(voice)] = voice
    mods.append(f'voice={len(voice)}B')
    title = open(title_bin, 'rb').read()
    if len(title) > 8 * BANK:
        sys.exit(f'mkrom: タイトルの絵が {len(title)} B で、バンク {TITLE_BANK}〜{TITLE_BANK + 7} に収まらない')
    rom[TITLE_BANK * BANK:TITLE_BANK * BANK + len(title)] = title
    mods.append(f'title={len(title)}B')
    with open(out_path, 'wb') as f:
        f.write(rom)
    print(f'mkrom: {out_path} {len(rom)} B (ASCII8) '
          f'loader {max(boot) - 0x4000 + 1} B, resident {max(code) - 0x4000 + 1} B / {CODE_LIMIT - 0x4000}, '
          f'page0 {p0d_len} B / {P0D_LIMIT - P0D_BASE}, '
          f'vars 0x{dstart:04X}-0x{heap:04X} ({heap - dstart} B / {DATA_LIMIT - DATA_BASE}), '
          f'modules {" ".join(mods)} (各 {MOD_BSS - MOD_BASE} B + {MOD_END - MOD_BSS} B まで)')


if __name__ == '__main__':
    main(*sys.argv[1:])
