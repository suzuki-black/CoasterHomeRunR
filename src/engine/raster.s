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
FILLBLK	= 0x0100		; 塗りの展開ブロック(ページ0の RAM。sys_page0_ram が _fillblk_tpl を写す)
FILLJP	= FILLBLK + 2
FILLEND	= FILLJP + 3 + 128
; 各行の左右端(0..64)。256 境界に置き、fill が inc d / dec d だけで左右を行き来する
_span_a	== 0xDE00
_span_b	== 0xDF00
SPAN_A_HI = 0xDE

; ROWCLAMP: HL = y(12.4)→ A = (y + 7) >> 4 を 0..ROWS に丸めた値(関数呼び出しと 16bit シフトをしない)
	.macro	ROWCLAMP ?rc_n, ?rc_b, ?rc_d
	bit	7, h
	jr	nz, rc_n
	ld	a, h
	cp	a, #3
	jr	nc, rc_b			; y >= 768 → 48 行以上
	ld	a, l
	add	a, #7
	ld	l, a
	ld	a, h
	adc	a, #0
	add	a, a
	add	a, a
	add	a, a
	add	a, a			; 上位 * 16
	ld	h, a
	ld	a, l
	rrca
	rrca
	rrca
	rrca
	and	a, #0x0F
	or	a, h
	cp	a, #ROWS + 1
	jr	c, rc_d
rc_b:
	ld	a, #ROWS
	jr	rc_d
rc_n:
	xor	a, a
rc_d:
	.endm

	.area	_DATA
e_fast:		.ds	1
_scr::		.ds	4 * 10
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
e_cnt:		.ds	1
e_t:		.ds	2
e_ol:		.ds	2
e_oh:		.ds	2
e_xh:		.ds	2
_p_q::		.ds	2	; 直前の投影の 2^23 / z(geom.s の draw_model が使う)

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
edge::
	; ---- 覆う行 [rs, re)
	ld	hl, (e_y0)
	ROWCLAMP
	ld	(e_rs), a
	ld	c, a
	ld	hl, (e_y1)
	ROWCLAMP
	sub	a, c
	ret	z
	ret	c
	ld	(e_cnt), a		; 辺キャッシュ(slot_ensure)が行の範囲を記録する
	; ---- t = rs*16 + 8 - y0(>= 0)
	ld	l, c
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
	ld	(e_t), hl
	; ---- dy = y1 - y0(> 0)
	ld	hl, (e_y1)
	or	a, a
	sbc	hl, de
	push	hl
	; ---- dx と符号
	ld	hl, (e_x1)
	ld	de, (e_x0)
	or	a, a
	sbc	hl, de
	ld	a, h
	ld	(e_neg), a		; bit7 = 左へ
	bit	7, h
	jr	z, 51$
	xor	a, a
	sub	a, l
	ld	l, a
	sbc	a, a
	sub	a, h
	ld	h, a
51$:
	ld	(e_adx), hl
	; ---- 速い経路に乗るか: 始点・終点の x が ±1792(12.4、±112 ブロック)以内、t < 4096
	;   上位バイトが 0..6 か 0xF9..0xFF なら ±1792 以内
	ld	a, (e_x0 + 1)
	add	a, #7
	cp	a, #14
	jr	nc, 52$
	ld	a, (e_x1 + 1)
	add	a, #7
	cp	a, #14
	jr	nc, 52$
	ld	a, (e_t + 1)
	cp	a, #0x10
	jr	nc, 52$
	ld	a, #1
	jr	53$
52$:
	xor	a, a
53$:
	ld	(e_fast), a
	pop	hl			; dy
	; ---- |dx| / dy を逆数表で: dy を 9bit の仮数 m(256..511)にそろえ、P = |dx| * recip9[m]
	;   16.16 の傾き = P >> k、8.8 の傾き = P >> (k + 8)(k = 8 + ずらした回数)
	ld	c, #8
