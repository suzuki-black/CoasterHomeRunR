; geom.s — 座標変換とトンネルのリング(ホットパス)
;
; C(SDCC)で書くと IX 相対のフレーム変数を 1 命令ごとに読み書きするコードになり、
; R800 ではデータのメモリアクセスが 1 回 3〜4 サイクルかかるので重かった(docs/検証記録.md)。
;
; 呼出規約は math.s の冒頭を参照。

	.module	geom
	.include "r800.inc"

; RM_VERT uc, us, neg, v, bit — リングの頂点 1 つ(ring_make の中で使う)
;   x = gx ± (uc)、z = gz ∓ (us)(neg = 1 なら左側: x は引き、z は足す)、y = gy + v を IX へ書き、
;   z >= NEAR_Z なら投影して rm_o へ書き、rm_fm に bit を立てる。IX と rm_o を次の頂点へ進める
	.macro	RM_VERT puc, pus, pneg, pv, pbit, ?skip
	ld	hl, (gx)
	ld	de, (puc)
	.if	pneg
	or	a, a
	sbc	hl, de
	.else
	add	hl, de
	.endif
	ld	0 (ix), l
	ld	1 (ix), h
	ld	hl, (gy)
	ld	de, #pv
	add	hl, de
	ld	2 (ix), l
	ld	3 (ix), h
	ld	hl, (gz)
	ld	de, (pus)
	.if	pneg
	add	hl, de
	.else
	or	a, a
	sbc	hl, de
	.endif
	ld	4 (ix), l
	ld	5 (ix), h
	ld	de, #-NEAR_Z
	add	hl, de
	bit	7, h
	jr	nz, skip
	push	ix
	pop	hl
	ld	de, (rm_o)
	call	_project2
	ld	a, (rm_fm)
	or	a, #pbit
	ld	(rm_fm), a
skip:
	ld	de, #6
	add	ix, de
	ld	hl, (rm_o)
	ld	de, #4
	add	hl, de
	ld	(rm_o), hl
	.endm

	.globl	_cam_x, _cam_y, _cam_z, _cam_sin, _cam_cos, _cam_yaw
	.globl	_fmul, _project2, _sin_tab
	.globl	_ring_v, _ring_s, _ring_front, _ring_ok

NEAR_Z	= 128

	.area	_DATA
gx:	.ds	2		; 直前に変換した点(カメラ空間)
gy:	.ds	2
gz:	.ds	2
xf_dx:	.ds	2
xf_dz:	.ds	2
g_uc:	.ds	6		; 断面の横位置 |u| = 1.0 / 1.9 / 1.2m について u*cos, u*sin
g_us:	.ds	6
rm_idx:	.ds	1
rm_fm:	.ds	1
rm_v:	.ds	2
rm_o:	.ds	2

	.area	_CODE

; ---------------------------------------------------------------------------
; xf_core: HL = ワールドの点 (x, y, z) → gx, gy, gz(カメラ空間)
;   x' = dx*cos - dz*sin、z' = dx*sin + dz*cos、y' = dy
; ---------------------------------------------------------------------------
xf_core:
	ld	e, (hl)
	inc	hl
	ld	d, (hl)			; DE = wx
	inc	hl
	ld	c, (hl)
	inc	hl
	ld	b, (hl)			; BC = wy
	inc	hl
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a			; HL = wz
	push	de
	ld	de, (_cam_z)
	or	a, a
	sbc	hl, de
	ld	(xf_dz), hl
	ld	h, b
	ld	l, c
	ld	de, (_cam_y)
	or	a, a
	sbc	hl, de
	ld	(gy), hl
	pop	hl
	ld	de, (_cam_x)
	or	a, a
	sbc	hl, de
	ld	(xf_dx), hl
	ld	de, (_cam_cos)
	call	_fmul			; DE = dx*cos
	push	de
	ld	hl, (xf_dz)
	ld	de, (_cam_sin)
	call	_fmul			; DE = dz*sin
	pop	hl
	or	a, a
	sbc	hl, de
	ld	(gx), hl
	ld	hl, (xf_dx)
	ld	de, (_cam_sin)
	call	_fmul
	push	de
	ld	hl, (xf_dz)
	ld	de, (_cam_cos)
	call	_fmul
	pop	hl
	add	hl, de
	ld	(gz), hl
	ret

; ---------------------------------------------------------------------------
; void xform3(const i16 *w, V3 *o)   HL = ワールドの点 (x, y, z)、DE = 出力
; ---------------------------------------------------------------------------
_xform3::
	push	de
	call	xf_core
	pop	de
	ld	hl, #gx
	ld	bc, #6
	ldir
	ret

