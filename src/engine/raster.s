; raster.s — 凸多角形のラスタライズ(ホットパス)
;
; 入力: _scr[n] = { i16 x, i16 y }(画面座標 12.4、±15512 以内に収めてあること)
;   → 差が 16bit 符号付きに収まるので、比較は sbc hl,de の符号だけで行える。
;
; 標本化(render.c と同じ約束):
;   行 r はブロック中心 y = r*16+8。辺 (y0 < y1) が覆う行 = [(y0+7)>>4, (y1+7)>>4)
;   スパン = [round(xl), round(xr))。下り辺は span_a、上り辺は span_b に x を書く。
;
; 辺ごとの計算(割り算 2〜3 回):
;   slope(16.16 ブロック/行) = |dx| * 65536 / dy          … div_core 1〜2 回
;   x_start(12.4)            = x0 ± |dx| * t / dy(t = 最初の行の中心 - y0) … MULUW + div_core 1 回
; 行ごとのループは exx で 32bit の加算をレジスタだけで回す(その間は DI)。

	.module	raster
	.include "r800.inc"

	.globl	div_core
	.globl	_recip9
	.globl	_g_color
	.globl	_g_buf_hi

ROWS	= 46

	.area	_DATA
_scr::		.ds	4 * 10
_span_a::	.ds	48		; span_b は span_a + 48(連続で置く。fill が 48(ix) で引く)
_span_b::	.ds	48
r_n:		.ds	1
r_ymin:		.ds	2
r_ymax:		.ds	2
r_xmin:		.ds	2
r_xmax:		.ds	2
e_x0:		.ds	2
e_y0:		.ds	2
e_x1:		.ds	2
e_y1:		.ds	2
e_dst:		.ds	2
e_rs:		.ds	1
e_neg:		.ds	1
e_dy:		.ds	2
e_adx:		.ds	2
e_sh:		.ds	2
e_sl:		.ds	2
e_xl:		.ds	2
e_t:		.ds	2
e_ol:		.ds	2
e_oh:		.ds	2
e_xh:		.ds	2
p_out:		.ds	2
p_x:		.ds	2
p_y:		.ds	2
p_q:		.ds	2

	.area	_CODE

; HL = (HL + 7) >> 4(算術)
row_of:
	ld	de, #7
	add	hl, de
	sra	h
	rr	l
	sra	h
	rr	l
	sra	h
	rr	l
	sra	h
	rr	l
	ret

; HL(符号付き)を 0..ROWS に丸めて A に
clamp_row:
	bit	7, h
	jr	nz, 1$
	ld	a, h
	or	a, a
	jr	nz, 2$
	ld	a, l
	cp	a, #ROWS + 1
	ret	c
2$:
	ld	a, #ROWS
	ret
1$:
	xor	a, a
	ret

; ---------------------------------------------------------------------------
; void raster(u8 n, u8 color)   A=n, L=color
; ---------------------------------------------------------------------------
_raster::
	push	af
	pop	af
	ld	c, a
	ld	a, l
	ld	(_g_color), a
	ld	a, c
	ld	(r_n), a
	push	ix
	push	iy

	; ---- 外接矩形(最初の頂点で初期化する。0x7FFF などの番兵は差が 16bit を超えて比較が壊れる)
	ld	hl, (_scr)
	ld	(r_xmin), hl
	ld	(r_xmax), hl
	ld	hl, (_scr + 2)
	ld	(r_ymin), hl
	ld	(r_ymax), hl
	ld	ix, #_scr
	ld	b, c
10$:
	ld	e, 2 (ix)
	ld	d, 3 (ix)
	ld	hl, (r_ymin)
	or	a, a
	sbc	hl, de
	jp	m, 11$
	ld	(r_ymin), de
11$:
	ld	hl, (r_ymax)
	or	a, a
	sbc	hl, de
	jp	p, 12$
	ld	(r_ymax), de
