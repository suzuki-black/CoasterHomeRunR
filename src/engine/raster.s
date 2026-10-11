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
	.r800				; IYL(edge)
	.include "r800.inc"

	.globl	div_core
	.globl	_recip9
	.globl	_g_color
	.globl	_g_buf_hi

ROWS	= 46
FILLBLK	= 0x0100		; 塗りの展開ブロック(ページ0の RAM。sys_page0_ram が _fillblk_tpl を写す)
FILLJP	= FILLBLK + 2
FILLEND	= FILLJP + 3 + 64
; 各行の左右端(0..64)。256 境界に置き、fill が inc d / dec d だけで左右を行き来する
_span_a	== 0xDE00
_span_b	== 0xDF00
SPAN_A_HI = 0xDE

; ROWCLAMP: HL = y(12.4)→ A = (y + 7) >> 4 を 0..ROWS に丸めた値(関数呼び出しをしない)。DE・HL を壊す
;   0 <= y < 768 なら (y + 7) << 4 の上位バイトが答え(y + 7 < 4096 なので桁あふれしない)
	.macro	ROWCLAMP ?rc_n, ?rc_b, ?rc_d
	bit	7, h
	jr	nz, rc_n
	ld	a, h
	cp	a, #3
	jr	nc, rc_b			; y >= 768 → 48 行以上
	ld	de, #7
	add	hl, de
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	a, h
	cp	a, #ROWS + 1
	jr	c, rc_d
rc_b:
	ld	a, #ROWS
	jr	rc_d
rc_n:
	xor	a, a
rc_d:
	.endm

; PSCALE: DE = clamp((HL * BC) >> 14, ±15000)(HL 符号付き、BC = q)。BC は保つ。A・HL・F を壊す
;   負の数は絶対値で掛けて最後に符号を戻す(0 へ向けて切り捨て)。呼び出しの多い所(1 フレーム 170〜240 回)なので展開する
	.macro	PSCALE	?neg, ?sat, ?satn, ?done
	bit	7, h
	jr	nz, neg
	MULUW_HL_BC			; DE:HL
	ld	a, d
	cp	a, #0x0E
	jr	nc, sat			; 14336 以上は飽和
	add	hl, hl			; DE:HL >> 14 → DE(= DE << 2 | H >> 6)
	rl	e
	rl	d
	add	hl, hl
	rl	e
	rl	d
	jr	done
sat:
	ld	de, #15000
	jr	done
neg:
	xor	a, a
	sub	a, l
	ld	l, a
	sbc	a, a
	sub	a, h
	ld	h, a
	MULUW_HL_BC
	ld	a, d
	cp	a, #0x0E
	jr	nc, satn
	add	hl, hl
	rl	e
	rl	d
	add	hl, hl
	rl	e
	rl	d
	xor	a, a
	sub	a, e
	ld	e, a
	sbc	a, a
	sub	a, d
	ld	d, a
	jr	done
satn:
	ld	de, #-15000
done:
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
; edge: e_x0,e_y0(上端)→ e_x1,e_y1(下端)が覆う行の x を e_dst[行] に書き、e_rs = 最初の行、e_cnt = 行数(行があるとき)
;   IY は壊してよい。IX は壊さない(slot_ensure の呼び出し元が使う)
;   ほとんどの辺は「始点・終点の x が ±1792 以内、t < 4096、傾きが 128 ブロック/行 未満」の速い経路に乗る。
;   速い経路はここで、途中の値をメモリに置かずに計算する(R800 はデータのメモリアクセスのたびにページが変わって待たされる)。
;   乗らない辺は edge_gen(はじめから、一般の経路も含めて計算する)。どちらも同じ値を書く
; ---------------------------------------------------------------------------
edge::
	ld	a, (e_x0 + 1)
	add	a, #7
	cp	a, #14
	jp	nc, edge_gen		; 上位バイトが 0..6 か 0xF9..0xFF でなければ ±1792 の外
	ld	a, (e_x1 + 1)
	add	a, #7
	cp	a, #14
	jp	nc, edge_gen
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
	ld	(e_cnt), a
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
	ld	a, h
	cp	a, #0x10
	jp	nc, edge_gen		; t >= 4096
	push	hl			; t
	; ---- |dx| と符号
	ld	hl, (e_x1)
	push	de
	ld	de, (e_x0)
	or	a, a
	sbc	hl, de
	pop	de			; DE = y0
	ld	a, h
	ld	(e_neg), a		; bit7 = 左へ
	bit	7, h
	jr	z, 1$
	xor	a, a
	sub	a, l
	ld	l, a
	sbc	a, a
	sub	a, h
	ld	h, a
1$:
	push	hl			; |dx|
	; ---- dy = y1 - y0 を 9bit の仮数 m(256..511)にそろえる。k = 8 + ずらした回数(裏の A に置く)
	ld	hl, (e_y1)
	or	a, a
	sbc	hl, de
	ld	b, #8
2$:
	ld	a, h
	cp	a, #2
	jr	c, 4$
	srl	h
	rr	l
	inc	b
	jr	2$
