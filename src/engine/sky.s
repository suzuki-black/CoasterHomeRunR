; sky.s — オープニングの遊園地の客(地形モジュール、ステージ 1 だけ。env.c の park_crowd から呼ぶ)
;
; void crowd_rows(void): 地平線の下の地面の行ごとに、その行の遠さの区画に並ぶ人を描く。
;   入力: _cr_n = 目の高さ(1/4m)* 32、_cr_fwd = 進んだ距離(1/4m)、_cr_side = 向き(横のブロック数)、_cr_row0 = 地平線の行
;   行 dr(1〜)ごとに: zq = n / dr、区画 = (fwd + zq) >> 3(前の行と同じ区画なら飛ばす)、h = 区画 * 37 + 91、
;   h の bit 4 が立つ区画は人がいない(半分の区画を空ける)。
;   組の間隔 gap = zq >= 144 ? 8 : min(1152 / zq, 28)(6m ぶんのブロック)、最初の列 c = (h + side) % gap。
;   組ごとに h = h * 5 + 17、2 人の色 = 色の表[(h >> 3) & 7]、c += (gap >> 1) + 1 + (h * gap >> 8)(c < 64 の間)。
;   2 人組は列 c を含むバイト(偶数の列とその右)に書く
; C で書くと 1 フレーム約 4.7ms(行ごとの割り算 2 回と、人ごとの関数呼び出し)だった。
; 2026-11-03: 人が多すぎて群れて見えたので、区画の半分を空け、2 人組にして組の間隔を 6m にした
; (2 人組は 1 バイトに書くだけなので、前の値を読む 1 人ずつの書き込みより軽い)

	.module	sky
	.r800
	.include "r800.inc"

	.globl	_g_buf_hi

GAME_ROWS = 46

	.area	_DATA
_cr_n::		.ds	2
_cr_fwd::	.ds	2
_cr_side::	.ds	1
_cr_row0::	.ds	1
cr_dr:		.ds	1
cr_last:	.ds	2
cr_zq:		.ds	2
cr_h:		.ds	1
cr_half:	.ds	1

	.area	_CODE

; 2 人組の色の表(上位 4 ビット = 左の人、下位 = 右の人。白 2・赤 3・紫 5・黄 6・橙 7・水色 8)
cr_pair:
	.db	0x26, 0x38, 0x75, 0x63, 0x82, 0x57, 0x23, 0x68

; HL / C → HL = 商、A = 余り(C は 1〜255)。B を壊す
div16_8:
	xor	a, a
	ld	b, #16
1$:
	add	hl, hl
	rla
	jr	c, 2$
	cp	a, c
	jr	c, 3$
2$:
	sub	a, c
	inc	l
3$:
	djnz	1$
	ret

_crowd_rows::
	ld	hl, #0xFFFF
	ld	(cr_last), hl
	ld	a, #1
	ld	(cr_dr), a
row:
	ld	a, (_cr_row0)
	ld	hl, #cr_dr
	add	a, (hl)
	cp	a, #GAME_ROWS
	ret	nc
	; zq = n / dr
	ld	c, (hl)
	ld	hl, (_cr_n)
	call	div16_8
	ld	(cr_zq), hl
	; 区画 = (fwd + zq) >> 3。前の行と同じなら飛ばす
	ld	de, (_cr_fwd)
	add	hl, de
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	ld	de, (cr_last)
	ld	a, l
	cp	a, e
	jr	nz, 1$
	ld	a, h
	cp	a, d
	jp	z, next
1$:
	ld	(cr_last), hl
	; h = 区画 * 37 + 91(下位 8 ビット)
	ld	a, l
	ld	e, #37
	MULUB_A_E
	ld	a, l
	add	a, #91
	ld	(cr_h), a
	bit	4, a
	jp	nz, next		; 区画の半分は人がいない(群れて見えないように)
	; gap
	ld	hl, (cr_zq)
	ld	a, h
	or	a, a
	jr	nz, 2$
	ld	a, l
	cp	a, #144
	jr	c, 3$
2$:
	ld	e, #8
	jr	5$
3$:
	ld	c, l
	ld	hl, #1152
	call	div16_8			; HL = 1152 / zq
	ld	e, #28
	ld	a, h
	or	a, a
	jr	nz, 5$
	ld	a, l
	cp	a, #29
	jr	nc, 5$
	ld	e, l
5$:
	; E = gap。最初の列 c = (h + side) % gap(9 ビットの和)
	ld	a, e
	srl	a
	inc	a
	ld	(cr_half), a
	ld	a, (cr_h)
	ld	l, a
	ld	h, #0
	ld	a, (_cr_side)
	add	a, l
	ld	l, a
	jr	nc, 6$
	inc	h
6$:
	ld	c, e
	push	de
	call	div16_8			; A = 余り
	pop	de
	ld	b, a			; B = c
	; この行の描き先: 上位 = バッファ + 行 >> 3、下位の行の位置 = 行 & 7(C)
	ld	a, (_cr_row0)
	ld	hl, #cr_dr
	add	a, (hl)
	ld	c, a
	and	a, #7
	ld	(cr_r7 + 1), a		; 下の「or a, #行 & 7」の即値
	ld	a, c
	rrca
	rrca
	rrca
	and	a, #0x1F
	ld	hl, #_g_buf_hi
	add	a, (hl)
	ld	(cr_hb + 1), a		; 下の「ld h, #上位」の即値
	ld	a, (cr_h)
	ld	d, a			; D = h、E = gap
person:
	ld	a, b
	cp	a, #64
	jr	nc, next
	; h = h * 5 + 17
	ld	a, d
	add	a, a
	add	a, a
	add	a, d
	add	a, #17
	ld	d, a
	; 2 人組の色 = cr_pair[(h >> 3) & 7](1 バイト = 横 2 ブロックなので、隣り合う 2 人を 1 回の書き込みで描く)
	rrca
	rrca
	rrca
	and	a, #7
	ld	hl, #cr_pair
	add	a, l
	ld	l, a
	adc	a, h
	sub	a, l
	ld	h, a
	ld	a, (hl)
	ex	af, af'			; A' = 2 人の色(割り込み処理は裏のレジスタを使わない)
	; 描き先の番地 = 上位 | ((c & 0x3E) << 2 | 行 & 7)
	ld	a, b
	and	a, #0x3E
	add	a, a
	add	a, a
cr_r7:
	or	a, #0			; 行 & 7(書き換える)
	ld	l, a
cr_hb:
	ld	h, #0			; 上位(書き換える)
	ex	af, af'
	ld	(hl), a
	; c += (gap >> 1) + 1 + (h * gap >> 8)
	ld	a, d
	MULUB_A_E			; HL = h * gap
	ld	a, (cr_half)
	add	a, h
	add	a, b
	ld	b, a
	jr	person
next:
	ld	hl, #cr_dr
	inc	(hl)
	jp	row