; ---------------------------------------------------------------------------
; void ring_make(u8 idx, const RailPt *p)   A = リング番号、DE = レール点 (x, y, z, yaw)
;   六角形の断面の 6 頂点をカメラ空間で ring_v[idx] に、手前の頂点は投影して ring_s[idx] に置き、
;   ring_front[idx](bit k = 頂点 k が手前)と ring_ok[idx] = 1 を設定する。
;   断面の右向き = (cos Δ, -sin Δ)(Δ = レールのヨー - カメラのヨー)。左右対称なので乗算は 3 種類だけ。
; ---------------------------------------------------------------------------
_ring_make::
	ld	(rm_idx), a
	push	de
	ex	de, hl
	call	xf_core
	pop	hl
	ld	de, #7
	add	hl, de
	ld	a, (hl)			; ヨーの上位バイト
	ld	hl, #_cam_yaw
	sub	a, (hl)			; Δ
	ld	l, a
	ld	h, #0
	add	hl, hl
	ld	de, #_sin_tab
	add	hl, de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)			; DE = sin Δ
	ld	bc, #127
	add	hl, bc
	ld	c, (hl)
	inc	hl
	ld	b, (hl)			; BC = cos Δ(sin_tab[Δ + 64])
	; |u| = 1.0m: そのまま
	ld	(g_uc), bc
	ld	(g_us), de
	; |u| = 1.9m(486)と 1.2m(307)
	push	de
	push	bc
	ld	hl, #486
	ld	d, b
	ld	e, c
	call	_fmul
	ld	(g_uc + 2), de
	pop	de			; cos
	push	de
	ld	hl, #307
	call	_fmul
	ld	(g_uc + 4), de
	pop	bc
	pop	de			; sin
	push	de
	ld	hl, #486
	call	_fmul
	ld	(g_us + 2), de
	pop	de
	ld	hl, #307
	call	_fmul
	ld	(g_us + 4), de
	; 出力先: ring_v + idx*36、ring_s + idx*24
	ld	a, (rm_idx)
	ld	l, a
	ld	h, #0
	add	hl, hl
	add	hl, hl			; *4
	ld	d, h
	ld	e, l
	add	hl, hl			; *8
	add	hl, de			; *12
	push	hl
	add	hl, hl			; *24
	ld	de, #_ring_s
	add	hl, de
	ld	(rm_o), hl
	pop	hl
	ld	d, h
	ld	e, l
	add	hl, hl			; *24
	add	hl, de			; *36
	ld	de, #_ring_v
	add	hl, de
	ld	(rm_v), hl
	xor	a, a
	ld	(rm_fm), a
	; 6 頂点を展開して計算する(表引きとループをしない)。頂点 k の右向きのずれ = ±u*cos、奥行きのずれ = ∓u*sin
	;   0: 天井の左(-1.0m, +1.40)  1: 天井の右(+1.0m, +1.40)  2: 右の壁(+1.9m, +0.20)
	;   3: 床の右(+1.2m, -1.30)    4: 床の左(-1.2m, -1.30)    5: 左の壁(-1.9m, +0.20)
	push	ix
	ld	ix, (rm_v)
	RM_VERT	g_uc, g_us, 1, 358, 0x01
	RM_VERT	g_uc, g_us, 0, 358, 0x02
	RM_VERT	g_uc+2, g_us+2, 0, 51, 0x04
	RM_VERT	g_uc+4, g_us+4, 0, -333, 0x08
	RM_VERT	g_uc+4, g_us+4, 1, -333, 0x10
	RM_VERT	g_uc+2, g_us+2, 1, 51, 0x20
	pop	ix
	; ring_front[idx] = fm、ring_ok[idx] = 1
	ld	a, (rm_idx)
	ld	e, a
	ld	d, #0
	ld	hl, #_ring_front
	add	hl, de
	ld	a, (rm_fm)
	ld	(hl), a
	ld	hl, #_ring_ok
	add	hl, de
	ld	(hl), #1
	ret