4$:
	bit	0, h
	jr	nz, 5$
	add	hl, hl
	dec	b
	jr	4$
5$:
	ld	a, b
	ex	af, af'			; A' = k(割り込み処理は裏のレジスタを使わない)
	ld	h, #0
	add	hl, hl
	ld	de, #_recip9
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	pop	bc			; |dx|
	MULUW_HL_BC			; DE:HL = P = |dx| * recip9[m]
	; ---- step88 = P >> (k + 8)
	ex	af, af'
	sub	a, #8
	jr	c, 7$
	ex	de, hl			; HL = P >> 16
	jr	z, 9$
	ld	b, a
6$:
	srl	h
	rr	l
	djnz	6$
	jr	9$
7$:
	neg				; k < 8: 左へ 8 - k ずらしてから上位 16bit
	ld	b, a
8$:
	add	hl, hl
	rl	e
	rl	d
	djnz	8$
	ex	de, hl
9$:
	bit	7, h
	jr	z, 10$
	pop	hl			; 傾き >= 128 ブロック/行: 一般の経路(はじめから)
	jp	edge_gen
10$:
	; ---- off88 = step88 * (t << 4) >> 8(MULUW の中央 2 バイト)
	ld	b, h
	ld	c, l			; BC = |step88|
	pop	hl			; t
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	MULUW_HL_BC
	ld	l, h
	ld	h, e
	ex	de, hl			; DE = off88
	; ---- x88 = (x0 + 8) << 4 ± off88、DE = ±step88
	ld	hl, (e_x0)
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	a, l
	add	a, #0x80		; + 8 << 4
	ld	l, a
	jr	nc, 11$
	inc	h
11$:
	ld	a, (e_neg)
	rla
	jr	c, 12$
	add	hl, de
	ld	d, b
	ld	e, c
	jr	13$
12$:
	or	a, a
	sbc	hl, de
	xor	a, a
	sub	a, c
	ld	e, a
	sbc	a, a
	sub	a, b
	ld	d, a
13$:
	; ---- 書く: BC = 書き込み先(e_dst + rs。行は 256 バイトの中で収まる)、IYL = 行数
	ld	a, (e_dst + 1)
	ld	b, a
	ld	a, (e_rs)
	ld	c, a
	ld	a, (e_dst)
	add	a, c
	ld	c, a
	ld	a, (e_cnt)
	ld	iyl, a
	; 始点・終点とも画面の中(0 <= x < 1024)なら丸めずに書くだけ
	ld	a, (e_x0 + 1)
	cp	a, #4
	jr	nc, 16$
	ld	a, (e_x1 + 1)
	cp	a, #4
	jr	nc, 16$
14$:
	ld	a, h
	ld	(bc), a
	inc	c
	add	hl, de
	dec	iyl
	jr	nz, 14$
	ret
16$:
	ld	a, h
	bit	7, a
	jr	nz, 18$
	cp	a, #65
	jr	c, 19$
	ld	a, #64
	jr	19$
18$:
	xor	a, a
19$:
	ld	(bc), a
	inc	c
	add	hl, de
	dec	iyl
	jr	nz, 16$
	ret

; edge_gen: edge のはじめからの計算(一般の経路を含む。速い経路に乗る辺も同じ値になる)
edge_gen:
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
	; ---- 両端とも画面の左に 16 ブロック以上外(x < -256)なら全部の行が 0、右に 16 ブロック以上外(x >= 1280)なら 64。
	;   傾きの誤差(逆数表の仮数 9bit、|dx| / 256 = 128 以内)では値が変わらない。仮リングの頂点のような画面のずっと外の辺
	ld	b, a			; B = 行数
	ld	a, (e_x0 + 1)
	cp	a, #0x80
	jr	c, 60$
	cp	a, #0xFF
	jr	z, 69$			; -256 <= x0 < 0
	ld	a, (e_x1 + 1)
	cp	a, #0x80
	jr	c, 69$
	cp	a, #0xFF
	jr	z, 69$
	xor	a, a
	jr	65$
60$:
	cp	a, #5
	jr	c, 69$			; x0 < 1280
	ld	a, (e_x1 + 1)
	cp	a, #5
	jr	c, 69$
	cp	a, #0x80
	jr	nc, 69$
	ld	a, #64
65$:
	ld	hl, (e_dst)
	ld	e, c
	ld	d, #0
	add	hl, de
66$:
	ld	(hl), a
	inc	hl
	djnz	66$
	ret