10$:
	ld	a, h
	cp	a, #2
	jr	c, 11$
	srl	h
	rr	l
	inc	c
	jr	10$
11$:
	bit	0, h
	jr	nz, 12$
	add	hl, hl
	dec	c
	jr	11$
12$:
	ld	h, #0
	add	hl, hl
	ld	de, #_recip9
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	push	bc
	ld	bc, (e_adx)
	MULUW_HL_BC			; DE:HL = P
	pop	bc			; C = k
	ld	a, (e_fast)
	or	a, a
	jp	z, 30$
	; ---- 速い経路: step88 = P >> (k + 8)
	push	de			; P を取っておく(急すぎたら一般の経路へ)
	push	hl
	ld	a, c
	sub	a, #8
	jr	c, 20$
	ex	de, hl			; HL = P >> 16
	jr	z, 22$
	ld	b, a
21$:
	srl	h
	rr	l
	djnz	21$
	jr	22$
20$:
	neg				; k < 8: 左へ 8 - k ずらしてから上位 16bit
	ld	b, a
23$:
	add	hl, hl
	rl	e
	rl	d
	djnz	23$
	ex	de, hl
22$:
	bit	7, h
	jr	z, 24$
	pop	hl			; 傾き >= 128 ブロック/行: 一般の経路へ
	pop	de
	jp	30$
24$:
	pop	af			; 取っておいた P は要らない
	pop	af
	; off88 = step88 * (t << 4) >> 8(MULUW の中央 2 バイト)
	ld	b, h
	ld	c, l			; BC = |step88|
	ld	hl, (e_t)
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	MULUW_HL_BC			; DE:HL
	ld	l, h
	ld	h, e
	ex	de, hl			; DE = off88
	; x88 = (x0 + 8) << 4 ± off88、DE = ±step88
	ld	hl, (e_x0)
	push	bc
	ld	bc, #8
	add	hl, bc
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	pop	bc
	ld	a, (e_neg)
	rla
	jr	c, 25$
	add	hl, de
	ld	d, b
	ld	e, c
	jr	26$
25$:
	or	a, a
	sbc	hl, de
	xor	a, a
	sub	a, c
	ld	e, a
	sbc	a, a
	sub	a, b
	ld	d, a
26$:
	; HL = x88、DE = step88。書き込み先 IY = e_dst + rs、B = 行数
	ld	a, (e_cnt)
	ld	b, a
	ld	a, (e_rs)
	ld	iy, (e_dst)
	push	de
	ld	e, a
	ld	d, #0
	add	iy, de
	pop	de
	; 始点・終点とも画面の中(0 <= x < 1024)なら丸めずに書くだけ
	ld	a, (e_x0 + 1)
	cp	a, #4
	jr	nc, 43$
	ld	a, (e_x1 + 1)
	cp	a, #4
	jr	nc, 43$
27$:
	ld	0 (iy), h
	inc	iy
	add	hl, de
	djnz	27$
	ret
43$:
	ld	a, h
	bit	7, a
	jr	nz, 44$
	cp	a, #65
	jr	c, 45$
	ld	a, #64
	jr	45$
44$:
	xor	a, a
45$:
	ld	0 (iy), a
	inc	iy
	add	hl, de
	djnz	43$
	ret

	; ---- 一般の経路へ: 16.16 の傾き S = P >> k を e_sl / e_sh に
30$:
	ld	a, c
	cp	a, #8
	jr	c, 35$
	ld	l, h			; まず 8bit はバイトの入れ替えで
	ld	h, e
	ld	e, d
	ld	d, #0
	sub	a, #8
35$:
	or	a, a
	jr	z, 34$
	ld	b, a
33$:
	srl	d
	rr	e
	rr	h
	rr	l
	djnz	33$
34$:
	ld	(e_sl), hl
	ld	(e_sh), de
	; ---- 一般の経路: 32bit のずれ計算
