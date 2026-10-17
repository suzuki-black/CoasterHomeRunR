#!/usr/bin/env python3
"""FM 音源(MSX-MUSIC = YM2413)の曲を MML から作る。

  gen_music.py songs.mml out.bin songs.h

out.bin は音のモジュールの曲の部分(ROM のバンク 28-29。起動時に RAM のセグメント 8 へ読み込む。0x8000- はドライバのコード
(sndfm.s・sndtick.c)で、曲の表はその後ろの BASE から。tools/mkrom.py が両方を合わせる)。
songs.h は曲の番号(SONG_名前。1 から。0 = 無し)。鳴らすのは src/engine/sound.c。

■ MML(src/sound/songs.mml)
  # から行末まではコメント
  @song 名前 loop|once      曲の始まり(once = 最後まで鳴らしたら止まる。ジングル)
  @tempo 150                 4 分音符 / 分(曲の途中で変えるときは本文に t150)
  @voice 61 61 1E 06 72 61 25 16   オリジナル音色(音色 0、@0 で使う)。YM2413 のレジスタ 0x00〜0x07 の値(16 進)。
                             内蔵の音色だけだと YM2413 らしい音になりがちなので、どの曲にも書き、どれかのパートで @0 を使う。
                             何個でも書ける(書いた順に 0, 1, 2 …)。曲の始めは 0 番
    @v1                      (本文)ここからオリジナル音色を 1 番に替える。同時に持てるのは 1 つなので、@0 を使っている
                             全チャンネルの音色が替わる。場面の切れ目で替え、曲の頭に戻ったときのために旋律の頭に @v0 を置く
  A 〜 F で始まる行          メロディのチャンネル(出てきた順に FM の ch0〜5。同じ名前の行は続けて書ける)
  P で始まる行               PSG のパート(PSG の ch A。FM のパートにわずかにずらした音程で重ねて、YM2413 らしさを薄める)。
                             書き方はメロディと同じ(音色 @ は無し)。加えて D+8 / D-6 でデチューン(セント)、
                             E3 で 3 ティックごとに音量を 1 下げる(E0 = 下げない)
  R で始まる行               リズム(FM の ch6〜8 をリズムモードで使う)
  本文(メロディ):
    c d e f g a b            音符。+ か # で半音上げ、- で半音下げ。続けて長さ(4 = 4 分音符、付点 .)
    n76                      MIDI の音番号で(60 = 真ん中のド)
    r                        休み
    %12                      長さをティック(1/60 秒)で(c%12 など)
    o4 < >                   オクターブ(o4 の c = MIDI 60)
    l8                       これから書く音の長さ
    @3                       音色(YM2413 の内蔵 1〜15: 1 バイオリン 2 ギター 3 ピアノ 4 フルート 5 クラリネット
                             6 オーボエ 7 トランペット 8 オルガン 9 ホルン 10 シンセ 11 ハープシコード
                             12 ビブラフォン 13 シンセベース 14 ウッドベース 15 エレキギター)
    v12                      音量 0〜15(15 が最大)
    q6                       音を伸ばす長さ(8 分の q。8 = 次の音まで伸ばす)
    &                        タイ(次の同じ高さの音とつなげる)
    [ ... ]3                 3 回くり返す(入れ子にできる)
    L                        ループで戻る所(無ければ頭)
    t150                     テンポ
  本文(リズム): b s t c h(バスドラム・スネア・タム・シンバル・ハイハット)を並べた塊 + 長さ。例 bh8 h8 sh8 h8
    r 休み、l 長さ、[ ]、L、t は同じ。V15,15,12,12,10 で音量(バスドラム・スネア・タム・シンバル・ハイハット)

■ データ(音のモジュールの曲の部分。番地は BASE = 0x8C00 から)
  BASE: db 曲の数、dw 曲 …
  曲: db メロディのチャンネル数、db フラグ(bit0 リズム、bit1 オリジナル音色、bit2 PSG のパート)、(bit1 なら)db 音色 × 8、
      dw チャンネル …、(リズムがあれば)dw リズム、(PSG のパートがあれば)dw PSG のパート
  チャンネル(メロディ):
    0x00〜0x7F n, len        音(MIDI の音番号)を len ティック
    0x80 len                 休み
    0x81 x                   音色 << 4 | YM2413 の音量(0 = 最大、15 = 無音)
    0x82 g                   音の終わりの g ティック前に音を切る(0 = 切らない)
    0x83                     ループで戻る所
    0x85 len                 今の音をそのまま len ティック伸ばす(長い音・タイ)
    0x86 x × 8               オリジナル音色(レジスタ 0x00〜0x07)を書き換える
    0xFE                     終わり(止まる)
    0xFF                     ループで戻る所(無ければ頭)へ
  チャンネル(PSG のパート): 0x90|周期の上位 4 ビット, 周期の下位, len = 音(デチューン込みの周期)、0x81 v = 音量(0〜15)、
    0x88 d = d ティックごとに音量を 1 下げる、0x80・0x82・0x83・0x85・0xFE・0xFF はメロディと同じ
  チャンネル(リズム): 0x00〜0x1F の音 n, len(bit4 BD 3 SD 2 TOM 1 CYM 0 HH)、0x80 len 休み、
    0x84 a b c(音量レジスタ 0x36・0x37・0x38 の値)、0x83・0xFE・0xFF は同じ
"""
import re
import sys