69$:
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
	; 詰めたバッファ(gfx.s の並び)へ塗る。行ごとに左右を決め、展開した書き込みの途中へ飛ぶ。
	;   奇数の端は半バイトだけ読み書きする。横に隣り合うバイトは +8(展開ブロックは ld (hl),a / add hl,bc の 32 組)
	push	ix
	ld	ix, #FILLBLK		; IXH = 展開ブロックの上位(行ごとに IXL だけ書いて jp (ix))
	ld	a, c
	add	a, b
	ld	(f_endi + 1), a		; 終わりの行(行ごとの比べる即値)
	; 色とバッファの上位バイトを、行ごとの命令の即値へ書き込んでおく(自己書き換え。行ごとにメモリから読まない)
	ld	a, (_g_buf_hi)
	ld	(f_buf + 1), a
	ld	a, (_g_color)
	and	a, #0x0F
	ld	(f_lo + 1), a
	ld	b, a
	add	a, a
	add	a, a
	add	a, a
	add	a, a
	ld	(f_hi + 1), a
	or	a, b
	ld	(f_cc + 1), a		; 両方のブロックが同じ色のバイト
	ld	d, #SPAN_A_HI
	ld	e, c
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
	; H = バッファ + (行 >> 3)、L = (左端 >> 1) * 8 + (行 & 7)
	ld	a, e
	rrca
	rrca
	rrca
	and	a, #0x1F
f_buf:
	add	a, #0			; + バッファの上位バイト(書き換える)
	ld	h, a
	ld	a, c
	and	a, #0xFE
	add	a, a
	add	a, a
	ld	l, a
	ld	a, e
	and	a, #7
	or	a, l
	ld	l, a
	bit	0, c
	jr	z, fill_even
	; 左端が奇数: そのバイトの右半分だけ
	ld	a, (hl)
	and	a, #0xF0
f_lo:
	or	a, #0			; 色(書き換える)
	ld	(hl), a
	ld	a, l
	add	a, #8
	ld	l, a
	dec	b
	jr	z, fill_next
fill_even:
	ld	a, b
	srl	a			; 丸ごと塗るバイトの数、キャリー = 右端に半分が残る
	jr	z, fill_tail		; 丸ごとのバイトが無ければキャリーはそのまま
	ld	b, a
	ex	af, af'			; キャリーを裏の F に置く(展開ブロックの add hl,bc がキャリーを壊すため。割り込みは裏を使わない)
	ld	a, b
	add	a, a
	ld	b, a
	ld	a, #<FILLEND
	sub	a, b
	ld	ixl, a			; 飛び先 = 終わり - 数 * 2(ブロックは 256 境界の中なので下位だけ)
	ld	bc, #8
f_cc:
	ld	a, #0			; 両方のブロックが同じ色のバイト(書き換える)
	jp	(ix)			; 展開ブロックの途中へ
fill_ret::
	ex	af, af'			; 裏に置いたキャリー
fill_tail:
	jr	nc, fill_next
	; 右端が奇数: そのバイトの左半分だけ
	ld	a, (hl)
	and	a, #0x0F
f_hi:
	or	a, #0			; 色 << 4(書き換える)
	ld	(hl), a
fill_next:
	inc	e
	ld	a, e
f_endi:
	cp	a, #0			; 終わりの行(書き換える)
	jp	nz, fill_row
fill_done::				; 計測用(塗りの終わりを捕まえる)
	pop	ix
	ret


; 展開した 32 回の書き込みの元(sys_page0_ram がページ0の FILLBLK = 0x0100 へ写す)。
;   256 境界の中で動くので、飛び先は下位バイトだけを書き換える
_fillblk_tpl::
	ld	a, #0			; FILLBLK(使わない。fill は色を A に入れて、下の展開の途中へ jp (ix) で飛ぶ)
	jp	0x0100			; FILLJP(使わない)
	.rept	32
	ld	(hl), a
	add	hl, bc
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
	; q = 2^23 / z を逆数表で。z を 9bit の仮数 m(256..511)にそろえ(右へ k 回)、q = recip9[m] >> (k + 1)。
	; z < 256(0.5〜1m)は左へ 1 回そろえて q = recip9[m]。k は上位バイトの桁で決め、シフトはループせずに並べる
	ld	a, h
	cp	a, #2
	jr	c, 20$
	cp	a, #8
	jr	c, 21$
	cp	a, #32
	jr	c, 22$
	cp	a, #64
	jp	c, 25$
	jp	26$
21$:
	cp	a, #4
	jr	c, 31$
	jp	32$
22$:
	cp	a, #16
	jp	c, 33$
	jp	34$
20$:
	or	a, a
	jr	nz, 30$
	; z < 256: 左へ 1 回
	add	hl, hl
	ld	h, #0
	add	hl, hl
	ld	de, #_recip9
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a			; HL = recip9[m]
	jp	14$
30$:
	ld	h, #0
	add	hl, hl
	ld	de, #_recip9
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a			; HL = recip9[m]
	srl	h
	rr	l
	jp	14$
31$:
	srl	h
	rr	l
	ld	h, #0
	add	hl, hl
	ld	de, #_recip9
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a			; HL = recip9[m]
	srl	h
	rr	l
	srl	h
	rr	l
	jp	14$