; ---------------------------------------------------------------------------
; void draw_model(const V3 *c)   HL = モデルの足元(カメラ空間、z >= NEAR_Z + 32)
;   敵モデル = カメラ正対の矩形の集まり(仕様書 7.4)。g_dm_rects の g_dm_n 個の矩形
;   (x0, y0, x1, y1: 1/16m、色、部位)を、足元を投影した位置から z に反比例した大きさで塗る。
;   g_dm_flash が 0 以外ならその色で塗る(被弾・予備動作の点滅)。
;   g_dm_hit が 0 以外なら、塗った矩形を当たり判定の表 hits(game.c)に g_dm_id として足す。
;   1/16m あたりの画面量 k = 2^21 / z は、投影で求めた p_q(= 2^23 / z)を 4 で割るだけ(割り算なし)。
; ---------------------------------------------------------------------------
	.globl	_project2, _rect_fill, _p_q
	.globl	_g_color, _g_rx, _g_ry, _g_rw, _g_rh
	.globl	_hits, _n_hits

MAX_HIT	= 48

	.area	_DATA
_g_dm_rects::	.ds	2
_g_dm_n::	.ds	1
_g_dm_flash::	.ds	1
_g_dm_id::	.ds	1
_g_dm_hit::	.ds	1
dm_c:		.ds	2
dm_sx:		.ds	2
dm_sy:		.ds	2
dm_k:		.ds	2
dm_x0:		.ds	1
dm_x1:		.ds	1
dm_y0:		.ds	1
dm_y1:		.ds	1

	.area	_CODE

; HL = (A(符号付き) * dm_k) >> 8
dm_scale:
	ld	hl, (dm_k)
	or	a, a
	jp	p, 1$
	neg
	call	2$
	xor	a, a
	sub	a, l
	ld	l, a
	sbc	a, a
	sub	a, h
	ld	h, a
	ret
1$:
2$:
	ld	c, a
	ld	b, #0
	MULUW_HL_BC			; DE:HL
	ld	l, h
	ld	h, e
	ret

; A = clamp((HL + 7) >> 4, 0, C)(HL は画面座標 12.4)
dm_blk:
	ld	de, #7
	add	hl, de
	bit	7, h
	jr	nz, 1$
	ld	a, h
	cp	a, #4
	jr	nc, 2$			; 1024 以上 = 64 ブロック以上
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
	cp	a, c
	ret	c
2$:
	ld	a, c
	ret
1$:
	xor	a, a
	ret

_draw_model::
	push	ix
	ld	(dm_c), hl
	ld	de, #dm_sx		; dm_sx, dm_sy は連続
	call	_project2
	ld	hl, (_p_q)
	srl	h
	rr	l
	srl	h
	rr	l
	ld	(dm_k), hl
	ld	ix, (_g_dm_rects)
	ld	a, (_g_dm_n)
	ld	b, a
1$:
	push	bc
	; 左右 = sx + scale(x0 / x1)
	ld	a, 0 (ix)
	call	dm_scale
	ld	de, (dm_sx)
	add	hl, de
	ld	c, #64
	call	dm_blk
	ld	(dm_x0), a
	ld	a, 2 (ix)
	call	dm_scale
	ld	de, (dm_sx)
	add	hl, de
	ld	c, #64
	call	dm_blk
	ld	(dm_x1), a
	; 上下 = sy - scale(y1 / y0)
	ld	a, 3 (ix)
	call	dm_scale
	ex	de, hl
	ld	hl, (dm_sy)
	or	a, a
	sbc	hl, de
	ld	c, #46
	call	dm_blk
	ld	(dm_y0), a
	ld	a, 1 (ix)
	call	dm_scale
	ex	de, hl
	ld	hl, (dm_sy)
	or	a, a
	sbc	hl, de
	ld	c, #46
	call	dm_blk
	ld	(dm_y1), a
	; 空なら次へ
	ld	a, (dm_x0)
	ld	c, a
	ld	a, (dm_x1)
	sub	a, c
	jr	z, 8$
	jr	c, 8$
	ld	(_g_rw), a
	ld	a, c
	ld	(_g_rx), a
	ld	a, (dm_y0)
	ld	c, a
	ld	a, (dm_y1)
	sub	a, c
	jr	z, 8$
	jr	c, 8$
	ld	(_g_rh), a
	ld	a, c
	ld	(_g_ry), a
	ld	a, (_g_dm_flash)
	or	a, a
	jr	nz, 2$
	ld	a, 4 (ix)