BASE = 0x8C00           # 曲の表(sndfm.s の SONG_TAB。0x8000- はドライバのコード)
LIMIT = 0xBC00 - BASE   # モジュールの 0xBC00- は変数(読み込み後に 0 で埋められる)
PSG_CLOCK = 3579545 / 2
TICKS = 60.0            # 1 秒のティック数
DRUM_BITS = {'b': 0x10, 's': 0x08, 't': 0x04, 'c': 0x02, 'h': 0x01}
NOTE = {'c': 0, 'd': 2, 'e': 4, 'f': 5, 'g': 7, 'a': 9, 'b': 11}


def expand_repeats(s):
    """[ ... ]n を展開する(入れ子可)"""
    while '[' in s:
        m = re.search(r'\[([^\[\]]*)\](\d*)', s)
        if not m:
            raise SystemExit('gen_music: [ ] の対応が取れない: ' + s[:40])
        n = int(m.group(2) or 2)
        s = s[:m.start()] + ' '.join([m.group(1)] * n) + s[m.end():]
    return s


class Clock:
    """テンポの変化と付点を含む長さを、ずれが積もらないようにティックへ丸める"""
    def __init__(self, tempo):
        self.tempo = tempo
        self.t = 0.0          # 曲の頭からの正確な時刻(ティック)

    def whole(self):
        return 4 * 60.0 * TICKS / self.tempo

    def advance(self, ticks_exact):
        a = round(self.t)
        self.t += ticks_exact
        return round(self.t) - a


def parse_len(s, i, deflen, clock):
    """長さ(数字・付点・%ティック)を読んで (ちょうどの長さ, 次の位置) を返す"""
    if i < len(s) and s[i] == '%':
        m = re.match(r'%(\d+)', s[i:])
        return float(m.group(1)), i + len(m.group(0))
    m = re.match(r'(\d*)(\.*)', s[i:])
    n = int(m.group(1)) if m.group(1) else None
    if n is None:
        base = deflen
    else:
        base = clock.whole() / n
    d = base
    add = base
    for _ in m.group(2):
        add /= 2
        d += add
    return d, i + len(m.group(0))