32$:
	srl	h
	rr	l
	srl	h
	rr	l
	ld	h, #0
	add	hl, hl
	ld	de, #_recip9
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a			; HL = recip9[m]
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	jp	14$
33$:
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	ld	h, #0
	add	hl, hl
	ld	de, #_recip9
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a			; HL = recip9[m]
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	jp	14$
34$:
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	ld	h, #0
	add	hl, hl
	ld	de, #_recip9
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a			; HL = recip9[m]
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	jp	14$
25$:
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	ld	h, #0
	add	hl, hl
	ld	de, #_recip9
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a			; HL = recip9[m]
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	jp	14$
26$:
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	ld	h, #0
	add	hl, hl
	ld	de, #_recip9
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a			; HL = recip9[m]
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
14$:
	ld	(_p_q), hl
	ld	b, h
	ld	c, l			; BC = q(PSCALE の間は保たれる)
	pop	hl			; y
	PSCALE
	ld	hl, #368
	or	a, a
	sbc	hl, de			; sy
	ex	(sp), hl		; スタック = sy、HL = x
	PSCALE
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
bf_ck:		.ds	1	; band_fast: 塗る面のビット(ループで 1 つずつ右へ送る)
bf_cr:		.ds	1	; band_fast: 使う放射の辺のビット(同上)

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
;   このフレームでまだなら、スロットを割り当てて辺 p→q の行ごとの x を計算して置く。IX は壊さない、IY は壊す
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
	; --- 作る(1 本 約 330 → 約 200 サイクル: 関数を呼ばず、座標は ldir で写さず 16bit で直接、見出しは 1 回だけ書く)
	; スロットの先頭 = ESLOT + A * 64(64 境界)
	rrca
	rrca
	ld	c, a
	and	a, #0xC0
	ld	l, a
	ld	a, c
	and	a, #0x3F
	add	a, #>ESLOT
	ld	h, a
	ld	(e_dst), hl
	pop	hl			; p
	; p をいったん上端として e_x0 / e_y0 に置く
	ld	c, (hl)
	inc	hl
	ld	b, (hl)
	inc	hl
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a			; HL = p.y
	ld	(e_x0), bc
	ld	(e_y0), hl
	ex	de, hl			; HL = q、DE = p.y
	ld	c, (hl)
	inc	hl
	ld	b, (hl)			; BC = q.x
	inc	hl
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a			; HL = q.y
	push	hl
	or	a, a
	sbc	hl, de			; q.y - p.y
	pop	hl
	jr	z, 7$			; 水平: 行なし
	jp	m, 2$
	; 下り(p → q): 作ったときの向き = 1
	ld	(e_x1), bc
	ld	(e_y1), hl
	ld	a, #1
	jr	3$
2$:
	; 上り: q を上端に入れ替える。向き = 0
	ld	(e_y1), de		; p.y
	ld	de, (e_x0)
	ld	(e_x1), de		; p.x
	ld	(e_x0), bc
	ld	(e_y0), hl
	xor	a, a
3$:
	ld	hl, (e_dst)
	ld	e, a
	ld	a, l
	or	a, #48
	ld	l, a
	ld	(hl), e			; [48] 向き
	xor	a, a
	ld	(e_cnt), a
	call	edge			; edge が壊すのは IY だけ(IY は呼び出し元の band_fast / hex_fast が帯ごとに保存する)
	; [46] 最初の行、[47] 最後の行 + 1(行が無ければ 0, 0)
	ld	hl, (e_dst)
	ld	a, l
	or	a, #46
	ld	l, a
	ld	a, (e_cnt)
	or	a, a
	jr	z, 4$
	ld	b, a
	ld	a, (e_rs)
	ld	(hl), a
	inc	l
	add	a, b
	ld	(hl), a
	ld	hl, (e_dst)
	ret
7$:
	; 水平: 行なし、向き 0
	ld	hl, (e_dst)
	ld	a, l
	or	a, #48
	ld	l, a
	ld	(hl), #0
	dec	l
	dec	l
4$:
	xor	a, a
	ld	(hl), a
	inc	l
	ld	(hl), a
	ld	hl, (e_dst)
	ret
8$:
	pop	hl
	dec	a
	jp	slot_addr

; seg_copy: HL = スロット、A = 1 なら作ったときと逆向きに使う
;   面の中での向きが下りなら span_a、上りなら span_b へ、覆う行の x をコピーし、塗る範囲(裏の B..C)を広げる
seg_copy:
	; スロットは 64 境界なので、見出し [46] の番地は下位バイトの or で作れる(push / pop しない)
	ld	c, a
	ld	d, h
	ld	a, l
	or	a, #46
	ld	e, a
	ld	a, (de)
	ld	b, a			; rs
	inc	e
	ld	a, (de)			; re
	inc	e
	sub	a, b
	ret	z
	ret	c
	ex	af, af'			; A' = 行数
	ld	a, (de)			; 作ったときの向き
	xor	a, c
	ld	c, #SPAN_A_HI
	jr	nz, 1$
	inc	c			; span_b
1$:
	; 範囲を広げる(裏の B = 最初の行、裏の C = 最後の行 + 1。割り込み処理は裏のレジスタを使わない)
	ld	a, b
	exx
	cp	a, b
	jr	nc, 2$
	ld	b, a
