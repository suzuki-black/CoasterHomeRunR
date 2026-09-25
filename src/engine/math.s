; math.s — 固定小数点演算(R800 の MULUW を使う。仕様書 7.5)
;
; 呼出規約(SDCC 4.6 z80 sdcccall(1)、実測で確認):
;   第1引数 16bit=HL / 8bit=A、第2引数 16bit=DE / 8bit=L(第1が A のとき)。
;   3つ目以降はスタック(呼ばれた側が片付ける)。戻り値 16bit=DE、32bit=HL:DE(HL が上位)。
;   IX/IY は保存する。

	.module	math
	.include "r800.inc"

	.area	_DATA
md_a:	.ds	2
md_c:	.ds	2


	.area	_CODE

; ---------------------------------------------------------------------------
; i16 fmul(i16 a, i16 b)  = (a * b) >> 8(符号付き 8.8 × 8.8。0 方向へ切り捨て)
; ---------------------------------------------------------------------------
_fmul::
	ld	a, h
	xor	a, d
	push	af			; 結果の符号(bit7)
	bit	7, h
	jr	z, 1$
	xor	a, a
	sub	a, l
	ld	l, a
	sbc	a, a
	sub	a, h
	ld	h, a
1$:
	ld	b, d
	ld	c, e
	bit	7, b
	jr	z, 2$
	xor	a, a
	sub	a, c
	ld	c, a
	sbc	a, a
	sub	a, b
	ld	b, a
2$:
	MULUW_HL_BC			; DE:HL = |a| * |b|
	ld	d, e
	ld	e, h			; DE = bit8-23
	pop	af
	ret	p
	xor	a, a
	sub	a, e
	ld	e, a
	sbc	a, a
	sub	a, d
	ld	d, a
	ret

; ---------------------------------------------------------------------------
; i16 muldiv(i16 a, i16 b, i16 c) = a * b / c(32bit の途中結果。0 方向へ切り捨て)
;   商が 16bit に収まらない(c=0 を含む)ときは ±0x7FFF に飽和する。
; ---------------------------------------------------------------------------
_muldiv::
	ld	(md_a), hl
	pop	hl			; 戻り番地
	pop	bc			; c
	push	hl
	ld	hl, (md_a)
	ld	a, h
	xor	a, d
	xor	a, b
	push	af			; 結果の符号
	bit	7, b
	jr	z, 1$
	xor	a, a
	sub	a, c
	ld	c, a
	sbc	a, a
	sub	a, b
	ld	b, a
1$:
	ld	(md_c), bc
	bit	7, h
	jr	z, 2$
	xor	a, a
	sub	a, l
	ld	l, a
	sbc	a, a
	sub	a, h
	ld	h, a
2$:
	ld	b, d
	ld	c, e
	bit	7, b
	jr	z, 3$
	xor	a, a
	sub	a, c
	ld	c, a
	sbc	a, a
	sub	a, b
	ld	b, a
3$:
	MULUW_HL_BC			; DE:HL = |a| * |b|
	ld	bc, (md_c)
	ex	de, hl			; HL = 上位, DE = 下位
	or	a, a
	sbc	hl, bc
	jr	nc, md_ovf		; 上位 >= c なら商が 16bit を超える
	add	hl, bc
	ex	de, hl			; DE = 余り(上位), HL = 下位
	call	div_core
	bit	7, h
	jr	z, md_sign
md_ovf:
	ld	hl, #0x7FFF
md_sign:
	pop	af
	ex	de, hl
	ret	p
	xor	a, a
	sub	a, e
	ld	e, a
	sbc	a, a
	sub	a, d
	ld	d, a
	ret

; DE:HL / BC(DE < BC が前提)→ HL = 商、DE = 余り。A を壊す(raster.s からも呼ぶ)
div_core::
	ld	a, d			; 上位が 0 で HL < BC なら商は 0(ループを回さない)
	or	a, e
	jr	nz, 6$
	push	hl
	or	a, a
	sbc	hl, bc
	pop	hl
	jr	nc, 6$
	ex	de, hl			; HL = 0、DE = 余り
	ret
6$:
	ld	a, #16
1$:
	add	hl, hl
	ex	de, hl
	adc	hl, hl
	jr	c, 3$
	sbc	hl, bc			; (キャリーは 0)
	jr	nc, 4$
	add	hl, bc
	ex	de, hl
	jr	5$
3$:
	or	a, a
	sbc	hl, bc
4$:
	ex	de, hl
	inc	l
5$:
	dec	a
	jr	nz, 1$
	ret

; ---------------------------------------------------------------------------
; u32 div_slope(u16 n, u16 d) = (n << 16) / d(符号なし。d=0 なら 0xFFFFFFFF)
; ---------------------------------------------------------------------------
_div_slope::
	ld	a, d
	or	a, e
	jr	z, 9$
	ld	b, d
	ld	c, e
	ld	de, #0			; 上位 16bit: 0:n / d
	call	div_core
	push	hl			; 商の上位
	ld	hl, #0			; 余り:0 / d
	call	div_core
	ex	de, hl			; DE = 商の下位
	pop	hl			; HL = 商の上位
	ret
9$:
	ld	hl, #0xFFFF
	ld	d, h
	ld	e, l
	ret