12$:
	ld	e, 0 (ix)
	ld	d, 1 (ix)
	ld	hl, (r_xmin)
	or	a, a
	sbc	hl, de
	jp	m, 13$
	ld	(r_xmin), de
13$:
	ld	hl, (r_xmax)
	or	a, a
	sbc	hl, de
	jp	p, 14$
	ld	(r_xmax), de
14$:
	inc	ix
	inc	ix
	inc	ix
	inc	ix
	djnz	10$

	; 画面の左右に完全に外れていれば描かない
	ld	a, (r_xmax + 1)
	bit	7, a
	jp	nz, r_done
	ld	hl, (r_xmin)
	ld	de, #64 * 16 + 1
	or	a, a
	sbc	hl, de
	jp	p, r_done

	; 行の範囲
	ld	hl, (r_ymin)
	call	row_of
	call	clamp_row
	ld	c, a			; C = r0
	ld	hl, (r_ymax)
	call	row_of
	call	clamp_row		; A = r1
	sub	a, c
	jp	z, r_done
	jp	c, r_done
	push	bc			; C = r0
	push	af			; A = 行数

	; ---- 辺
	ld	ix, #_scr
	ld	a, (r_n)
	ld	b, a
20$:
	push	bc
	; 次の頂点(最後は先頭へ)
	push	ix
	pop	iy
	inc	iy
	inc	iy
	inc	iy
	inc	iy
	dec	b
	jr	nz, 21$
	ld	iy, #_scr
21$:
	; y0 = 2(ix), y1 = 2(iy)
	ld	l, 2 (ix)
	ld	h, 3 (ix)
	ld	e, 2 (iy)
	ld	d, 3 (iy)
	or	a, a
	sbc	hl, de			; y0 - y1
	jr	z, 29$			; 水平な辺は無視
	jp	p, 22$
	; y0 < y1: 下り辺 (ix → iy) を span_a へ
	ld	l, 0 (ix)
	ld	h, 1 (ix)
	ld	(e_x0), hl
	ld	l, 2 (ix)
	ld	h, 3 (ix)
	ld	(e_y0), hl
	ld	l, 0 (iy)
	ld	h, 1 (iy)
	ld	(e_x1), hl
	ld	(e_y1), de
	ld	hl, #_span_a
	jr	23$
22$:
	; y0 > y1: 上り辺。上端 (iy) から下端 (ix) を span_b へ
	ld	l, 0 (iy)
	ld	h, 1 (iy)
	ld	(e_x0), hl
	ld	(e_y0), de
	ld	l, 0 (ix)
	ld	h, 1 (ix)
	ld	(e_x1), hl
	ld	l, 2 (ix)
	ld	h, 3 (ix)
	ld	(e_y1), hl
	ld	hl, #_span_b
23$:
	ld	(e_dst), hl
	call	edge
29$:
	inc	ix
	inc	ix
	inc	ix
	inc	ix
	pop	bc
	djnz	20$

	; ---- 塗る
	pop	af			; 行数
	pop	bc			; C = r0
	ld	b, a
	call	fill
r_done:
	pop	iy
	pop	ix
	ret

; ---------------------------------------------------------------------------
; edge: e_x0,e_y0(上端)→ e_x1,e_y1(下端)が覆う行の x を e_dst[行] に書く
;   IX/IY は壊してよい(_raster が保存している)
; ---------------------------------------------------------------------------
edge:
	; rs, re
	ld	hl, (e_y0)
	call	row_of
	push	hl			; rs(クランプ前、符号付き)
	call	clamp_row
	ld	(e_rs), a
	ld	c, a
	ld	hl, (e_y1)
	call	row_of
	call	clamp_row		; A = re
	sub	a, c
	pop	hl
	ret	z
	ret	c
	push	af			; 行数
	; t = rs*16 + 8 - y0(rs はクランプ後)
	ld	a, (e_rs)
	ld	l, a
	ld	h, #0
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	de, #8
	add	hl, de
	ld	de, (e_y0)
	or	a, a
	sbc	hl, de
	push	hl			; t(>= 0)
	; dy
	ld	hl, (e_y1)
	or	a, a
	sbc	hl, de
	ld	(e_dy), hl
	; dx と符号
	ld	hl, (e_x1)
	ld	de, (e_x0)
	or	a, a
	sbc	hl, de
	ld	a, h
	rla
	sbc	a, a
	ld	(e_neg), a		; 0xFF = 左へ
	jr	nc, 1$
	xor	a, a
	sub	a, l
	ld	l, a
	sbc	a, a
	sub	a, h
	ld	h, a