def compile_melody(text, tempo, name, voices):
    s = expand_repeats(text)
    clock = Clock(tempo)
    octave, vol, inst, q = 4, 12, 3, 7
    deflen = clock.whole() / 8
    ev = []                   # ('note', midi, ticks) ('rest', ticks) ('iv', byte) ('gate', g) ('loop',)
    cur_iv = None
    cur_gate = None
    tie = False
    i = 0
    while i < len(s):
        ch = s[i]
        if ch.isspace() or ch == '|':
            i += 1
            continue
        if ch in NOTE or ch == 'n':
            if ch == 'n':
                m = re.match(r'n(\d+)', s[i:])
                midi = int(m.group(1))
                i += len(m.group(0))
            else:
                midi = (octave + 1) * 12 + NOTE[ch]
                i += 1
                while i < len(s) and s[i] in '+#-':
                    midi += -1 if s[i] == '-' else 1
                    i += 1
            exact, i = parse_len(s, i, deflen, clock)
            ticks = clock.advance(exact)
            j = i
            while j < len(s) and s[j].isspace():
                j += 1
            next_tie = j < len(s) and s[j] == '&'
            if next_tie:
                i = j + 1
            iv = (inst << 4) | (15 - vol)
            if iv != cur_iv:
                ev.append(('iv', iv))
                cur_iv = iv
            if tie and ev and ev[-1][0] in ('note', 'ext') and ev[-1][1] == midi:
                ev.append(('ext', midi, ticks))
            else:
                ev.append(('note', midi, ticks, q))
            tie = next_tie
            continue
        if ch == 'r':
            exact, i = parse_len(s, i + 1, deflen, clock)
            ev.append(('rest', clock.advance(exact)))
            tie = False
            continue
        m = re.match(r'@v(\d+)', s[i:])
        if m:
            k = int(m.group(1))
            if k >= len(voices):
                raise SystemExit('gen_music: %s: オリジナル音色 %d 番が無い(@voice は %d 個)' % (name, k, len(voices)))
            ev.append(('voice', voices[k]))
            i += len(m.group(0))
            continue
        m = re.match(r'D([+-]?\d+)', s[i:])
        if m:
            ev.append(('det', int(m.group(1))))
            i += len(m.group(0))
            continue
        m = re.match(r'E(\d+)', s[i:])
        if m:
            ev.append(('dec', int(m.group(1))))
            i += len(m.group(0))
            continue
        m = re.match(r'([olvqt@])(\d+)(\.*)', s[i:])
        if m:
            c, n = m.group(1), int(m.group(2))
            if c == 'o':
                octave = n
            elif c == 'l':
                d = clock.whole() / n
                add = d
                for _ in m.group(3):
                    add /= 2
                    d += add
                deflen = d
            elif c == 'v':
                vol = n
            elif c == 'q':
                q = n
            elif c == 't':
                clock.tempo = n
            elif c == '@':
                inst = n
            i += len(m.group(0))
            continue
        if ch == '<':
            octave -= 1
            i += 1
            continue
        if ch == '>':
            octave += 1
            i += 1
            continue
        if ch == 'L':
            ev.append(('loop',))
            i += 1
            continue
        raise SystemExit('gen_music: %s: 読めない文字 %r: %s' % (name, ch, s[max(0, i - 20):i + 20]))
    return ev


def melody_bytes(ev):
    """イベントをバイト列に。q(音を伸ばす長さ)は音ごとに、その音の長さから切る位置を計算して 0x82 で渡す"""
    out = []
    gate = None
    k = 0
    while k < len(ev):
        e = ev[k]
        if e[0] == 'iv':
            out += [0x81, e[1]]
        elif e[0] == 'loop':
            out += [0x83]
        elif e[0] == 'voice':
            out += [0x86] + list(e[1])
        elif e[0] == 'rest':
            t = e[1]
            while t > 0:
                n = min(t, 255)
                out += [0x80, n]
                t -= n
        elif e[0] == 'note':
            total = e[2]
            k2 = k + 1
            while k2 < len(ev) and ev[k2][0] == 'ext':
                total += ev[k2][2]
                k2 += 1
            q = e[3]
            g = 0 if q >= 8 else max(1, total - round(total * q / 8))
            if total <= 1:
                g = 0
            g = min(g, 255)
            if g != gate:
                out += [0x82, g]
                gate = g
            t = total
            n = min(t, 255)
            out += [e[1], n]
            t -= n
            while t > 0:
                n = min(t, 255)
                out += [0x85, n]
                t -= n
            k = k2
            continue
        k += 1
    return out


def psg_period(midi, cents):
    f = 440.0 * 2 ** ((midi - 69) / 12.0) * 2 ** (cents / 1200.0)
    return max(1, min(4095, round(PSG_CLOCK / 16 / f)))


def psg_bytes(ev):
    """PSG のパートのイベントをバイト列に(sndtick.c の psg_part)。音は周期(デチューン込み)を直接書く"""
    out = []
    gate = None
    det = 0
    k = 0
    while k < len(ev):
        e = ev[k]
        if e[0] == 'iv':
            out += [0x81, 15 - (e[1] & 15)]
        elif e[0] == 'det':
            det = e[1]
        elif e[0] == 'dec':
            out += [0x88, e[1]]
        elif e[0] == 'loop':
            out += [0x83]
        elif e[0] == 'rest':
            t = e[1]
            while t > 0:
                n = min(t, 255)
                out += [0x80, n]
                t -= n
        elif e[0] == 'note':
            total = e[2]
            k2 = k + 1
            while k2 < len(ev) and ev[k2][0] == 'ext':
                total += ev[k2][2]
                k2 += 1
            q = e[3]
            g = 0 if q >= 8 else max(1, total - round(total * q / 8))
            if total <= 1:
                g = 0
            g = min(g, 255)
            if g != gate:
                out += [0x82, g]
                gate = g
            per = psg_period(e[1], det)
            t = total
            n = min(t, 255)
            out += [0x90 | (per >> 8), per & 0xFF, n]
            t -= n
            while t > 0:
                n = min(t, 255)
                out += [0x85, n]
                t -= n
            k = k2
            continue
        elif e[0] == 'voice':
            raise SystemExit('gen_music: PSG のパートに @v は使えない')
        k += 1
    return out