40$:
	ld	hl, (e_sl)
	; ---- 最初の行の中心までのずれ off(16.16) = slope * t / 16 = (sh*t << 12) + (sl*t >> 4)
	ld	bc, (e_t)
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
	MULUW_HL_BC			; DE:HL = sh * t(BC は保存される)
	ld	a, d
	or	a, a
	jr	nz, 2$
	ld	a, e
	and	a, #0xF0
	jr	nz, 2$			; sh*t >= 2^20 → 画面の遥か外。飽和
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
	ld	hl, #0x7FFF
	ld	(e_oh), hl
3$:
	; ---- x(16.16) = (x0 + 8) << 12 ± off(+8 = +0.5 ブロックの丸め)
	ld	hl, (e_x0)
	ld	de, #8
	add	hl, de
	ld	a, l
	and	a, #0x0F
	rlca
	rlca
	rlca
	rlca
	ld	d, a
	ld	e, #0
	.rept	4
	sra	h
	rr	l
	.endm
	ld	bc, (e_ol)
	ld	a, (e_neg)
	rla
	jr	c, 4$
	ex	de, hl
	add	hl, bc
	ex	de, hl
	ld	bc, (e_oh)
	adc	hl, bc
	jr	5$
4$:
	ex	de, hl
	or	a, a
	sbc	hl, bc
	ex	de, hl
	ld	bc, (e_oh)
	sbc	hl, bc
5$:
	; HL:DE = x の開始(16.16)
	ld	(e_xh), hl
	ld	(e_xl), de
	ld	a, (e_neg)
	rla
	jr	nc, 6$
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
	ld	a, (e_cnt)
	ld	b, a
	ld	a, (e_rs)
	ld	e, a
	ld	d, #0
	ld	iy, (e_dst)
	add	iy, de
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
fill::
	; 終わりの行(r0 + 行数)と色を命令へ書き込む(自己書き換え。コードは RAM 上)
	; ★push で 2 バイトずつ書く版も試したが、塗りの時間は 1 行ごとの準備で決まっていて速くならなかった
	ld	a, c
	add	a, b
	ld	(fill_cmp + 1), a
	ld	a, (_g_color)
	ld	(FILLBLK + 1), a
	; DE = span_a + r0、HL = バッファの行頭
	ld	d, #SPAN_A_HI
	ld	e, c
	ld	l, c
	ld	h, #0
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	a, (_g_buf_hi)
	add	a, h
	ld	h, a
fill_row:
	inc	d
	ld	a, (de)			; span_b
	ld	c, a
	dec	d
	ld	a, (de)			; span_a
	sub	a, c			; 大小と幅を 1 回の引き算で
	jr	z, fill_next
	jr	c, fill_lt
	ld	b, a			; span_a > span_b: 幅 = A、左端 = span_b(C)
	jr	fill_go
fill_lt:
	neg				; span_a < span_b: 幅 = -A、左端 = span_b - 幅
	ld	b, a
	ld	a, c
	sub	a, b
	ld	c, a
fill_go:
	; 飛び先の下位 = 終わりの下位 - 幅 * 2(ブロックは 256 境界の中なので上位は固定)
	ld	a, b
	add	a, a
	ld	b, a
	ld	a, #<FILLEND
	sub	a, b
	ld	(FILLJP + 1), a
	ld	b, l			; 行頭の下位を覚えておく
	ld	a, l
	add	a, c
	ld	l, a			; 行頭は 64 の倍数、左端 <= 63 なので下位だけで足りる
	jp	FILLBLK			; 色を A に入れてから展開ブロックの途中へ
fill_ret::
	ld	l, b
fill_next:
	ld	a, l
	add	a, #64
	ld	l, a
	jr	nc, fill_inc
	inc	h
fill_inc:
	inc	e
	ld	a, e
fill_cmp:
	cp	a, #0			; 終わりの行(書き換える)
	jp	nz, fill_row
	ret