1$:
	ld	(e_adx), hl

	; slope = |dx| * 65536 / dy を逆数表で求める(割り算の代わり)
	;   dy を 9bit の仮数 m(256..511)と指数に分け、1/dy ≒ recip9[m] / 2^(24+s)
	;   slope = (|dx| * recip9[m]) >> k、k = 8 + s(dy を右へずらした回数 s、左なら負)
	ld	hl, (e_dy)
	ld	c, #8
10$:
	ld	a, h
	cp	a, #2
	jr	c, 11$			; HL < 512
	srl	h
	rr	l
	inc	c
	jr	10$
11$:
	bit	0, h
	jr	nz, 12$			; HL >= 256
	add	hl, hl
	dec	c
	jr	11$
12$:
	ld	h, #0			; m - 256 = L
	add	hl, hl
	ld	de, #_recip9
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a			; HL = recip9[m]
	push	bc
	ld	bc, (e_adx)
	MULUW_HL_BC			; DE:HL = |dx| * recip9[m]
	pop	bc
	ld	a, c
	or	a, a
	jr	z, 14$
13$:
	srl	d
	rr	e
	rr	h
	rr	l
	dec	c
	jr	nz, 13$
14$:
	ld	(e_sl), hl
	ld	(e_sh), de

	; x の開始位置のずれ off(16.16) = slope * t / 16 = (sh*t << 12) + (sl*t >> 4)
	;   (割り算をもう 1 回するより MULUW 2 回のほうが速い)
	pop	bc			; t
	ld	(e_t), bc
	ld	hl, (e_sl)
	MULUW_HL_BC			; DE:HL = sl * t
	.rept	4
	srl	d
	rr	e
	rr	h
	rr	l
	.endm
	ld	(e_ol), hl
	ld	(e_oh), de
	ld	hl, (e_sh)
	ld	bc, (e_t)
	MULUW_HL_BC			; DE:HL = sh * t
	ld	a, d
	or	a, a
	jr	nz, 2$
	ld	a, e
	and	a, #0xF0
	jr	nz, 2$			; sh*t >= 2^20 → 画面の遥か外。飽和
	; sh*t << 12: 上位ワード = (sh*t) >> 4、下位ワードの上位バイト = (sh*t & 15) << 4
	ld	a, l
	and	a, #0x0F
	rlca
	rlca
	rlca
	rlca
	ld	c, a
	.rept	4
	srl	e
	rr	h
	rr	l
	.endm
	ld	a, (e_ol + 1)
	add	a, c
	ld	(e_ol + 1), a
	ld	de, (e_oh)
	adc	hl, de
	ld	(e_oh), hl
	bit	7, h
	jr	z, 3$
2$:
	ld	hl, #0x7FFF		; 飽和(どのみち 0 か 64 に丸まる)
	ld	(e_oh), hl
3$:
	; x(16.16) = (x0 << 12) + 0x800 ± off
	ld	hl, (e_x0)
	ld	de, #8			; +0.5 ブロック(12.4 で 8)を先に足して丸める
	add	hl, de
	ld	a, l
	and	a, #0x0F
	rlca
	rlca
	rlca
	rlca
	ld	d, a
	ld	e, #0			; DE = 下位ワード
	.rept	4
	sra	h
	rr	l
	.endm				; HL = 上位ワード
	ld	a, (e_neg)
	or	a, a
	jr	nz, 4$
	ex	de, hl
	ld	bc, (e_ol)
	add	hl, bc
	ex	de, hl
	ld	bc, (e_oh)
	adc	hl, bc
	jr	5$
