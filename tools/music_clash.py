#!/usr/bin/env python3
"""曲(src/sound/songs.mml)の旋律と伴奏のぶつかりを数える(「音痴」に聞こえる所を探す)。

  python3 tools/music_clash.py 曲の名前 [属七の和音の小節 …]

16 分音符ごとに縦に切り、旋律(A のチャンネル)の音のうち 8 分音符以上の長さのもの、または拍の頭のものについて、
同時に鳴っている他のチャンネルの音との 短 2 度・長 7 度・増 4 度 を出す(速い経過音のぶつかりは数えない)。
属七の和音の小節を引数で渡すと、その小節の増 4 度(和音の性質)は数えない。小節は 4/4 として数える。
"""
import re
import sys

sys.path.insert(0, 'tools')
import gen_music as g   # noqa: E402

BAD = {1: '短2度', 11: '長7度', 6: '増4度'}
NAMES = ['C', 'C#', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'Ab', 'A', 'Bb', 'B']


def main(song, *dom_bars):
    dom = {int(a) for a in dom_bars}
    src = open('src/sound/songs.mml', encoding='utf-8').read()
    cur = None
    chs, order, voices, tempo = {}, [], [], 120
    for raw in src.split('\n'):
        line = raw.split('#', 1)[0].rstrip()
        if not line.strip():
            continue
        m = re.match(r'@song\s+(\w+)', line)
        if m:
            cur = m.group(1)
            continue
        if cur != song:
            continue
        m = re.match(r'@tempo\s+(\d+)', line)
        if m:
            tempo = int(m.group(1))
            continue
        m = re.match(r'@voice((?:\s+[0-9A-Fa-f]{2}){8})', line)
        if m:
            voices.append([int(x, 16) for x in m.group(1).split()])
            continue
        m = re.match(r'([A-F])\s+(.*)', line)
        if m:
            chs.setdefault(m.group(1), '')
            chs[m.group(1)] += ' ' + m.group(2)
            if m.group(1) not in order:
                order.append(m.group(1))
    if not order:
        sys.exit('music_clash: 曲 %s が無い' % song)
    tl = {}
    for c in order:
        t, seq = 0, []
        for e in g.compile_melody(chs[c], tempo, c, voices):
            if e[0] == 'note':
                seq.append((t, t + e[2], e[1]))
                t += e[2]
            elif e[0] == 'ext':
                seq[-1] = (seq[-1][0], seq[-1][1] + e[2], seq[-1][2])
                t += e[2]
            elif e[0] == 'rest':
                t += e[1]
        tl[c] = seq
    s16 = 4 * 60.0 * g.TICKS / tempo / 16      # 16 分音符のティック
    end = max(s[-1][1] for s in tl.values() if s)
    n = 0
    for k in range(int(end / s16)):
        t = round(k * s16)
        bar = k // 16 + 1
        mel = [x for (a, b, x) in tl.get('A', []) if a <= t < b and ((b - a) >= 2 * s16 - 1 or k % 4 == 0)]
        for m in mel:
            for c in order:
                if c == 'A':
                    continue
                for x in [x for (a, b, x) in tl[c] if a <= t < b]:
                    iv = (m - x) % 12
                    if iv in BAD and abs(m - x) < 30 and not (iv == 6 and bar in dom):
                        print('小節 %2d 拍 %.2f: 旋律 %s%d と %s の %s%d が %s' % (
                            bar, (k % 16) / 4 + 1, NAMES[m % 12], m // 12 - 1, c, NAMES[x % 12], x // 12 - 1, BAD[iv]))
                        n += 1
    print('music_clash: %s のぶつかり %d' % (song, n))


if __name__ == '__main__':
    main(*sys.argv[1:])
