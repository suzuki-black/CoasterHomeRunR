#!/bin/sh
# 誘導弾の声「Fire!」を作り、src/engine/voice_fire.s(ゲームモジュールに入れる 8bit unsigned、3,996Hz)へ書く。
#   sh tools/gen_voice.sh
#   mac の読み上げ(say)を使うので、生成したファイルはリポジトリに入れておく(ビルドのたびには作らない)。
#   声は Junior、速さ 300(2026-10-10 にユーザーが選んだ。0.31 秒)。検証は tools/lineirq192/ と docs/検証記録.md
set -e
cd "$(dirname "$0")/.."
OUT=build/voice
mkdir -p "$OUT"
say -v Junior -r 300 -o "$OUT/fire.aiff" "Fire!"
afconvert -f WAVE -d LEI16@3996 -c 1 "$OUT/fire.aiff" "$OUT/fire.wav"
python3 - "$OUT/fire.wav" src/engine/voice_fire.s <<'PY'
import struct, sys, wave
w = wave.open(sys.argv[1]); n = w.getnframes(); d = struct.unpack('<%dh' % n, w.readframes(n))
m = max(abs(x) for x in d); th = m * 0.04
i0 = next(i for i, x in enumerate(d) if abs(x) > th)
i1 = n - next(i for i, x in enumerate(reversed(d)) if abs(x) > th)
s = d[max(0, i0 - 8):i1 + 8]
b = [max(1, min(255, int(round(128 + x * 127.0 / m)))) for x in s]   # 8bit unsigned、中心 0x80
with open(sys.argv[2], 'w') as f:
    f.write('; voice_fire.s — 誘導弾の声「Fire!」(tools/gen_voice.sh が作る。手で直さない)\n')
    f.write(';   8bit unsigned(中心 0x80)、3,996Hz(走査線 3.9375 行ごと)、%d サンプル = %.2f 秒。\n' % (len(b), len(b) / 3996))
    f.write(';   ゲームモジュールに入れる(割込みは BGM と同じく、ページ2をゲームのセグメントにして読む)\n')
    f.write('\t.module\tvoice_fire\n\t.area\t_CODE\n')
    f.write('_voice_fire_len::\n\t.dw\t%d\n' % len(b))
    f.write('_voice_fire::\n')
    for i in range(0, len(b), 16):
        f.write('\t.db\t' + ','.join('0x%02X' % v for v in b[i:i + 16]) + '\n')
print('src/engine/voice_fire.s: %d サンプル' % len(b))
PY
