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
;
; void sky_place(void): 空のスプライト 6 個(夕日・芯・雲 2・鳥 2)の属性を決める(env.c の envm_sky から呼ぶ)。
;   物 i の向き _sp_az[i](256 分割)、地平線からの高さ _sp_alt[i](ドット)、パターン _sp_pat[i] は envm_sky が決める。
;   番号・組・色・重なりを調べる範囲は g_sky_def[i](skydata.c)。画面の外、スコアと警告の行、車両の縁にかかるなら隠す。
;   空の色以外に重なるなら隠す(スプライトは常に手前に出るので)。重なりを調べるのは 1 フレームに 1 組(_sky_turn)だけで、
;   結果は次の順番まで使う。w = 0 の物は同じ組の直前の物の結果に従う。C で書くと 1 フレーム約 0.6ms だった

	.module	sky
	.r800
	.include "r800.inc"

	.globl	_g_buf_hi, _g_spr_attr, _g_sky_def, _cam_yaw

GAME_ROWS = 46
C_SKY	= 15			; 空の色(env.c)

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
_sp_az::	.ds	6
_sp_pat::	.ds	6
_sp_alt::	.ds	12
_sky_yh::	.ds	2	; 地平線の高さ(ドット。env.c の sky_draw)
_sky_turn::	.ds	1
sky_vis:	.ds	4
sp_i:		.ds	1
sp_x:		.ds	2
sp_y:		.ds	2
cl_x:		.ds	2
cl_y:		.ds	2
cl_w:		.ds	1
cl_h:		.ds	1
cl_r:		.ds	2
cl_j:		.ds	1
cl_i:		.ds	1

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

; ---------------------------------------------------------------------------
_sky_place::
	push	ix
	push	iy
	xor	a, a
1$:
	ld	(sp_i), a
	call	sky_put
	ld	a, (sp_i)
	inc	a
	cp	a, #6
	jr	c, 1$
	ld	a, (_sky_turn)
	inc	a
	and	a, #3
	ld	(_sky_turn), a
	pop	iy
	pop	ix
	ret

; 物 (sp_i) を置く。IX = g_sky_def[i](番号、組、色、ox、oy、w、h)、IY = 属性の 4 バイト
sky_put:
	ld	l, a
	ld	h, #0
	ld	e, l
	ld	d, h
	add	hl, hl
	add	hl, hl
	add	hl, hl
	or	a, a
	sbc	hl, de			; * 7
	ld	de, #_g_sky_def
	add	hl, de
	push	hl
	pop	ix
	ld	a, 0 (ix)
	add	a, a
	add	a, a
	ld	e, a
	ld	d, #0
	ld	iy, #_g_spr_attr
	add	iy, de
	ld	0 (iy), #209		; まず隠す
	; 向きの差 dz = az - カメラのヨー(-63〜63 のときだけ)
	ld	a, (sp_i)
	ld	e, a
	ld	hl, #_sp_az
	add	hl, de
	ld	a, (hl)
	ld	hl, #_cam_yaw
	sub	a, (hl)
	add	a, #63
	cp	a, #127
	ret	nc
	sub	a, #63
	; x = 112 + dz * 3(-32〜255 のときだけ)
	ld	l, a
	add	a, a
	sbc	a, a
	ld	h, a
	ld	d, h
	ld	e, l
	add	hl, hl
	add	hl, de
	ld	de, #112
	add	hl, de
	ld	(sp_x), hl
	ld	de, #32
	add	hl, de
	bit	7, h
	ret	nz
	ld	a, h
	or	a, a
	jr	z, 2$
	ld	a, l
	cp	a, #0x20
	ret	nc			; x + 32 >= 288
2$:
	; y = 地平線 - 高さ - 32
	ld	a, (sp_i)
	add	a, a
	ld	e, a
	ld	d, #0
	ld	hl, #_sp_alt
	add	hl, de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	ld	hl, (_sky_yh)
	or	a, a
	sbc	hl, de
	ld	de, #-32
	add	hl, de
	ld	(sp_y), hl
	; y + oy >= 24 かつ y + oy + h < 148 のときだけ(スコアと警告の行、車両の縁には出さない)
	ld	e, 4 (ix)
	ld	d, #0
	add	hl, de
	bit	7, h
	ret	nz
	ld	(cl_y), hl
	ld	a, h
	or	a, a
	jr	nz, 3$
	ld	a, l
	cp	a, #24
	ret	c