; 展開した 64 回の書き込みの元(sys_page0_ram がページ0の FILLBLK = 0x0100 へ写す)。
;   256 境界の中で動くので、飛び先は下位バイトだけを書き換える
_fillblk_tpl::
	ld	a, #0			; FILLBLK: 色(書き換える)
	jp	0x0100			; FILLJP: 飛び先(下位を書き換える。上位は 0x01 固定)
	.rept	64
	ld	(hl), a
	inc	l
	.endm
	jp	fill_ret		; FILLEND
_fillblk_end::

; ---------------------------------------------------------------------------
; void project2(const V3 *v, i16 *out)   HL=v, DE=out。z >= 128(0.5m)が前提
;   out[0] = 512 + x*512/z、out[1] = 368 - y*512/z(12.4、±15000 に飽和)
;   割り算は q = 2^23 / z の 1 回だけ。x*q >> 14 = x*512/z
; ---------------------------------------------------------------------------
_project2::
	; 変数をメモリに置かず、スタックとレジスタで回す(R800 はデータのメモリアクセスが重い)
	push	de			; 出力先
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	inc	hl
	push	de			; x
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	inc	hl
	push	de			; y
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a			; HL = z
	; q = 2^23 / z を逆数表で。z を 9bit の仮数 m(256..511)にそろえ、q = recip9[m] >> k
	ld	c, #1
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
	ld	h, #0
	add	hl, hl
	ld	de, #_recip9
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a			; HL = recip9[m]
	ld	a, c
	or	a, a
	jr	z, 14$
13$:
	srl	h
	rr	l
	dec	c
	jr	nz, 13$
14$:
	ld	(_p_q), hl
	ld	b, h
	ld	c, l			; BC = q(p_scale の間は保たれる)
	pop	hl			; y
	call	p_scale
	ld	hl, #368
	or	a, a
	sbc	hl, de			; sy
	ex	(sp), hl		; スタック = sy、HL = x
	call	p_scale
	ld	hl, #512
	add	hl, de			; sx
	pop	de			; sy
	ex	(sp), hl		; スタック = sx、HL = 出力先
	pop	bc			; sx
	ld	(hl), c
	inc	hl
	ld	(hl), b
	inc	hl
	ld	(hl), e
	inc	hl
	ld	(hl), d
	ret

; DE = clamp((HL * BC) >> 14, ±15000)(HL 符号付き、BC = q)。BC は保つ
p_scale:
	bit	7, h
	jr	nz, 5$
	MULUW_HL_BC			; DE:HL
	call	6$
	ret
5$:
	xor	a, a
	sub	a, l
	ld	l, a
	sbc	a, a
	sub	a, h
	ld	h, a
	MULUW_HL_BC
	call	6$
	xor	a, a
	sub	a, e
	ld	e, a
	sbc	a, a
	sub	a, d
	ld	d, a
	ret
	; DE:HL >> 14 → DE(15000 で飽和)。結果 = DE << 2 | H >> 6
6$:
	ld	a, d
	cp	a, #0x0E
	jr	nc, 7$			; 14336 以上は飽和
	add	hl, hl
	rl	e
	rl	d
	add	hl, hl
	rl	e
	rl	d
	ret
7$:
	ld	de, #15000
	ret

; ---------------------------------------------------------------------------
; 辺キャッシュ(トンネルの辺を面どうしで共有する)
;   トンネルの辺は 2 面ずつ共有している(リングの辺 = 隣り合う 2 区間、放射方向の辺 = 同じ区間の隣の面)。
;   辺ごとに「行ごとの x」を 1 フレーム 1 回だけ計算してスロットに置き、面はそれを span_a / span_b へ
;   コピーして塗るだけにする。
;   スロット id: リングの辺 = リング番号 * 6 + k(頂点 k → k+1)、放射方向の辺 = 72 + 手前のリング番号 * 6 + k
;   スロットはフレームごとに先着順で割り当てる(eslot_map[id] = スロット番号 + 1)。スロット = ESLOT + 番号 * 64:
;     [0..45] 行 r の x、[46] 最初の行、[47] 最後の行 + 1、[48] 1 = 作ったときの向き(p→q)が下り
;   同じ 2 点からは向きによらず同じ値が出る(上端→下端にそろえて計算する)ので、面の間にすき間は出ない。
; ---------------------------------------------------------------------------
ESLOT	= 0x0200		; 96 スロット × 64 = 0x0200-0x19FF(ページ0の RAM)
NSLOT	= 95			; 割り当てる数。最後の 1 本(95)は使い切ったときの一時用
NIDS	= 144			; 辺の番号の数

	.area	_DATA