4$:
	ex	de, hl
	ld	bc, (e_ol)
	or	a, a
	sbc	hl, bc
	ex	de, hl
	ld	bc, (e_oh)
	sbc	hl, bc
5$:
	ld	(e_xh), hl
	ld	(e_xl), de
	; 傾きの符号
	ld	a, (e_neg)
	or	a, a
	jr	z, 6$
	ld	hl, (e_sl)
	ld	de, (e_sh)
	xor	a, a
	sub	a, l
	ld	l, a
	ld	a, #0
	sbc	a, h
	ld	h, a
	ld	a, #0
	sbc	a, e
	ld	e, a
	ld	a, #0
	sbc	a, d
	ld	d, a
	ld	(e_sl), hl
	ld	(e_sh), de
6$:
	pop	af			; 行数
	ld	b, a
	ld	a, (e_rs)
	ld	e, a
	ld	d, #0
	ld	iy, (e_dst)
	add	iy, de
	; 表: HL = x 下位、DE = 傾き 下位 / 裏: HL = x 上位、DE = 傾き 上位
	di
	exx
	ld	hl, (e_xh)
	ld	de, (e_sh)
	exx
	ld	hl, (e_xl)
	ld	de, (e_sl)
7$:
	exx
	ld	a, h
	or	a, a
	jr	nz, 8$
	ld	a, l
	cp	a, #65
	jr	c, 9$
	ld	a, #64
	jr	9$
8$:
	bit	7, h
	ld	a, #0
	jr	nz, 9$
	ld	a, #64
9$:
	exx
	ld	0 (iy), a
	inc	iy
	add	hl, de
	exx
	adc	hl, de
	exx
	djnz	7$
	ei
	ret

; ---------------------------------------------------------------------------
; fill: C = 最初の行、B = 行数。span_a[r] と span_b[r] の間を g_color で塗る
; ---------------------------------------------------------------------------
fill:
	ld	e, c
	ld	d, #0
	ld	ix, #_span_a
	add	ix, de
	ld	l, c
	ld	h, #0
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	a, (_g_buf_hi)
	ld	d, a
	ld	e, #0
	add	hl, de
1$:
	ld	a, 0 (ix)
	ld	e, 48 (ix)
	cp	a, e
	jp	z, 4$
	jr	c, 2$
	ld	c, a
	ld	a, e
	ld	e, c
2$:
	; A = 左端、E = 右端。展開した 64 回の書き込みの途中へ飛び込んで幅だけ塗る
	ld	c, a
	ld	a, e
	sub	a, c			; 幅(1..64)
	push	hl			; 行頭
	ld	e, a
	ld	a, l
	add	a, c
	ld	l, a			; 行頭は 64 の倍数、左端 <= 63 なので下位だけで足りる
	ld	a, e
	add	a, a
	ld	e, a
	ld	d, #0			; DE = 幅 * 2(1 回 2 バイト)
	push	hl
	ld	hl, #6$
	or	a, a
	sbc	hl, de
	ex	(sp), hl		; スタック = 飛び先、HL = 書き込み先
	ld	a, (_g_color)
	ret
	.rept	64
	ld	(hl), a
	inc	l
	.endm
6$:
	pop	hl
4$:
	ld	de, #64
	add	hl, de
	inc	ix
	dec	b
	jp	nz, 1$
	ret

; ---------------------------------------------------------------------------
; void project2(const V3 *v, i16 *out)   HL=v, DE=out。z >= 128(0.5m)が前提
;   out[0] = 512 + x*512/z、out[1] = 368 - y*512/z(12.4、±15000 に飽和)
;   割り算は q = 2^23 / z の 1 回だけ。x*q >> 14 = x*512/z
; ---------------------------------------------------------------------------
_project2::
	ld	(p_out), de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	inc	hl
	ld	(p_x), de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	inc	hl
	ld	(p_y), de
	ld	c, (hl)
	inc	hl
	ld	b, (hl)			; BC = z
	ld	a, b			; z = 128 ちょうどだと商が 16bit を超えるので 129 以上にする
	or	a, a
	jr	nz, 5$
	ld	a, c
	cp	a, #129
	jr	nc, 5$
	ld	c, #129