3$:
	ld	e, 6 (ix)
	add	hl, de
	ld	a, h
	or	a, a
	ret	nz
	ld	a, l
	cp	a, #148
	ret	nc
	; 重なりを調べる(w が 0 でなく、この組の順番のとき)
	ld	e, 1 (ix)
	ld	hl, #sky_vis
	add	hl, de
	ld	a, 5 (ix)
	or	a, a
	jr	z, 4$
	ld	a, (_sky_turn)
	cp	a, e
	jr	nz, 4$
	push	hl
	ld	a, 5 (ix)
	ld	(cl_w), a
	ld	a, 6 (ix)
	ld	(cl_h), a
	ld	e, 3 (ix)
	ld	hl, (sp_x)
	add	hl, de
	ld	(cl_x), hl
	call	sky_clear
	pop	hl
	ld	(hl), a
4$:
	ld	a, (hl)
	or	a, a
	ret	z
	; 置く(x が負なら EC: 32 ドット左へ)
	ld	c, 2 (ix)
	ld	hl, (sp_x)
	bit	7, h
	jr	z, 5$
	ld	de, #32
	add	hl, de
	set	7, c
5$:
	ld	1 (iy), l
	ld	3 (iy), c
	ld	a, (sp_i)
	ld	e, a
	ld	d, #0
	ld	hl, #_sp_pat
	add	hl, de
	ld	a, (hl)
	ld	2 (iy), a
	ld	a, (sp_y)
	dec	a			; y は 14〜147 なので 208 にはならない
	ld	0 (iy), a
	ret

; A = 0 / 1 / 2 → 0 / C >> 1 / C
half3:
	or	a, a
	ret	z
	dec	a
	ld	a, c
	ret	nz
	srl	a
	ret

; 範囲 (cl_x, cl_y)-(+cl_w, +cl_h) の 3×3 の点が、どれも空の色か(A = 1)。画面の左右の外の点は見ない。
; 上下がスコアと警告の行・車両の縁(行 6 未満・37 以上)にかかれば 0
sky_clear:
	xor	a, a
	ld	(cl_j), a
1$:
	ld	a, (cl_h)
	ld	c, a
	ld	a, (cl_j)
	call	half3
	ld	e, a
	ld	d, #0
	ld	hl, (cl_y)
	add	hl, de
	sra	h
	rr	l
	sra	h
	rr	l
	ld	(cl_r), hl
	xor	a, a
	ld	(cl_i), a
2$:
	ld	a, (cl_w)
	ld	c, a
	ld	a, (cl_i)
	call	half3
	ld	e, a
	ld	d, #0
	ld	hl, (cl_x)
	add	hl, de
	sra	h
	rr	l
	sra	h
	rr	l
	ld	a, h
	or	a, a
	jr	nz, 5$			; 列 < 0 か >= 256
	ld	a, l
	cp	a, #64
	jr	nc, 5$
	ld	b, a			; B = 列
	ld	hl, (cl_r)
	ld	a, h
	or	a, a
	jr	nz, 9$
	ld	a, l
	cp	a, #6
	jr	c, 9$
	cp	a, #37
	jr	nc, 9$
	ld	c, a			; C = 行
	rrca
	rrca
	rrca
	and	a, #0x1F
	ld	hl, #_g_buf_hi
	add	a, (hl)
	ld	h, a
	ld	a, b
	and	a, #0x3E
	add	a, a
	add	a, a
	ld	l, a
	ld	a, c
	and	a, #7
	or	a, l
	ld	l, a
	ld	a, (hl)
	bit	0, b
	jr	nz, 3$
	rrca
	rrca
	rrca
	rrca
3$:
	and	a, #0x0F
	cp	a, #C_SKY
	jr	nz, 9$
5$:
	ld	hl, #cl_i
	inc	(hl)
	ld	a, (hl)
	cp	a, #3
	jp	c, 2$
	ld	hl, #cl_j
	inc	(hl)
	ld	a, (hl)
	cp	a, #3
	jp	c, 1$
	ld	a, #1
	ret
9$:
	xor	a, a
	ret
