; bench.s — 計測用のルーチン(make BENCH=1 のときだけリンクする。bench.c から呼ぶ)
	.module	bench

	.area	_CODE

; ---------------------------------------------------------------------------
; void bench_fill64(void) — 計測用(make BENCH=1): 64 バイトの展開した塗り
; void bench_nop64(void)  — 計測用: 64 個の nop
; ---------------------------------------------------------------------------
_bench_fill64::
	ld	hl, #0xC000
	ld	a, #1
	.rept	64
	ld	(hl), a
	inc	l
	.endm
	ret
_bench_nop64::
	.rept	64
	nop
	.endm
	ret
; void bench_push64(void) — 計測用: push で 64 バイト塗る
	.area	_DATA
bp_sp:	.ds	2
	.area	_CODE
_bench_push64::
	di
	ld	(bp_sp), sp
	ld	sp, #0xC040
	ld	bc, #0x0101
	.rept	32
	push	bc
	.endm
	ld	sp, (bp_sp)
	ei
	ret