2$:
	exx
	ex	af, af'
	ld	e, a			; 行数
	add	a, b			; re
	exx
	cp	a, c
	jr	c, 3$
	ld	c, a
3$:
	exx
	ld	a, l
	add	a, b
	ld	l, a			; スロット + rs(64 境界の中)
	ld	d, c
	ld	c, e			; 行数
	ld	e, b			; DE = span + rs
	ld	b, #0
	ldir
	ret

; sc_fill: 裏の B..C の行を g_color で塗り、範囲を空に戻す
sc_fill:
	exx
	push	bc			; B = 最初の行、C = 最後の行 + 1
	ld	bc, #ROWS << 8		; 範囲を空に戻す
	exx
	pop	bc
	ld	a, c
	sub	a, b
	ret	z
	ret	c
	ld	c, b
	ld	b, a
	jp	fill

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

; band_fast の辺の用意(リング a・b の辺 k と、放射 k)。IX = sl_tbl + k * 2
	.macro	BF_RING
	; リング a の辺 k: a[k] → a[k+1]
	ld	a, (bf_k)
	ld	b, a
	ld	hl, (bf_pa)
	ld	a, (bf_a)
	call	band_ring_edge
	ld	0 (ix), l
	ld	1 (ix), h
	; リング b の辺 k
	ld	a, (bf_k)
	ld	b, a
	ld	hl, (bf_pb)
	ld	a, (bf_b)
	call	band_ring_edge
	ld	12 (ix), l
	ld	13 (ix), h
	.endm
	.macro	BF_RADIAL
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
	.endm

; ---------------------------------------------------------------------------
; void band_fast(u8 a, u8 b)   A = 手前のリング番号、L = 奥のリング番号
;   2 本のリングの間の 6 面(面 k = a[k], a[k+1], b[k+1], b[k])を色 g_band_col[k] で塗る。
;   全頂点が手前(投影済み)であること。
; ---------------------------------------------------------------------------
_band_fast::
	push	ix
	push	iy			; slot_ensure(edge)が IY を壊す
	ld	(bf_a), a
	ld	a, l
	ld	(bf_b), a
	call	ring_ptr
	ld	(bf_pb), hl
	ld	a, (bf_a)
	call	ring_ptr
	ld	(bf_pa), hl
	; --- 塗る面が使う辺だけを用意する(計算済みならスロットを引くだけ)
	;   面 k はリング a の辺 k・リング b の辺 k・放射 k・放射 k+1 を使う。色 0 の面(屋外の空など)の辺は作らない
	;   (遊園地で 1 フレーム 54 本 → 40 本)。使わない sl_tbl の欄は、2 つ目のループでも読まない
	; 塗る面のビット(bit k = 面 k の色が 0 でない)と、使う放射の辺のビット(bit k = 面 k か面 k-1 を塗る)を先に作る
	ld	hl, (_g_band_col)
	ld	b, #6
	ld	c, #0
14$:
	ld	a, (hl)
	inc	hl
	cp	a, #1			; 0 ならキャリー
	ccf
	rr	c			; 面 0 が bit2、面 5 が bit7 に入る
	djnz	14$
	ld	a, c
	rrca
	rrca				; bit k = 面 k
	ld	(bf_ck), a
	ld	c, a
	add	a, a			; bit k = 面 k-1(k = 1..5)
	ld	b, a
	ld	a, c
	rlca
	rlca
	rlca				; 面 5 → bit0
	and	a, #1
	or	a, b
	or	a, c
	ld	(bf_cr), a
	ld	ix, #sl_tbl
	xor	a, a
	ld	(bf_k), a
	ld	a, (bf_ck)
	cp	a, #0x3F
	jr	nz, 1$
	; 6 面とも塗る帯(洞窟・坑道など): ビットを確かめずに 18 本とも用意する
15$:
	BF_RING
	BF_RADIAL
	inc	ix
	inc	ix
	ld	a, (bf_k)
	inc	a
	ld	(bf_k), a
	cp	a, #6
	jr	nz, 15$
	jr	16$
	; 塗らない面がある帯: 塗る面が使う辺だけ
1$:
	ld	a, (bf_ck)
	rrca
	ld	(bf_ck), a
	jr	nc, 12$
	BF_RING
12$:
	ld	a, (bf_cr)
	rrca
	ld	(bf_cr), a
	jr	nc, 13$
	BF_RADIAL
13$:
	inc	ix
	inc	ix
	ld	a, (bf_k)
	inc	a
	ld	(bf_k), a
	cp	a, #6
	jp	nz, 1$
16$:
	; --- 面(ここからは辺を作らない: edge_gen が裏のレジスタを使うので、塗る範囲を裏に置けるのは辺を作り終えてから)
	exx
	ld	bc, #ROWS << 8		; 塗る範囲を空に
	exx
	xor	a, a
	ld	ix, #sl_tbl
	ld	(bf_k), a
