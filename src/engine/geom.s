; geom.s — 座標変換とトンネルのリング(ホットパス)
;
; C(SDCC)で書くと IX 相対のフレーム変数を 1 命令ごとに読み書きするコードになり、
; R800 ではデータのメモリアクセスが 1 回 3〜4 サイクルかかるので重かった(docs/検証記録.md)。
;
; 呼出規約は math.s の冒頭を参照。

	.module	geom

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
rm_k:	.ds	1
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
	ld	(rm_k), a
	ld	(rm_fm), a
1$:
	; 頂点 k の表: u の種類*2、符号、v(2 バイト)
	ld	a, (rm_k)
	add	a, a
	add	a, a
	ld	e, a
	ld	d, #0
	ld	hl, #hex_tab
	add	hl, de
	ld	e, (hl)			; E = u の種類 * 2
	inc	hl
	ld	b, (hl)			; B = 符号(0 = +、1 = -)
	inc	hl
	ld	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a			; HL = v
	ld	de, (gy)
	add	hl, de
	push	hl			; y
	; u*cos と u*sin を読む
	ld	a, (rm_k)
	add	a, a
	add	a, a
	ld	e, a
	ld	d, #0
	ld	hl, #hex_tab
	add	hl, de
	ld	e, (hl)
	ld	d, #0
	ld	hl, #g_uc
	add	hl, de
	ld	c, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, c			; HL = u*cos(|u|)
	push	hl
	ld	hl, #g_us
	add	hl, de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)			; DE = u*sin(|u|)
	pop	hl
	ld	a, b
	or	a, a
	jr	z, 2$
	; 符号を反転
	xor	a, a
	sub	a, l
	ld	l, a
	sbc	a, a
	sub	a, h
	ld	h, a
	xor	a, a
	sub	a, e
	ld	e, a
	sbc	a, a
	sub	a, d
	ld	d, a
2$:
	; x = gx + u*cos、z = gz - u*sin
	ld	bc, (gx)
	add	hl, bc
	ex	de, hl			; DE = x、HL = u*sin
	ld	b, h
	ld	c, l
	ld	hl, (gz)
	or	a, a
	sbc	hl, bc			; HL = z
	ld	bc, (rm_v)
	ld	a, e
	ld	(bc), a
	inc	bc
	ld	a, d
	ld	(bc), a
	inc	bc
	pop	de			; y
	ld	a, e
	ld	(bc), a
	inc	bc
	ld	a, d
	ld	(bc), a
	inc	bc
	ld	a, l
	ld	(bc), a
	inc	bc
	ld	a, h
	ld	(bc), a
	; z >= NEAR_Z なら投影
	ld	de, #NEAR_Z
	or	a, a
	sbc	hl, de
	jp	m, 3$
	ld	hl, (rm_v)
	ld	de, (rm_o)
	call	_project2
	ld	a, (rm_k)
	ld	b, a
	ld	a, #1
	inc	b
4$:
	dec	b
	jr	z, 5$
	add	a, a
	jr	4$
5$:
	ld	hl, #rm_fm
	or	a, (hl)
	ld	(hl), a
3$:
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
	jp	nz, 1$
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

; 断面の 6 頂点: u の種類*2(0 = 1.0m、2 = 1.9m、4 = 1.2m)、符号(1 = 左)、v(8.8)
hex_tab:
	.db	0, 1
	.dw	358			; 0: 天井の左
	.db	0, 0
	.dw	358			; 1: 天井の右
	.db	2, 0
	.dw	51			; 2: 右の壁の中ほど
	.db	4, 0
	.dw	-333			; 3: 床の右
	.db	4, 1
	.dw	-333			; 4: 床の左
	.db	2, 1
	.dw	51			; 5: 左の壁の中ほど
