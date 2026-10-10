#!/bin/sh
# 検証 ROM を作る(どちらも 16KB、ROM タイプ Normal)
#   build/LINEIRQ.ROM … SCREEN 3 で走査線割込みが来る行を測る(lineirq.s、測り方は measure.tcl)
#   build/PCMTEST.ROM … 走査線割込みで PCM の声「FIRE」を鳴らす(pcmtest.s、測り方は pcmrec.tcl)
#   build/PCMTEST_NOLOG.ROM … 同じで、A4h に書いた時刻を記録しない(割込みの重さを測る用)
#   build/PCMTEST_FAST.ROM  … さらに遅れの判定をしない(割込みの重さの下限を測る用。割込みを止める区間があると外す)
#   build/PCMTEST_OPT.ROM   … 詰めた割込み(ふつうの場合だけ 1 本の流れで処理する。遅れの判定はしない)
#   build/PCMTEST_SKIP.ROM  … OPT で、割込みが来ない行では待たない(声を止めたまま行と時刻だけ進める。声は 1 割ほど伸びる)
#   声の素材は mac の読み上げ(say)で作る: VOICE=Junior・RATE=300(既定。2026-10-10 にユーザーが選んだ。0.31 秒)。3,996Hz・8bit に直して build/lineirq192/fire.inc へ
set -e
cd "$(dirname "$0")/../.."
OUT=build/lineirq192
VOICE=${VOICE:-Junior}
mkdir -p "$OUT"
say -v "$VOICE" -r "${RATE:-300}" -o "$OUT/fire.aiff" "Fire!"
afconvert -f WAVE -d LEI16@3996 -c 1 "$OUT/fire.aiff" "$OUT/fire.wav"
python3 - "$OUT/fire.wav" "$OUT/fire.inc" <<'PY'
import struct, sys, wave
w = wave.open(sys.argv[1]); n = w.getnframes(); d = struct.unpack('<%dh' % n, w.readframes(n))
m = max(abs(x) for x in d); th = m * 0.04
i0 = next(i for i, x in enumerate(d) if abs(x) > th)
i1 = n - next(i for i, x in enumerate(reversed(d)) if abs(x) > th)
s = d[max(0, i0 - 8):i1 + 8]
b = [max(1, min(255, int(round(128 + x * 127.0 / m)))) for x in s]   # 8bit unsigned、中心 0x80
with open(sys.argv[2], 'w') as f:
    f.write('SND_LEN = %d\n' % len(b))
    for i in range(0, len(b), 16):
        f.write('\t.db ' + ','.join('0x%02X' % v for v in b[i:i + 16]) + '\n')
PY
printf 'NOLOG = 1\n.include "../../tools/lineirq192/pcmtest.s"\n' > "$OUT/pcmtest_nolog.s"
printf 'NOLOG = 1\nFAST = 1\n.include "../../tools/lineirq192/pcmtest.s"\n' > "$OUT/pcmtest_fast.s"
printf 'NOLOG = 1\nFAST = 1\nOPT = 1\n.include "../../tools/lineirq192/pcmtest.s"\n' > "$OUT/pcmtest_opt.s"
printf 'NOLOG = 1\nFAST = 1\nOPT = 1\nSKIPGAP = 1\n.include "../../tools/lineirq192/pcmtest.s"\n' > "$OUT/pcmtest_skip.s"
for M in lineirq pcmtest; do
  sdasz80 -I"$OUT" -o "$OUT/$M.rel" tools/lineirq192/$M.s
  sdldz80 -i "$OUT/$M.ihx" "$OUT/$M.rel" >/dev/null
done
for M in pcmtest_nolog pcmtest_fast pcmtest_opt pcmtest_skip; do
  sdasz80 -I"$OUT" -o "$OUT/$M.rel" "$OUT/$M.s"
  sdldz80 -i "$OUT/$M.ihx" "$OUT/$M.rel" >/dev/null
done
for M in lineirq:LINEIRQ pcmtest:PCMTEST pcmtest_nolog:PCMTEST_NOLOG pcmtest_fast:PCMTEST_FAST pcmtest_opt:PCMTEST_OPT pcmtest_skip:PCMTEST_SKIP; do
python3 - "$OUT/${M%%:*}.ihx" "build/${M##*:}.ROM" <<'PY'
import sys
rom = bytearray(b'\xFF' * 0x4000)
for line in open(sys.argv[1]):
    line = line.strip()
    if not line.startswith(':') or line[7:9] != '00':
        continue
    n, addr = int(line[1:3], 16), int(line[3:7], 16)
    for i, b in enumerate(bytes.fromhex(line[9:9 + n * 2])):
        a = addr + i
        # 0xD000〜 は RAM で動く部分(pcmtest.s)。ROM の 0x5000〜 に置き、起動時に写す
        rom[a - 0xD000 + 0x1000 if a >= 0xD000 else a - 0x4000] = b
open(sys.argv[2], 'wb').write(rom)
PY
echo "build/${M##*:}.ROM"
done