_eslot_map::	.ds	NIDS	; 0 = このフレームでまだ、それ以外 = スロット番号 + 1
es_next:	.ds	1	; 次に割り当てるスロット番号
_g_ring_s::	.ds	2	; リングの投影済み座標の表(env.c の ring_s。1 本 24 バイト)
_g_band_col::	.ds	2	; band_fast: 6 色の表へのポインタ
sl_tbl:		.ds	36	; band_fast: 18 本の辺のスロット(リング a の辺、リング b の辺、放射)
bf_pa:		.ds	2
bf_pb:		.ds	2
bf_a:		.ds	1
bf_b:		.ds	1
bf_k:		.ds	1
sc_r0:		.ds	1
sc_r1:		.ds	1

	.area	_CODE

; void eslot_clear(void) — フレームの始めに呼ぶ
_eslot_clear::
	ld	hl, #_eslot_map
	ld	de, #_eslot_map + 1
	ld	bc, #NIDS - 1
	ld	(hl), #0
	ldir
	xor	a, a
	ld	(es_next), a
	ret

; HL = ESLOT + A * 64
slot_addr:
	ld	l, a
	ld	h, #0
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	de, #ESLOT
	add	hl, de
	ret

; slot_ensure: A = id、HL = p、DE = q(画面座標へのポインタ)→ HL = スロットの先頭
;   このフレームでまだなら、スロットを割り当てて辺 p→q の行ごとの x を計算して置く。IX/IY は壊さない
slot_ensure::
	ld	c, a
	ld	b, #0
	push	hl
	ld	hl, #_eslot_map
	add	hl, bc
	ld	a, (hl)
	or	a, a
	jp	nz, 8$
	ld	a, (es_next)
	cp	a, #NSLOT
	jr	c, 1$
	ld	a, #NSLOT		; 使い切った: 一時用のスロットで計算する(覚えない)
	jr	6$
1$:
	inc	a
	ld	(es_next), a
	ld	(hl), a
	dec	a
6$:
	pop	hl
	; --- 作る。スロットの先頭を e_dst に
	push	de
	push	hl
	call	slot_addr
	ld	(e_dst), hl
	ld	de, #46
	add	hl, de
	xor	a, a
	ld	(hl), a			; rs = re = 0(行なし)
	inc	hl
	ld	(hl), a
	inc	hl
	ld	(hl), a			; 向き
	pop	hl			; p
	pop	de			; q
	; p.y と q.y を比べ、上端 → 下端の順で e_x0.. に入れる
	push	hl
	push	de
	inc	hl
	inc	hl
	ld	c, (hl)
	inc	hl
	ld	b, (hl)			; BC = p.y
	ex	de, hl
	inc	hl
	inc	hl
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a			; HL = q.y
	or	a, a
	sbc	hl, bc
	pop	de			; q
	pop	hl			; p
	jr	z, 7$			; 水平: 行なし
	jp	m, 2$
	; 下り: そのまま。向きを記録
	push	hl
	ld	hl, (e_dst)
	ld	bc, #48
	add	hl, bc
	ld	(hl), #1
	pop	hl
	jr	3$
2$:
	ex	de, hl			; 上り: 入れ替える
