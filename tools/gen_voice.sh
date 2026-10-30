#!/bin/sh
# 声(内蔵 PCM で鳴らす英語の声)を作り、声のモジュール src/sound/voicemod.bin(ROM のバンク 30-31。起動時にセグメント 9 へ)へ書く。
#   sh tools/gen_voice.sh
#   読み上げは flite(Carnegie Mellon University の Language Technologies Institute。BSD に似た許諾で、作った音は自由に配れる。
#   条件は著作権表示を残すこと = README のクレジット)。brew install flite。生成したファイルはリポジトリに入れておく(ビルドのたびには作らない)。
#   声は cmu_us_slt、速さは 1.3 倍(duration_stretch 0.77)。2026-10-25 にユーザーが選んだ。
#   声の高さは平均 270Hz(int_f0_target_mean)。元の 190Hz では低く、330Hz からは幼く聞こえた(2026-10-30 ユーザーがゲームの音で聞き比べて選んだ)
#   (はじめは mac の読み上げ(say)の Junior で作ったが、macOS の使用許諾は読み上げの声を公開・配布する使い方を認めていないので作り直した)
#   検証は tools/lineirq192/ と docs/検証記録.md
#   並び(番号は pcm.h の VOICE_。1 から): 1 Fire!(誘導弾) 2 Enemy ahead! 3 Enemy behind!(警告) 4 Warning!(ボス) 5 You will go no further!(最後のボスの台詞)
#   最後のボスの台詞だけ、男の声(rms)を低く読ませ、さらに 1.2 倍のサンプリング周波数で作って 3,996Hz で鳴らす(1.2 倍遅く、低くなる)
# モジュールの形(0x8000-): db 声の数、(dw 番地, dw サンプル数) × 声の数、続けてサンプル(8bit unsigned、中心 0x80、3,996Hz)
set -e
cd "$(dirname "$0")/.."
OUT=build/voice
mkdir -p "$OUT"
n=0
for p in "fire:Fire!" "ahead:Enemy ahead!" "behind:Enemy behind!" "warning:Warning!"; do
  name=${p%%:*}
  text=${p#*:}
  flite -voice slt --setf duration_stretch=0.77 --setf int_f0_target_mean=270 --setf int_f0_target_stddev=45 -t "$text" -o "$OUT/$name.raw.wav"
  afconvert -f WAVE -d LEI16@3996 -c 1 "$OUT/$name.raw.wav" "$OUT/$name.wav"
done
flite -voice rms --setf duration_stretch=0.9 --setf int_f0_target_mean=80 -t "You will go no further!" -o "$OUT/king.raw.wav"
afconvert -f WAVE -d LEI16@4795 -c 1 "$OUT/king.raw.wav" "$OUT/king.wav"
python3 - "$OUT" src/sound/voicemod.bin <<'PY'
import struct, sys, wave
names = ['fire', 'ahead', 'behind', 'warning', 'king']
voices = []
for n in names:
    w = wave.open('%s/%s.wav' % (sys.argv[1], n))
    k = w.getnframes()
    d = struct.unpack('<%dh' % k, w.readframes(k))
    m = max(abs(x) for x in d)
    th = m * 0.04
    i0 = next(i for i, x in enumerate(d) if abs(x) > th)
    i1 = k - next(i for i, x in enumerate(reversed(d)) if abs(x) > th)
    s = d[max(0, i0 - 8):i1 + 8]
    voices.append(bytes(max(1, min(255, int(round(128 + x * 127.0 / m)))) for x in s))   # 中心 0x80
head = 1 + 4 * len(voices)
out = bytearray([len(voices)]) + bytes(4 * len(voices))
for j, v in enumerate(voices):
    out[1 + 4 * j:5 + 4 * j] = struct.pack('<HH', 0x8000 + len(out), len(v))
    out += v
if len(out) > 0x3C00:
    sys.exit('gen_voice: 声のモジュールが %d バイトで 15KB を超えた' % len(out))
open(sys.argv[2], 'wb').write(out)
for n, v in zip(names, voices):
    print('  %-8s %5d サンプル %.2f 秒' % (n, len(v), len(v) / 3996))
print('gen_voice: %s %d バイト' % (sys.argv[2], len(out)))
PY
