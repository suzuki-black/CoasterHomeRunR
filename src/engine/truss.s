; truss.s — 鉄橋の斜材の端の点と、海の白波(地形モジュール。env.c の truss_draw・sea_waves から呼ぶ)
;
; void truss_pt(void): 頂点 _tp_t(上弦材の下の頂点の画面座標 x, y)と _tp_f(床の頂点)の間の、区間の中の位置 _tp_g(0..255)の点を、
;   左右に太さ w を付けた 2 点として _tp_o[0..3] に書く(_tp_rev = 1 なら右の点を先に。四角形の頂点の順をそろえる)。
;   w = (床の y - 上の y) >> 5(遠くでも 8 = 半ブロック)。g = 0 なら床の頂点、255 なら上の頂点、それ以外は床 + (上 - 床) * g / 256。
; C で書くと(引数 5 つをスタックで渡す形)ボス戦の後ろ向きのフレームの 4% を使っていた(1 フレーム 12 回)。

	.module	truss
	.r800
	.include "r800.inc"

	.globl	_pset, _g_rx, _g_ry, _g_color

GAME_ROWS = 46
C_WAVE	= 14			; 白波の色(env.c の C_DIRT1)

	.area	_DATA
_wave_inv::	.ds	2 * 256	; 8192 / (24 + i)(白波の奥行き(1/4m)の逆数。env.c が初めて使うときに作る)
_sw_m::		.ds	1	; 白波の奥行きの進み(1/4m の下位 8 ビット。前を見ているときは負)
_sw_row::	.ds	1	; 地平線の行
sd_i:		.ds	1
sd_zi:		.ds	1
sd_s:		.ds	1
sd_inv:		.ds	2
_tp_t::		.ds	2
_tp_f::		.ds	2
_tp_o::		.ds	2
_tp_g::		.ds	1
_tp_rev::	.ds	1
tp_x:		.ds	2
tp_y:		.ds	2

	.area	_CODE

; HL = f + (t - f) * g / 256(HL = t - f の差、DE = f、A = g)。BC を壊す
lerp:
	push	de
	ld	c, a
	ld	b, #0
	bit	7, h
	jr	nz, 1$
	MULUW_HL_BC			; DE:HL = 差 * g
	ld	l, h
	ld	h, e
	jr	2$
1$:
	xor	a, a			; 負: 絶対値で掛けて符号を戻す(0 へ向けて切り捨て。fmul と同じ)
	sub	a, l
	ld	l, a
	sbc	a, a
	sub	a, h
	ld	h, a
	MULUW_HL_BC
	ld	l, h
	ld	h, e
	xor	a, a
	sub	a, l
	ld	l, a
	sbc	a, a
	sub	a, h
	ld	h, a
2$:
	pop	de
	add	hl, de
	ret

_truss_pt::
	push	ix
	push	iy
	; ---- 点(tp_x, tp_y)
	ld	a, (_tp_g)
	or	a, a
	jr	nz, 1$
	ld	hl, (_tp_f)		; g = 0: 床の頂点
	jr	2$
1$:
	inc	a
	jr	nz, 3$
	ld	hl, (_tp_t)		; g = 255: 上の頂点
2$:
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	inc	hl
	ld	(tp_x), de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	ld	(tp_y), de
	jr	4$
3$:
	ld	ix, (_tp_t)
	ld	iy, (_tp_f)
	ld	l, 0 (ix)		; x = f.x + (t.x - f.x) * g / 256
	ld	h, 1 (ix)
	ld	e, 0 (iy)
	ld	d, 1 (iy)
	or	a, a
	sbc	hl, de
	ld	a, (_tp_g)
	call	lerp
	ld	(tp_x), hl
	ld	l, 2 (ix)		; y も同じ
	ld	h, 3 (ix)
	ld	e, 2 (iy)
	ld	d, 3 (iy)
	or	a, a
	sbc	hl, de
	ld	a, (_tp_g)
	call	lerp
	ld	(tp_y), hl