def psg_ticks(b):
    t = 0
    i = 0
    while i < len(b):
        x = b[i]
        if (x & 0xF0) == 0x90:
            t += b[i + 2]
            i += 3
        elif x in (0x80, 0x85):
            t += b[i + 1]
            i += 2
        elif x in (0x81, 0x82, 0x88):
            i += 2
        else:
            i += 1
    return t


def compile_rhythm(text, tempo, name):
    s = expand_repeats(text)
    clock = Clock(tempo)
    deflen = clock.whole() / 8
    out = []
    i = 0
    while i < len(s):
        ch = s[i]
        if ch.isspace() or ch == '|':
            i += 1
            continue
        if ch in DRUM_BITS:
            bits = 0
            while i < len(s) and s[i] in DRUM_BITS:
                bits |= DRUM_BITS[s[i]]
                i += 1
            exact, i = parse_len(s, i, deflen, clock)
            t = clock.advance(exact)
            out += [bits, min(t, 255)]
            t -= min(t, 255)
            while t > 0:
                out += [0x80, min(t, 255)]
                t -= min(t, 255)
            continue
        if ch == 'r':
            exact, i = parse_len(s, i + 1, deflen, clock)
            t = clock.advance(exact)
            while t > 0:
                out += [0x80, min(t, 255)]
                t -= min(t, 255)
            continue
        m = re.match(r'V(\d+),(\d+),(\d+),(\d+),(\d+)', s[i:])
        if m:
            bd, sd, tom, cym, hh = (15 - int(x) for x in m.groups())
            out += [0x84, bd, (hh << 4) | sd, (tom << 4) | cym]
            i += len(m.group(0))
            continue
        m = re.match(r'([lt])(\d+)(\.*)', s[i:])
        if m:
            if m.group(1) == 'l':
                d = clock.whole() / int(m.group(2))
                add = d
                for _ in m.group(3):
                    add /= 2
                    d += add
                deflen = d
            else:
                clock.tempo = int(m.group(2))
            i += len(m.group(0))
            continue
        if ch == 'L':
            out.append(0x83)
            i += 1
            continue
        raise SystemExit('gen_music: %s: リズムで読めない文字 %r' % (name, ch))
    return out


def stream_ticks(b, rhythm):
    """チャンネルのバイト列の長さ(ティック)。ループで戻る所の前後を問わず全部"""
    t = 0
    i = 0
    while i < len(b):
        x = b[i]
        if x < 0x80:
            t += b[i + 1]
            i += 2
        elif x in (0x80, 0x85):
            t += b[i + 1]
            i += 2
        elif x in (0x81, 0x82):
            i += 2
        elif x == 0x84:
            i += 4
        elif x == 0x86:
            i += 9
        else:
            i += 1
    return t


