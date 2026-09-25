; r800.inc — R800 固有命令のオペコード直書きマクロ
;
; sdasz80 は R800 命令を知らないので .db で置く(仕様書 7.5)。
; 出典: openMSX src/cpu/CPUCore.cc の ED プレフィクス表(TurboRVoxelShooter で使用実績あり)
;   ED C3 = MULUW HL,BC : DE:HL = HL * BC(符号なし)。フラグは破壊
;   ED C1 = MULUB A,B   : HL = A * B(符号なし)
; ※ MULUB A,H / A,L / A,A (ED E1/E9/F9) は openMSX に実装が無い。使わない。

	.macro	MULUW_HL_BC
	.db	0xED, 0xC3
	.endm
	.macro	MULUB_A_B
	.db	0xED, 0xC1
	.endm
	.macro	MULUB_A_E
	.db	0xED, 0xD9
	.endm