2$:
	; 面の色。0 の面は塗らない(屋外の区間の空・落下中の床など)
	ld	hl, (_g_band_col)
	ld	e, a
	ld	d, #0
	add	hl, de
	ld	a, (hl)
	or	a, a
	jr	z, 4$
	ld	(_g_color), a
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
	call	sc_fill
4$:
	inc	ix
	inc	ix
	ld	a, (bf_k)
	inc	a
	ld	(bf_k), a
	cp	a, #6
	jp	nz, 2$
	pop	iy
	pop	ix
	ret

; band_ring_edge: A = リング番号、B = k、HL = リングの頂点の先頭(g_ring_s + リング番号 * 24。band_fast が計算済み)
;   → HL = リングの辺 k(r[k] → r[k+1])のスロット
band_ring_edge:
	ld	c, a
	add	a, a
	add	a, c
	add	a, a			; r * 6
	add	a, b
	push	af			; id
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
	push	hl
	add	hl, de			; r[k+1]
	ex	(sp), hl		; スタック = r[k+1]、HL = 先頭
	ld	a, b
	add	a, a
	add	a, a
	ld	e, a
	add	hl, de			; r[k]
	pop	de			; DE = r[k+1]
	pop	af			; A = id
	jp	slot_ensure

; void hex_fast(u8 r)   A = リング番号。リングの六角形を g_color で塗る(リングの辺を共有)
_hex_fast::
	push	ix
	push	iy			; slot_ensure(edge)が IY を壊す
	ld	(bf_a), a
	call	ring_ptr
	ld	(bf_pa), hl
	; 先に 6 本の辺を用意する(辺を作る edge_gen は裏のレジスタを使うので、写して塗る範囲を裏に置くのはその後)
	ld	ix, #sl_tbl
	xor	a, a
	ld	(bf_k), a
1$:
	ld	b, a
	ld	hl, (bf_pa)
	ld	a, (bf_a)
	call	band_ring_edge
	ld	0 (ix), l
	ld	1 (ix), h
	inc	ix
	inc	ix
	ld	a, (bf_k)
	inc	a
	ld	(bf_k), a
	cp	a, #6
	jr	nz, 1$
	exx
	ld	bc, #ROWS << 8		; 塗る範囲を空に
	exx
	ld	ix, #sl_tbl
	ld	b, #6
2$:
	push	bc
	ld	l, 0 (ix)
	ld	h, 1 (ix)
	xor	a, a
	call	seg_copy
	inc	ix
	inc	ix
	pop	bc
	djnz	2$
	call	sc_fill
	pop	iy
	pop	ix
	ret

; ---------------------------------------------------------------------------
; リング面の飾り(env.c)の点と長方形。点 = リングの中心 + (u, v) * q(ring_c[idx] = 中心の x, y と q)
;   void rpt_a(u8 i, u8 idx)   A = i、L = idx。scr[i] = (cx + g_pu * q >> 14, cy - g_pv * q >> 14)(PSCALE。±15000 に飽和)
;   void rquad_a(u8 idx)       A = idx。u0..u1 × v0..v1(g_qu0, g_qu1, g_qv0, g_qv1)を g_color で塗る
; リング面の形は画面上でそのまま縮小されるだけなので、四角形は必ず軸に沿った長方形になる。
; rquad_a は多角形の塗り(raster)を通さず、raster と同じ丸め(列 = (x + 8) >> 4、行 = (y + 7) >> 4、
; どちらも画面に収める)で rect_fill に渡す。辺の計算と多角形の準備が要らず、掛け算も 8 回 → 4 回(u・v の 2 通りずつ)
; ---------------------------------------------------------------------------
	.globl	_ring_c, _rect_fill, _g_rx, _g_ry, _g_rw, _g_rh

	.area	_DATA
_g_pu::		.ds	2
_g_pv::		.ds	2
_g_qu0::	.ds	2
_g_qu1::	.ds	2
_g_qv0::	.ds	2
_g_qv1::	.ds	2
rp_cx:		.ds	2
rp_cy:		.ds	2
rp_q:		.ds	2
_g_rp_last::	.ds	1	; rp_ring が覚えているリング(0xFF = なし)
rp_i:		.ds	1
	.area	_CODE

; L = idx → rp_cx, rp_cy、BC = q。同じリングが続く(飾りは同じリングの点・長方形を続けて描く)ときは表を読まない。
;   ring_make(geom.s)がリングを作り直すたびに g_rp_last を 0xFF(覚えていない)にする
rp_ring:
	ld	a, l
	ld	hl, #_g_rp_last
	cp	a, (hl)
	jr	nz, 1$
	ld	bc, (rp_q)
	ret
1$:
	ld	(hl), a
	ld	l, a
	ld	h, #0
	ld	e, l
	ld	d, h
	add	hl, hl
	add	hl, de
	add	hl, hl			; idx * 6
	ld	de, #_ring_c
	add	hl, de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	inc	hl
	ld	(rp_cx), de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	inc	hl
	ld	(rp_cy), de
	ld	c, (hl)
	inc	hl
	ld	b, (hl)
	ld	(rp_q), bc
	ret

