#!/usr/bin/env python3
"""check_models.py — 面のゲームモジュールから外した敵の絵が、その面に出ないかを確かめる

    python3 tools/check_models.py

models.c は面ごとに(GAME_S1・S2・S3・S5・S6・SEA)その面に出ない敵の絵を外す({ 0, 0 })。外した絵の敵が出ると、見えず
撃てない敵になって周回区間から抜けられなくなる。各面について
  - 台本(src/stage/stage*.c)の EV_SPAWN・EV_BOSS で出る種類
  - その種類が撃つ弾(game.c の edef の shot)
  - ボスが呼ぶ子分(game.c の「ce.type == ET_x ... spawn(ET_y」)
を集め、models.c をその面の定義でプリプロセッサに通した models[] の中身と突き合わせる。足りなければ 1 で終わる。
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENG = ROOT / 'src' / 'engine'
STAGES = {1: ('stage1_cave.c', 'GAME_S1'), 2: ('stage2_lake.c', 'GAME_S2'), 3: ('stage3_forest.c', 'GAME_S3'),
          4: ('stage4_bridge.c', 'GAME_SEA'), 5: ('stage5_town.c', 'GAME_S5'), 6: ('stage6_home.c', 'GAME_S6')}


def et_names():
    """game.h の敵の種類の enum(ET_NONE から ET_COUNT の手前まで)"""
    src = (ENG / 'game.h').read_text()
    body = src[src.index('enum { ET_NONE'):]
    body = body[:body.index('ET_COUNT')]
    body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
    return re.findall(r'ET_\w+', body)


def edef_shots(names):
    """game.c の edef の各行の shot(弾の種類)。行の順は enum の順"""
    src = (ENG / 'game.c').read_text()
    body = src[src.index('static const EDef edef[ET_COUNT]'):]
    body = body[:body.index('};')]
    rows = [l for l in body.splitlines() if l.strip().startswith('/*') and ('{' in l or 'EN_' in l)]
    shots = {}
    for name, row in zip(names, rows):
        m = re.findall(r'ET_\w+', row)
        if m:
            shots[name] = m[-1]
    return shots


def minions():
    """ボスが呼ぶ子分: 同じ行の「ce.type == ET_x」と「spawn(ET_y」"""
    src = (ENG / 'game.c').read_text()
    out = {}
    for line in src.splitlines():
        a = re.search(r'ce\.type == (ET_\w+)', line)
        b = re.search(r'spawn\((ET_\w+)', line)
        if a and b:
            out.setdefault(a.group(1), set()).add(b.group(1))
    # 大ダコ人は octo_arms で足(ET_TENT)を出す
    if 'octo_arms' in src:
        out.setdefault('ET_B07', set()).add('ET_TENT')
    return out


def present(define, names):
    """models.c を define 付きで SDCC のプリプロセッサ(sdcpp)に通し、models[] の各要素が絵を持つか"""
    cpp = subprocess.run(['sdcpp', '-P', '-D' + define, '-I' + str(ENG), str(ENG / 'models.c')],
                         capture_output=True, text=True, check=True).stdout
    body = cpp[cpp.index('const Model models[] ='):]
    body = body[body.index('{') + 1:body.index('};')]
    elems = re.findall(r'\{\s*0\s*,\s*0\s*\}|\{\s*m_\w+\s*,', body)
    return {n: not re.match(r'\{\s*0', e) for n, e in zip(names, elems)}


def main():
    names = et_names()
    shots = edef_shots(names)
    sub = minions()
    bad = 0
    for st, (fname, define) in STAGES.items():
        src = (ROOT / 'src' / 'stage' / fname).read_text()
        used = set(re.findall(r'EV_(?:SPAWN|BOSS)[^,]*,\s*(ET_\w+)', src))
        todo = list(used)
        while todo:
            t = todo.pop()
            for x in [shots.get(t)] + sorted(sub.get(t, ())):
                if x and x not in used:
                    used.add(x)
                    todo.append(x)
        have = present(define, names)
        miss = sorted(t for t in used if not have.get(t))
        print(f'stage {st} ({define}): 出る種類 {len(used)}、絵の無いもの {miss if miss else "無し"}')
        bad += len(miss)
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