def main(src, out_bin, out_h):
    songs = []          # (名前, loop, tempo, {ch: text}, 順)
    cur = None
    for raw in open(src, encoding='utf-8'):
        line = raw.split('#', 1)[0].rstrip()
        if not line.strip():
            continue
        m = re.match(r'@song\s+(\w+)\s+(loop|once)', line)
        if m:
            cur = {'name': m.group(1), 'loop': m.group(2) == 'loop', 'tempo': 120, 'ch': {}, 'order': []}
            songs.append(cur)
            continue
        m = re.match(r'@voice((?:\s+[0-9A-Fa-f]{2}){8})\s*$', line)
        if m:
            cur.setdefault('voices', []).append([int(x, 16) for x in m.group(1).split()])
            continue
        m = re.match(r'@tempo\s+(\d+)', line)
        if m:
            cur['tempo'] = int(m.group(1))
            continue
        m = re.match(r'([A-FRP])\s+(.*)', line)
        if m and cur is not None:
            c = m.group(1)
            if c not in cur['ch']:
                cur['ch'][c] = ''
                cur['order'].append(c)
            cur['ch'][c] += ' ' + m.group(2)
            continue
        raise SystemExit('gen_music: 読めない行: ' + line)

    blobs = []
    for sg in songs:
        mel = [c for c in sg['order'] if c not in 'RP']
        if len(mel) > 6:
            raise SystemExit('gen_music: %s: メロディは 6 チャンネルまで' % sg['name'])
        chans = []
        for c in mel:
            ev = compile_melody(sg['ch'][c], sg['tempo'], sg['name'] + ':' + c, sg.get('voices', []))
            chans.append(melody_bytes(ev) + [0xFF if sg['loop'] else 0xFE])
        if 'voices' not in sg or not any(re.search(r'@0(?!\d)', sg['ch'][c]) for c in mel):
            raise SystemExit('gen_music: %s: オリジナル音色(@voice と @0)を使っていない(どの曲も使う)' % sg['name'])
        rhy = None
        if 'R' in sg['ch']:
            rhy = compile_rhythm(sg['ch']['R'], sg['tempo'], sg['name'] + ':R') + [0xFF if sg['loop'] else 0xFE]
        psg = None
        if 'P' in sg['ch']:
            psg = psg_bytes(compile_melody(sg['ch']['P'], sg['tempo'], sg['name'] + ':P', [])) + [0xFF if sg['loop'] else 0xFE]
        blobs.append((sg['name'], chans, rhy, sg['voices'][0], psg))

    # くり返す曲は、どのチャンネルも同じ長さ(か、短い方が何回かまわってそろう長さ)でないと、回るたびにずれる
    for name, chans, rhy, _, psg in blobs:
        lens = [stream_ticks(c, False) for c in chans] + ([stream_ticks(rhy, True)] if rhy else []) + ([psg_ticks(psg)] if psg else [])
        sg = next(x for x in songs if x['name'] == name)
        if sg['loop'] and any(max(lens) % n for n in lens):
            raise SystemExit('gen_music: %s: チャンネルの長さ(ティック)がそろっていない(短い方が何回かで割り切れること): %s'
                             % (name, lens))
        print('  %-10s %s ティック(%.1f 秒)' % (name, lens[0] if len(set(lens)) == 1 else lens, max(lens) / TICKS))

    # 配置: 頭に曲の表、続けて曲ごとの見出しとチャンネル
    head = 1 + 2 * len(blobs)
    data = bytearray(head)
    data[0] = len(blobs)
    for k, (name, chans, rhy, voice, psg) in enumerate(blobs):
        hdr_at = len(data)
        data[1 + 2 * k:3 + 2 * k] = (BASE + hdr_at).to_bytes(2, 'little')
        streams = chans + ([rhy] if rhy else []) + ([psg] if psg else [])
        hdr = bytearray([len(chans), (1 if rhy else 0) | 2 | (4 if psg else 0)]) + bytes(voice) + bytes(2 * len(streams))
        data += hdr
        hdr_at += 8                     # チャンネルの番地はオリジナル音色の後(メロディ、リズム、PSG のパートの順)
        ptrs = []
        for c in streams:
            ptrs.append(BASE + len(data))
            data += bytes(c)
        for j, p in enumerate(ptrs):
            data[hdr_at + 2 + 2 * j:hdr_at + 4 + 2 * j] = p.to_bytes(2, 'little')
    if len(data) > LIMIT:
        raise SystemExit('gen_music: 音のモジュールが %d バイトで、%d を超えた' % (len(data), LIMIT))
    open(out_bin, 'wb').write(bytes(data))
    with open(out_h, 'w') as f:
        f.write('/* songs.h — tools/gen_music.py が src/sound/songs.mml から作る。手で直さない */\n')
        f.write('#ifndef SONGS_H\n#define SONGS_H\n')
        for k, (name, _, _, _, _) in enumerate(blobs):
            f.write('#define SONG_%s %d\n' % (name, k + 1))   # 0 = 無し
        f.write('#define SONG_COUNT %d\n#endif\n' % len(blobs))
    print('gen_music: %d 曲、%d バイト' % (len(blobs), len(data)))


if __name__ == '__main__':
    main(*sys.argv[1:])