_rpt_a::
	ld	(rp_i), a
	call	rp_ring
	ld	hl, (_g_pu)
	PSCALE				; BC は保たれる
	ld	hl, (rp_cx)
	add	hl, de
	push	hl			; x
	ld	hl, (_g_pv)
	PSCALE
	ld	hl, (rp_cy)
	or	a, a
	sbc	hl, de
	ex	de, hl			; DE = y
	ld	a, (rp_i)
	add	a, a
	add	a, a
	ld	l, a
	ld	h, #0
	ld	bc, #_scr
	add	hl, bc
	pop	bc
	ld	(hl), c
	inc	hl
	ld	(hl), b
	inc	hl
	ld	(hl), e
	inc	hl
	ld	(hl), d
	ret

; HL = x(12.4)→ A = (x + 8) >> 4 を 0..64 に収めた値(raster の辺と同じ丸め)
rq_xcol:
	ld	de, #8
	add	hl, de
	bit	7, h
	jr	nz, 2$
	ld	a, h
	cp	a, #0x04
	jr	nc, 1$			; 1024 以上 → 64
	ld	a, l
	rrca
	rrca
	rrca
	rrca
	and	a, #0x0F
	ld	l, a
	ld	a, h
	rlca
	rlca
	rlca
	rlca
	or	a, l
	cp	a, #65
	ret	c
1$:
	ld	a, #64
	ret
2$:
	xor	a, a
	ret

_rquad_a::
	ld	l, a
	call	rp_ring			; BC = q
	ld	hl, (_g_qu0)
	PSCALE
	ld	hl, (rp_cx)
	add	hl, de
	push	hl			; x0
	ld	hl, (_g_qu1)
	PSCALE
	ld	hl, (rp_cx)
	add	hl, de
	push	hl			; x1
	ld	hl, (_g_qv0)
	PSCALE
	ld	hl, (rp_cy)
	or	a, a
	sbc	hl, de
	push	hl			; y0
	ld	hl, (_g_qv1)
	PSCALE
	ld	hl, (rp_cy)
	or	a, a
	sbc	hl, de			; HL = y1
	pop	de			; DE = y0
	; 行: 上 = min(y0, y1)、下 = max(y0, y1)
	push	hl
	or	a, a
	sbc	hl, de			; y1 - y0
	pop	hl
	jp	p, 1$
	ex	de, hl
1$:
	; DE = 上、HL = 下
	push	de
	ROWCLAMP			; A = 下の行(含まない)
	pop	hl
	ld	c, a
	ROWCLAMP			; A = 上の行
	ld	b, a
	ld	a, c
	sub	a, b
	jr	z, 8$
	jr	c, 8$
	ld	(_g_rh), a
	ld	a, b
	ld	(_g_ry), a
	; 列: 左 = min(x0, x1)、右 = max(x0, x1)
	pop	hl			; x1
	pop	de			; x0
	push	hl
	or	a, a
	sbc	hl, de			; x1 - x0
	pop	hl
	jp	p, 2$
	ex	de, hl
2$:
	; DE = 左、HL = 右
	push	de
	call	rq_xcol
	pop	hl
	ld	c, a			; C = 右の列(含まない)
	push	bc
	call	rq_xcol			; A = 左の列
	pop	bc
	ld	b, a
	ld	a, c
	sub	a, b
	ret	z
	ret	c
	ld	(_g_rw), a
	ld	a, b
	ld	(_g_rx), a
	jp	_rect_fill
8$:
	pop	hl
	pop	hl
	ret


; ---------------------------------------------------------------------------
; void poly3(const V3 *v, u8 n, u8 color)   HL = v、スタックに n・color(戻るときに捨てる)
; void poly3p(const V3 *const *pv, u8 n, u8 color)   同じで、頂点をポインタの並びで渡す(分岐の線路: 頂点を写さずに済む)
;   ニアクリップ(Sutherland-Hodgman、z >= NEAR_Z を残す)して投影し、raster で塗る。
;   辺 a→b ごとに、a と b の片方だけが手前なら交点(a + (b - a) * (NEAR_Z - a.z) / (b.z - a.z)、muldiv)を、
;   b が手前なら b を出す。もと render.c の C 版と同じ計算・同じ順(C だと 1 回 約 3,100 サイクルかかっていた)
; ---------------------------------------------------------------------------
	.globl	_muldiv
P3_NEAR	= 128			; NEAR_Z(render.h)

	.area	_DATA
p3_cv:	.ds	6 * 10		; 切った後の頂点(V3)
p3_pl:	.ds	2 * 8		; poly3 の頂点のポインタの並び
p3_pp:	.ds	2		; 次の終点のポインタの位置
p3_a:	.ds	2		; 辺の始点
p3_b:	.ds	2		; 辺の終点
p3_o:	.ds	2		; 書き先
p3_t:	.ds	2		; NEAR_Z - a.z
p3_dz:	.ds	2		; b.z - a.z
p3_k:	.ds	1		; 残りの辺
p3_m:	.ds	1		; 切った後の頂点の数
p3_in:	.ds	1		; 1 = a が手前
p3_col:	.ds	1

	.area	_CODE