3$:
	; HL = 上端、DE = 下端
	ld	bc, #4
	push	de
	ld	de, #e_x0		; e_x0, e_y0 は連続
	ldir
	pop	hl
	ld	de, #e_x1		; e_x1, e_y1 は連続
	ld	c, #4
	ldir
	xor	a, a
	ld	(e_cnt), a
	push	ix
	push	iy
	call	edge
	pop	iy
	pop	ix
	ld	hl, (e_dst)
	ld	a, (e_cnt)
	or	a, a
	ret	z
	ld	b, a
	push	hl
	ld	de, #46
	add	hl, de
	ld	a, (e_rs)
	ld	(hl), a
	inc	hl
	add	a, b
	ld	(hl), a
	pop	hl
	ret
7$:
	ld	hl, (e_dst)
	ret
8$:
	pop	hl
	dec	a
	jp	slot_addr

; seg_copy: HL = スロット、A = 1 なら作ったときと逆向きに使う
;   面の中での向きが下りなら span_a、上りなら span_b へ、覆う行の x をコピーし、sc_r0 / sc_r1 を広げる
seg_copy:
	ld	c, a
	push	hl
	ld	de, #46
	add	hl, de
	ld	b, (hl)			; rs
	inc	hl
	ld	a, (hl)			; re
	inc	hl
	sub	a, b
	jr	z, 9$
	jr	c, 9$
	ld	e, a			; 行数
	ld	a, (hl)			; 作ったときの向き
	xor	a, c
	ld	d, #SPAN_A_HI
	jr	nz, 1$
	inc	d			; span_b
1$:
	; 範囲を広げる
	ld	a, (sc_r0)
	cp	a, b
	jr	c, 2$
	ld	a, b
	ld	(sc_r0), a
2$:
	ld	a, b
	add	a, e
	ld	c, a
	ld	a, (sc_r1)
	cp	a, c
	jr	nc, 3$
	ld	a, c
	ld	(sc_r1), a
3$:
	ld	c, e			; 行数
	ld	e, b			; DE = span + rs
	pop	hl
	ld	a, l
	add	a, b
	ld	l, a			; スロット + rs(64 境界の中)
	ld	b, #0
	ldir
	ret
9$:
	pop	hl
	ret

; sc_fill: sc_r0..sc_r1 を g_color で塗り、範囲を空に戻す
sc_fill:
	ld	a, (sc_r0)
	ld	c, a
	ld	a, (sc_r1)
	sub	a, c
	jr	z, 1$
	jr	c, 1$
	ld	b, a
	call	fill
1$:
	ld	a, #ROWS
	ld	(sc_r0), a
	xor	a, a
	ld	(sc_r1), a
	ret

; HL = g_ring_s + A * 24
ring_ptr:
	ld	l, a
	ld	h, #0
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	d, h
	ld	e, l
	add	hl, hl
	add	hl, de
	ld	de, (_g_ring_s)
	add	hl, de
	ret

; ---------------------------------------------------------------------------
; void band_fast(u8 a, u8 b)   A = 手前のリング番号、L = 奥のリング番号
;   2 本のリングの間の 6 面(面 k = a[k], a[k+1], b[k+1], b[k])を色 g_band_col[k] で塗る。
;   全頂点が手前(投影済み)であること。
; ---------------------------------------------------------------------------
_band_fast::
	push	ix
	ld	(bf_a), a
	ld	a, l
	ld	(bf_b), a
	call	ring_ptr
	ld	(bf_pb), hl
	ld	a, (bf_a)
	call	ring_ptr
	ld	(bf_pa), hl
	; --- 18 本の辺を用意(計算済みならスロットを引くだけ)
	ld	ix, #sl_tbl
	xor	a, a
	ld	(bf_k), a