5$:
	ld	de, #0x0080
	ld	hl, #0
	call	div_core		; HL = 2^23 / z
	ld	(p_q), hl
	ld	hl, (p_x)
	call	p_scale
	ld	hl, #512
	add	hl, de
	ex	de, hl
	ld	hl, (p_out)
	ld	(hl), e
	inc	hl
	ld	(hl), d
	inc	hl
	push	hl
	ld	hl, (p_y)
	call	p_scale
	ld	hl, #368
	or	a, a
	sbc	hl, de
	ex	de, hl
	pop	hl
	ld	(hl), e
	inc	hl
	ld	(hl), d
	ret

; DE = clamp((HL * p_q) >> 14, ±15000)(HL 符号付き)
p_scale:
	ld	a, h
	push	af
	bit	7, h
	jr	z, 1$
	xor	a, a
	sub	a, l
	ld	l, a
	sbc	a, a
	sub	a, h
	ld	h, a
1$:
	ld	bc, (p_q)
	MULUW_HL_BC			; DE:HL
	; >> 14: 結果 = DE << 2 | H >> 6。DE >= 0x1000 なら 16bit を超える
	ld	a, d
	and	a, #0xF0
	jr	nz, 3$
	add	hl, hl
	rl	e
	rl	d
	add	hl, hl
	rl	e
	rl	d			; DE = 結果
	ld	hl, #15000
	or	a, a
	sbc	hl, de
	jr	nc, 4$
3$:
	ld	de, #15000
4$:
	pop	af
	bit	7, a
	ret	z
	xor	a, a
	sub	a, e
	ld	e, a
	sbc	a, a
	sub	a, d
	ld	d, a
	ret

; ---------------------------------------------------------------------------
; void band_fast(const i16 *sa, const i16 *sb)   HL=sa, DE=sb(各 6 頂点 × (x, y))
;   2 本のリングの間の 6 面(面 k = sa[k], sa[k+1], sb[k+1], sb[k])を
;   色 g_band_col[k] で塗る。全頂点が手前(投影済み)であること。
; ---------------------------------------------------------------------------
	.area	_DATA
bf_sa:		.ds	2
bf_sb:		.ds	2
bf_k:		.ds	1
_g_band_col::	.ds	2	; 6 色の表へのポインタ

	.area	_CODE
; HL = base + 4 * A
bf_pt:
	add	a, a
	add	a, a
	ld	c, a
	ld	b, #0
	add	hl, bc
	ret

_band_fast::
	ld	(bf_sa), hl
	ld	(bf_sb), de
	xor	a, a
	ld	(bf_k), a
1$:
	ld	de, #_scr
	ld	a, (bf_k)
	ld	hl, (bf_sa)
	call	bf_pt
	.rept	4
	ldi
	.endm
	ld	a, (bf_k)
	inc	a
	cp	a, #6
	jr	nz, 2$
	xor	a, a
2$:
	push	af			; k1
	ld	hl, (bf_sa)
	call	bf_pt
	.rept	4
	ldi
	.endm
	pop	af
	ld	hl, (bf_sb)
	call	bf_pt
	.rept	4
	ldi
	.endm
	ld	a, (bf_k)
	ld	hl, (bf_sb)
	call	bf_pt
	.rept	4
	ldi
	.endm
	ld	hl, (_g_band_col)
	ld	a, (bf_k)
	ld	c, a
	ld	b, #0
	add	hl, bc
	ld	l, (hl)
	ld	a, #4
	call	_raster
	ld	a, (bf_k)
	inc	a
	ld	(bf_k), a
	cp	a, #6
	jr	nz, 1$
	ret