_poly3::
	pop	de
	pop	bc			; C = n、B = color
	push	de
	; 頂点のポインタの並びを作る
	ld	de, #p3_pl
	push	bc
	ld	b, c
21$:
	ld	a, l
	ld	(de), a
	inc	de
	ld	a, h
	ld	(de), a
	inc	de
	push	bc
	ld	bc, #6
	add	hl, bc
	pop	bc
	djnz	21$
	pop	bc
	ld	hl, #p3_pl
	jr	p3_core
_poly3p::
	pop	de
	pop	bc			; C = n、B = color
	push	de
p3_core:
	ld	a, b
	ld	(p3_col), a
	ld	a, c
	ld	(p3_k), a
	ld	(p3_pp), hl		; 終点は並びの頭から
	dec	a
	add	a, a
	ld	e, a
	ld	d, #0
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	ld	(p3_a), hl		; a = 並びの最後
	ld	de, #p3_cv
	ld	(p3_o), de
	xor	a, a
	ld	(p3_m), a
	call	p3_inz
	ld	(p3_in), a
1$:
	ld	hl, (p3_pp)
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	ld	(p3_b), hl
	call	p3_inz
	ld	c, a			; C = b が手前か
	ld	a, (p3_in)
	cp	a, c
	jr	z, 5$
	; 交点: t = NEAR_Z - a.z、dz = b.z - a.z
	push	bc
	ld	hl, (p3_a)
	ld	de, #4
	add	hl, de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)			; DE = a.z
	ld	hl, #P3_NEAR
	or	a, a
	sbc	hl, de
	ld	(p3_t), hl
	ld	hl, (p3_b)
	ld	bc, #4
	add	hl, bc
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a			; HL = b.z
	or	a, a
	sbc	hl, de
	ld	(p3_dz), hl
	ld	c, #0
	call	p3_lerp			; x
	ld	c, #2
	call	p3_lerp			; y
	ld	hl, (p3_o)
	ld	de, #4
	add	hl, de
	ld	(hl), #<P3_NEAR
	inc	hl
	ld	(hl), #>P3_NEAR
	inc	hl
	ld	(p3_o), hl
	ld	hl, #p3_m
	inc	(hl)
	pop	bc
5$:
	ld	a, c
	ld	(p3_in), a
	or	a, a
	jr	z, 6$
	; b を写す
	ld	hl, (p3_b)
	ld	de, (p3_o)
	ldi
	ldi
	ldi
	ldi
	ldi
	ldi
	ld	(p3_o), de
	ld	hl, #p3_m
	inc	(hl)
6$:
	ld	hl, (p3_b)
	ld	(p3_a), hl
	ld	hl, (p3_pp)
	inc	hl
	inc	hl
	ld	(p3_pp), hl
	ld	hl, #p3_k
	dec	(hl)
	jp	nz, 1$
	ld	a, (p3_m)
	cp	a, #3
	ret	c
	; 投影して塗る
	ld	b, a
	ld	hl, #p3_cv
	ld	de, #_scr
7$:
	push	bc
	push	hl
	push	de
	call	_project2		; HL = 点、DE = 出力
	pop	hl
	ld	de, #4
	add	hl, de
	ex	de, hl
	pop	hl
	ld	bc, #6
	add	hl, bc
	pop	bc
	djnz	7$
	ld	a, (p3_col)
	ld	l, a
	ld	a, (p3_m)
	jp	_raster

; p3_inz: HL = 頂点 → A = 1(z >= NEAR_Z)/ 0。符号つきの比較は上位ビットを反転して符号なしで比べる
p3_inz:
	ld	de, #4
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	ld	a, h
	xor	a, #0x80
	ld	h, a
	ld	de, #P3_NEAR ^ 0x8000
	or	a, a
	sbc	hl, de
	sbc	a, a			; 小さい = 0xFF
	inc	a
	ret

; p3_lerp: C = 成分の位置(0 = x、2 = y)→ o の成分 = a + muldiv(b - a, t, dz)
p3_lerp:
	ld	b, #0
	ld	hl, (p3_a)
	add	hl, bc
	ld	e, (hl)
	inc	hl
	ld	d, (hl)			; DE = a の成分
	push	de
	push	bc
	ld	hl, (p3_b)
	add	hl, bc
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	or	a, a
	sbc	hl, de			; HL = b - a
	ld	de, (p3_dz)
	push	de			; muldiv の 3 つ目(muldiv が捨てる)
	ld	de, (p3_t)
	call	_muldiv			; DE = (b - a) * t / dz
	pop	bc
	pop	hl
	add	hl, de
	ex	de, hl
	ld	hl, (p3_o)
	add	hl, bc
	ld	(hl), e
	inc	hl
	ld	(hl), d
	ret