1$:
	; リング a の辺 k: a[k] → a[k+1]
	ld	a, (bf_k)
	ld	b, a
	ld	a, (bf_a)
	call	band_ring_edge
	ld	0 (ix), l
	ld	1 (ix), h
	; リング b の辺 k
	ld	a, (bf_k)
	ld	b, a
	ld	a, (bf_b)
	call	band_ring_edge
	ld	12 (ix), l
	ld	13 (ix), h
	; 放射 k: a[k] → b[k]
	ld	a, (bf_k)
	add	a, a
	add	a, a
	ld	e, a
	ld	d, #0
	ld	hl, (bf_pb)
	add	hl, de
	push	hl
	ld	hl, (bf_pa)
	add	hl, de
	pop	de
	ld	a, (bf_a)
	ld	c, a
	add	a, a
	add	a, c
	add	a, a			; a * 6
	ld	c, a
	ld	a, (bf_k)
	add	a, c
	add	a, #72
	call	slot_ensure
	ld	24 (ix), l
	ld	25 (ix), h
	inc	ix
	inc	ix
	ld	a, (bf_k)
	inc	a
	ld	(bf_k), a
	cp	a, #6
	jr	nz, 1$
	; --- 面
	ld	a, #ROWS
	ld	(sc_r0), a
	xor	a, a
	ld	(sc_r1), a
	ld	ix, #sl_tbl
	ld	(bf_k), a
2$:
	ld	l, 0 (ix)		; リング a の辺 k(作った向きのまま)
	ld	h, 1 (ix)
	xor	a, a
	call	seg_copy
	ld	a, (bf_k)		; 放射 k+1(作った向きのまま)
	cp	a, #5
	ld	l, 26 (ix)
	ld	h, 27 (ix)
	jr	nz, 3$
	ld	hl, (sl_tbl + 24)
3$:
	xor	a, a
	call	seg_copy
	ld	l, 12 (ix)		; リング b の辺 k(逆向き)
	ld	h, 13 (ix)
	ld	a, #1
	call	seg_copy
	ld	l, 24 (ix)		; 放射 k(逆向き)
	ld	h, 25 (ix)
	ld	a, #1
	call	seg_copy
	ld	hl, (_g_band_col)
	ld	a, (bf_k)
	ld	e, a
	ld	d, #0
	add	hl, de
	ld	a, (hl)
	ld	(_g_color), a
	call	sc_fill
	inc	ix
	inc	ix
	ld	a, (bf_k)
	inc	a
	ld	(bf_k), a
	cp	a, #6
	jr	nz, 2$
	pop	ix
	ret

; band_ring_edge: A = リング番号、B = k → HL = リングの辺 k(r[k] → r[k+1])のスロット
band_ring_edge:
	ld	c, a
	add	a, a
	add	a, c
	add	a, a			; r * 6
	add	a, b
	push	af			; id
	ld	a, c
	call	ring_ptr		; HL = r[0]
	ld	a, b
	add	a, a
	add	a, a
	ld	e, a
	ld	d, #0
	add	hl, de			; r[k]
	push	hl
	ld	a, c
	call	ring_ptr		; HL = r[0]
	ld	a, b
	inc	a
	cp	a, #6
	jr	nz, 1$
	xor	a, a
1$:
	add	a, a
	add	a, a
	ld	e, a
	ld	d, #0
	add	hl, de
	ex	de, hl			; DE = r[k+1]
	pop	hl
	pop	af
	jp	slot_ensure

; void hex_fast(u8 r)   A = リング番号。リングの六角形を g_color で塗る(リングの辺を共有)
_hex_fast::
	ld	(bf_a), a
	ld	a, #ROWS
	ld	(sc_r0), a
	xor	a, a
	ld	(sc_r1), a
	ld	(bf_k), a
1$:
	ld	a, (bf_k)
	ld	b, a
	ld	a, (bf_a)
	call	band_ring_edge
	xor	a, a
	call	seg_copy
	ld	a, (bf_k)
	inc	a
	ld	(bf_k), a
	cp	a, #6
	jr	nz, 1$
	jp	sc_fill

; i16 qmul(i16 v, u16 q)   HL = v(8.8 m)、DE = q(2^23 / z)→ DE = v * q >> 14(画面上の長さ 12.4、±15000 に飽和)
;   リング面の飾り(env.c)を、リング中心の投影から 2D で求めるのに使う
_qmul::
	ld	b, d
	ld	c, e
	jp	p_scale
