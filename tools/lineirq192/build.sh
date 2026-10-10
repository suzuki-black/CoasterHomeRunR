#!/bin/sh
# 検証 ROM(SCREEN 3 の走査線割込みの行の範囲を測る)を作る → build/LINEIRQ.ROM(16KB、ROM タイプ Normal)
#   測り方は tools/lineirq192/measure.tcl
set -e
cd "$(dirname "$0")/../.."
OUT=build/lineirq192
mkdir -p "$OUT"
sdasz80 -o "$OUT/lineirq.rel" tools/lineirq192/lineirq.s
sdldz80 -i "$OUT/lineirq.ihx" "$OUT/lineirq.rel" >/dev/null
python3 - "$OUT/lineirq.ihx" build/LINEIRQ.ROM <<'EOF'
import sys
rom = bytearray(b'\xFF' * 0x4000)
for line in open(sys.argv[1]):
    line = line.strip()
    if not line.startswith(':') or line[7:9] != '00':
        continue
    n, addr = int(line[1:3], 16), int(line[3:7], 16)
    for i, b in enumerate(bytes.fromhex(line[9:9 + n * 2])):
        rom[addr + i - 0x4000] = b
open(sys.argv[2], 'wb').write(rom)
EOF
echo "build/LINEIRQ.ROM"