4$:
	; ---- 太さ w = (床の y - 上の y) >> 5、8 未満なら 8
	ld	ix, (_tp_t)
	ld	iy, (_tp_f)
	ld	l, 2 (iy)
	ld	h, 3 (iy)
	ld	e, 2 (ix)
	ld	d, 3 (ix)
	or	a, a
	sbc	hl, de
	sra	h
	rr	l
	sra	h
	rr	l
	sra	h
	rr	l
	sra	h
	rr	l
	sra	h
	rr	l
	ld	de, #8
	push	hl
	or	a, a
	sbc	hl, de
	pop	hl
	jp	p, 5$
	ex	de, hl
5$:
	ld	a, (_tp_rev)		; rev なら w = -w
	or	a, a
	jr	z, 6$
	xor	a, a
	sub	a, l
	ld	l, a
	sbc	a, a
	sub	a, h
	ld	h, a
6$:
	; ---- o[0] = x - w、o[1] = y、o[2] = x + w、o[3] = y
	ex	de, hl			; DE = w
	ld	ix, (_tp_o)
	ld	hl, (tp_x)
	or	a, a
	sbc	hl, de
	ld	0 (ix), l
	ld	1 (ix), h
	ld	hl, (tp_x)
	add	hl, de
	ld	4 (ix), l
	ld	5 (ix), h
	ld	hl, (tp_y)
	ld	2 (ix), l
	ld	3 (ix), h
	ld	6 (ix), l
	ld	7 (ix), h
	pop	iy
	pop	ix
	ret

; ---------------------------------------------------------------------------
; void sea_dots(void): 海の白波 20 個。海面は目の 12m 下として、白波 i の点(左右 = ((i*23)&31 - 16)*4 + 2、奥行き = (i*37 + sw_m)
;   の下位 8 ビット + 24。どちらも 1/4m)を投影し、1 行の短い線(遠いほど短く 3〜1 ブロック)で描く。
;   C で書くと 1 個 約 80µs かかっていた(1 フレーム 20 個)
; ---------------------------------------------------------------------------
_sea_dots::
	ld	a, #C_WAVE
	ld	(_g_color), a
	xor	a, a
	ld	(sd_i), a
sd_loop:
	ld	a, (sd_i)		; 奥行き zi = i * 37 + sw_m(256 で回る)
	ld	b, #37
	MULUB_A_B
	ld	a, (_sw_m)
	add	a, l
	ld	(sd_zi), a
	ld	l, a			; inv = wave_inv[zi]
	ld	h, #0
	add	hl, hl
	ld	de, #_wave_inv
	add	hl, de
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	ld	(sd_inv), hl
	ld	bc, #48			; 行 = 地平線 + 48 * inv / 256(12m × 32 ブロック / 奥行き)
	MULUW_HL_BC
	ld	a, e
	or	a, a
	jr	nz, sd_next
	ld	a, (_sw_row)
	add	a, h
	jr	c, sd_next
	cp	a, #GAME_ROWS
	jr	nc, sd_next
	ld	(_g_ry), a
	ld	a, (sd_i)		; 左右 lat = ((i * 23) & 31 - 16) * 4 + 2
	ld	b, #23
	MULUB_A_B
	ld	a, l
	and	a, #31
	sub	a, #16
	add	a, a
	add	a, a
	add	a, #2
	ld	(sd_s), a
	bit	7, a
	jr	z, 1$
	neg
1$:
	ld	c, a			; 横 = 32 ± |lat| * inv / 256
	ld	b, #0
	ld	hl, (sd_inv)
	MULUW_HL_BC
	ld	b, h
	ld	a, (sd_s)
	bit	7, a
	ld	a, #32
	jr	nz, 2$
	add	a, b
	jr	3$
2$:
	sub	a, b
	jr	c, sd_next
3$:
	ld	c, a			; C = 左端
	ld	a, (sd_zi)		; 長さ: 15m より手前 3、30m より手前 2、その先 1
	ld	b, #1
	cp	a, #96
	jr	nc, 4$
	inc	b
	cp	a, #36
	jr	nc, 4$
	inc	b
4$:
	ld	a, c
	add	a, b
	cp	a, #65
	jr	nc, sd_next
5$:
	ld	a, c
	ld	(_g_rx), a
	push	bc
	call	_pset
	pop	bc
	inc	c
	djnz	5$
sd_next:
	ld	a, (sd_i)
	inc	a
	ld	(sd_i), a
	cp	a, #20
	jp	nz, sd_loop
	ret