2$:
	ld	(_g_color), a
	call	_rect_fill
	; 当たり判定の表へ(x0, y0, x1, y1, z, id)
	ld	a, (_g_dm_hit)
	or	a, a
	jr	z, 8$
	ld	a, (_n_hits)
	cp	a, #MAX_HIT
	jr	nc, 8$
	ld	c, a
	inc	a
	ld	(_n_hits), a
	ld	l, c
	ld	h, #0
	ld	d, h
	ld	e, l
	add	hl, hl
	add	hl, hl
	add	hl, hl
	or	a, a
	sbc	hl, de			; * 7
	ld	de, #_hits
	add	hl, de
	ld	a, (dm_x0)
	ld	(hl), a
	inc	hl
	ld	a, (dm_y0)
	ld	(hl), a
	inc	hl
	ld	a, (dm_x1)
	ld	(hl), a
	inc	hl
	ld	a, (dm_y1)
	ld	(hl), a
	inc	hl
	push	hl
	ld	hl, (dm_c)
	ld	de, #4
	add	hl, de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)			; z
	pop	hl
	ld	(hl), e
	inc	hl
	ld	(hl), d
	inc	hl
	ld	a, (_g_dm_id)
	ld	(hl), a
8$:
	ld	de, #6
	add	ix, de
	pop	bc
	dec	b
	jp	nz, 1$
	pop	ix
	ret

; ---------------------------------------------------------------------------
; void frame_quads(u8 idx)   A = リング番号。g_fq_cx, g_fq_cy = リング中心の投影(12.4)
;   木の枠: 床を除く 5 面に沿った帯。内側の縁 = 頂点を中心へ 1/8 寄せた点(画面上で d - d/8)。
; ---------------------------------------------------------------------------
	.globl	_raster, _scr, _ring_s

	.area	_DATA
_g_fq_cx::	.ds	2
_g_fq_cy::	.ds	2
fq_in:		.ds	24	; 内側の 6 点 (x, y)
fq_o:		.ds	2
fq_k:		.ds	1

	.area	_CODE

; HL = c + d - d/8(d = HL - c、DE = c)
fq_pull:
	or	a, a
	sbc	hl, de			; d
	ld	b, h
	ld	c, l
	sra	b
	rr	c
	sra	b
	rr	c
	sra	b
	rr	c			; d / 8
	or	a, a
	sbc	hl, bc
	add	hl, de
	ret

_frame_quads::
	; 外側 = ring_s + idx * 24
	ld	l, a
	ld	h, #0
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	d, h
	ld	e, l
	add	hl, hl
	add	hl, de
	ld	de, #_ring_s
	add	hl, de
	ld	(fq_o), hl
	; 内側の 6 点
	push	ix
	push	iy
	push	hl
	pop	ix
	ld	iy, #fq_in
	ld	b, #6
1$:
	push	bc
	ld	l, 0 (ix)
	ld	h, 1 (ix)
	ld	de, (_g_fq_cx)
	call	fq_pull
	ld	0 (iy), l
	ld	1 (iy), h
	ld	l, 2 (ix)
	ld	h, 3 (ix)
	ld	de, (_g_fq_cy)
	call	fq_pull
	ld	2 (iy), l
	ld	3 (iy), h
	ld	de, #4
	add	ix, de
	add	iy, de
	pop	bc
	djnz	1$
	pop	iy
	pop	ix
	; 5 面(k = 3 の床は付けない)
	xor	a, a
	ld	(fq_k), a
2$:
	ld	a, (fq_k)
	cp	a, #3
	jr	z, 4$
	; scr = o[k], o[k1], in[k1], in[k]
	add	a, a
	add	a, a
	ld	c, a
	ld	b, #0			; BC = 4k
	ld	a, (fq_k)
	inc	a
	cp	a, #6
	jr	nz, 3$
	xor	a, a
3$:
	add	a, a
	add	a, a
	ld	e, a
	ld	d, #0			; DE = 4 k1
	push	de
	ld	hl, (fq_o)
	add	hl, bc
	ld	de, #_scr
	push	bc
	ld	bc, #4
	ldir				; o[k]
	pop	bc
	pop	hl			; 4 k1
	push	hl
	push	bc
	ld	bc, (fq_o)
	add	hl, bc
	ld	bc, #4
	ldir				; o[k1]
	pop	bc
	pop	hl
	push	bc
	ld	bc, #fq_in
	add	hl, bc
	ld	bc, #4
	ldir				; in[k1]
	pop	hl
	ld	bc, #fq_in
	add	hl, bc
	ld	bc, #4
	ldir				; in[k]
	ld	a, #4
	ld	l, #9			; 木の色
	call	_raster
4$:
	ld	a, (fq_k)
	inc	a
	ld	(fq_k), a
	cp	a, #6
	jr	nz, 2$
	ret
