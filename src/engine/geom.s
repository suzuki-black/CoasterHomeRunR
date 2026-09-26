; geom.s — 座標変換とトンネルのリング(ホットパス)
;
; C(SDCC)で書くと IX 相対のフレーム変数を 1 命令ごとに読み書きするコードになり、
; R800 ではデータのメモリアクセスが 1 回 3〜4 サイクルかかるので重かった(docs/検証記録.md)。
;
; 呼出規約は math.s の冒頭を参照。

	.module	geom
	.include "r800.inc"


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
rm_idx:	.ds	1
rm_k:	.ds	1
rm_p:	.ds	2
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
;   洞窟の断面(g_rm_prof: 6 頂点の (u, v)。u = 右、v = 上、8.8 m、レール=目の位置からの相対)を
;   カメラ空間で ring_v[idx] に、手前の頂点は投影して ring_s[idx] に置き、
;   ring_front[idx](bit k = 頂点 k が手前)と ring_ok[idx] = 1 を設定する。
;   断面の中心(レール点)も投影して ring_c[idx] = (x, y, 2^23/z) に置く(z が手前でなければ q = 0)。
;   リング面の飾り(レール・枕木・坑道枠・鍾乳石)は、この中心と q から 2D で求める(env.c)。
;   断面の右向き = (cos Δ, -sin Δ)(Δ = レールのヨー - カメラのヨー)。
; ---------------------------------------------------------------------------
	.globl	_ring_c
	.area	_DATA
_g_rm_prof::	.ds	2
g_cs:		.ds	2
g_sn:		.ds	2
	.area	_CODE

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
	ld	d, (hl)
	ld	(g_sn), de
	ld	bc, #127
	add	hl, bc
	ld	c, (hl)
	inc	hl
	ld	b, (hl)
	ld	(g_cs), bc
	; ring_c + idx*6
	ld	a, (rm_idx)
	ld	l, a
	ld	h, #0
	add	hl, hl
	ld	d, h
	ld	e, l
	add	hl, hl
	add	hl, de			; *6
	ld	de, #_ring_c
	add	hl, de
	; 中心の投影(z が手前なら)
	push	hl
	ld	hl, (gz)
	ld	de, #-NEAR_Z
	add	hl, de
	pop	de			; DE = ring_c[idx]
	bit	7, h
	jr	z, 1$
	ld	hl, #4			; 手前でない: q = 0
	add	hl, de
	xor	a, a
	ld	(hl), a
	inc	hl
	ld	(hl), a
	jr	2$
1$:
	push	de
	ld	hl, #gx			; gx, gy, gz は連続
	call	_project2
	pop	hl
	ld	de, #4
	add	hl, de
	ld	de, (_p_q)
	ld	(hl), e
	inc	hl
	ld	(hl), d
2$:
	; ring_s + idx*24、ring_v + idx*36
	ld	a, (rm_idx)
	ld	l, a
	ld	h, #0
	add	hl, hl
	add	hl, hl
	ld	d, h
	ld	e, l
	add	hl, hl
	add	hl, de			; *12
	ld	d, h
	ld	e, l
	add	hl, hl			; *24
	push	hl
	ld	bc, #_ring_s
	add	hl, bc
	ld	(rm_o), hl
	pop	hl
	add	hl, de			; *36
	ld	de, #_ring_v
	add	hl, de
	ld	(rm_v), hl
	xor	a, a
	ld	(rm_fm), a
	ld	(rm_k), a
	ld	hl, (_g_rm_prof)
	ld	(rm_p), hl
3$:
	; u, v を読む
	ld	hl, (rm_p)
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	inc	hl
	ld	c, (hl)
	inc	hl
	ld	b, (hl)
	inc	hl
	ld	(rm_p), hl
	push	bc			; v
	push	de			; u
	; x = gx + u*cos
	ex	de, hl
	ld	de, (g_cs)
	call	_fmul
	ld	hl, (gx)
	add	hl, de
	ld	b, h
	ld	c, l			; BC = x
	; z = gz - u*sin
	pop	hl			; u
	push	bc
	ld	de, (g_sn)
	call	_fmul
	ld	hl, (gz)
	or	a, a
	sbc	hl, de			; HL = z
	pop	bc			; x
	pop	de			; v
	push	hl			; z
	; 書く: x, y = gy + v, z
	ld	hl, (rm_v)
	ld	(hl), c
	inc	hl
	ld	(hl), b
	inc	hl
	push	hl
	ld	hl, (gy)
	add	hl, de
	ex	de, hl			; DE = y
	pop	hl
	ld	(hl), e
	inc	hl
	ld	(hl), d
	inc	hl
	pop	de			; z
	ld	(hl), e
	inc	hl
	ld	(hl), d
	; z >= NEAR_Z なら投影
	ex	de, hl
	ld	de, #-NEAR_Z
	add	hl, de
	bit	7, h
	jr	nz, 4$
	ld	hl, (rm_v)
	ld	de, (rm_o)
	call	_project2
	ld	a, (rm_k)
	ld	b, a
	inc	b
	ld	a, #0x80
5$:
	rlca
	djnz	5$			; A = 1 << k
	ld	hl, #rm_fm
	or	a, (hl)
	ld	(hl), a
4$:
	ld	hl, (rm_v)
	ld	de, #6
	add	hl, de
	ld	(rm_v), hl
	ld	hl, (rm_o)
	ld	de, #4
	add	hl, de
	ld	(rm_o), hl
	ld	a, (rm_k)
	inc	a
	ld	(rm_k), a
	cp	a, #6
	jp	nz, 3$
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

