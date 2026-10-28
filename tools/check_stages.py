#!/usr/bin/env python3
"""check_stages.py — ステージの台本・レール・地形が周回区間の決まりを守っているかを確かめる

    python3 tools/check_stages.py

各面(src/stage/stage*.c)について:
  1. 位置で進める命令(EV_AT)が位置の順に並んでいる(台本は先頭から順に実行するので、前の命令の位置まで後ろの命令も待つ)
  2. 周回区間(Stage.loops)の始まりと終わりの前後 24m のレールがまっすぐで平ら(戻ったときに景色が変わらないように)
  3. 周回区間の始まりと終わりの前後 6 本(LOOP_K)のリングの地形の種類が同じ(地形モジュールが終わりの側へ写すので、
     違うと見せ場のリングが消える)
  4. 周回区間の曲がりの合計が 1 周の倍数(空や遠景の向きが戻ったときに変わらないように)
  4'. 波の命令(WAVE)が周回区間の中にある(直前の位置の命令が周回区間の中)
  5. レールが 800m(RAIL_MAX)、リングが 192 本(MAX_RINGS)に収まる
を確かめ、違反があれば 1 で終わる。
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENG = ROOT / 'src' / 'engine'
STAGE_FILES = sorted((ROOT / 'src' / 'stage').glob('stage*.c'))
LOOP_K = 6
RING = 4
RAIL_MAX_M = 800
MAX_RINGS = 192


def preprocess(path):
    # songs.h は build に生成される(Makefile の -I$(BUILD) と同じ)
    return subprocess.run(['sdcpp', '-P', '-I' + str(ENG), '-I' + str(ROOT / 'build'), str(path)],
                          capture_output=True, text=True, check=True).stdout


def c_int(expr):
    expr = expr.strip()
    if not re.fullmatch(r'[-+*/ ()0-9xXa-fA-F]+', expr):
        raise ValueError(expr)
    return int(eval(expr.replace('/', '//'), {}))


def array_body(src, name):
    m = re.search(r'\b' + re.escape(name) + r'\s*\[\s*\]\s*=\s*\{', src)
    if not m:
        return None
    i = m.end()
    depth = 1
    j = i
    while depth:
        if src[j] == '{':
            depth += 1
        elif src[j] == '}':
            depth -= 1
        j += 1
    return src[i:j - 1]


def rail(src, name):
    """RailSeg の列 → [(長さ, ヨー角の変化, 高さの変化)]。長さ 0 で終わり"""
    out = []
    for m in re.finditer(r'\{([^{}]*)\}', array_body(src, name)):
        f = [c_int(x) for x in m.group(1).split(',')]
        if f[0] == 0:
            break
        out.append((f[0], f[1], f[2]))
    return out


def stage_struct(src):
    body = src[src.index('const Stage stage_data'):]
    body = body[body.index('{') + 1:body.rindex('}')]
    return body


def main():
    errors = 0

    def err(msg):
        nonlocal errors
        errors += 1
        print('  NG: ' + msg)

    for path in STAGE_FILES:
        print(path.name)
        src = preprocess(path)
        st = stage_struct(src)
        names = re.findall(r'\b([a-z]+_(?:rail|left|right|tail|zones|loops|events))\b', st)
        track = next(n for n in names if n.endswith('_rail'))
        segs = rail(src, track)
        tail = next((n for n in names if n.endswith('_tail')), None)
        left = next((n for n in names if n.endswith('_left')), None)
        if tail:
            segs = segs + rail(src, left) + rail(src, tail)   # 分岐の左右は同じ長さ・同じ形(鏡写し)なので左で数える
        # 1 m ごとのヨー角の変化と高さの変化
        per_m = []
        for ln, dyaw, dy in segs:
            per_m += [(dyaw / ln, dy)] * ln
        length = len(per_m)
        if length > RAIL_MAX_M:
            err(f'レールが {length}m(最大 {RAIL_MAX_M}m)')
        # 地形
        zb = [c_int(x) if re.fullmatch(r'\s*[-0-9xXa-fA-F]+\s*', x) else x.strip()
              for x in array_body(src, next(n for n in names if n.endswith('_zones'))).split(',') if x.strip()]
        zones = zb[1:]
        nrings = length // RING + 2

        def kind(r):
            for i in range(0, len(zones), 2):
                if r < zones[i]:
                    return zones[i + 1]
            return zones[-1]
        if nrings > MAX_RINGS:
            err(f'リングが {nrings} 本(最大 {MAX_RINGS})')
        # 周回区間
        lname = next((n for n in names if n.endswith('_loops')), None)
        loops = []
        if lname:
            vals = [c_int(x) for x in array_body(src, lname).split(',') if x.strip()]
            vals = vals[:vals.index(0)]
            loops = list(zip(vals[0::2], vals[1::2]))
        for a, n in loops:
            sa, sb = a * RING, (a + n) * RING
            for name, s in (('始まり', sa), ('終わり', sb)):
                for m in range(s - 24, s + 24):
                    if m < 0 or m >= length or per_m[m] != (0, 0):
                        err(f'周回 {sa}〜{sb}m の{name}の前後 24m がまっすぐ・平らでない({m}m)')
                        break
            turn = sum(per_m[m][0] for m in range(sa, sb))
            if round(turn) % 65536:
                err(f'周回 {sa}〜{sb}m の曲がりの合計が 1 周の倍数でない({turn:.0f})')
            for i in range(-LOOP_K, LOOP_K):
                if kind(a + i) != kind(a + n + i):
                    err(f'周回 {sa}〜{sb}m: リング {a + n + i} の地形 {kind(a + n + i)} が始まりの側({kind(a + i)})と違う'
                        '(写すと消える)')
                    break
        # 台本の順番
        ev = array_body(src, next(n for n in names if n.endswith('_events')))
        ats = []
        waves = 0
        for m in re.finditer(r'\{([^{}]*)\}', ev):
            f = [x.strip() for x in m.group(1).split(',')]
            cmd = f[1]
            if '0x80' in cmd or 'EV_AT' in cmd or re.search(r'\|\s*128', cmd):
                ats.append(c_int(f[0]))
            elif f[0] == '0':
                # 周回区間の波(WAVE): 直前の位置の命令が周回区間の中にあること(外だと、前の命令に続けてその場で出てしまう)
                waves += 1
                last = ats[-1] if ats else -1
                if not any(a * RING <= last < (a + n) * RING for a, n in loops):
                    err(f'波の命令が周回区間の外にある(直前の位置の命令 {last}m)')
        for i in range(len(ats) - 1):
            if ats[i + 1] < ats[i]:
                err(f'台本の順番: {ats[i]}m の後に {ats[i + 1]}m')
        print(f'  レール {length}m、リング {nrings} 本、周回 {[(a * RING, (a + n) * RING) for a, n in loops]}、'
              f'位置の命令 {len(ats)} 個、波の命令 {waves} 個')
    print('OK' if not errors else f'違反 {errors} 件')
    sys.exit(1 if errors else 0)


if __name__ == '__main__':
    main()
