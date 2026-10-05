; geom.s — 座標変換とトンネルのリング(ホットパス)
;
; C(SDCC)で書くと IX 相対のフレーム変数を 1 命令ごとに読み書きするコードになり、
; R800 ではデータのメモリアクセスが 1 回 3〜4 サイクルかかるので重かった(docs/検証記録.md)。
;
; 呼出規約は math.s の冒頭を参照。

	.module	geom
	.include "r800.inc"


	.globl	_cam_x, _cam_y, _cam_z, _cam_sin, _cam_cos, _cam_yaw
	.globl	_cam_pitch, _cam_psin, _cam_pcos
	.globl	_fmul, _project2, _sin_tab
	.globl	_ring_v, _ring_s, _ring_front, _ring_ok, _g_rp_last

NEAR_Z	= 128

	.area	_DATA
gx:	.ds	2		; 直前に変換した点(カメラ空間)
gy:	.ds	2
gz:	.ds	2
xf_dx:	.ds	2
xf_dz:	.ds	2
rm_idx:	.ds	1
rm_k:	.ds	1
rm_dy:	.ds	2
rm_dz:	.ds	2
rm_fm:	.ds	1
rm_v:	.ds	2
rm_o:	.ds	2

	.area	_CODE

; ---------------------------------------------------------------------------
; 定数との掛け算(fmul と同じ結果: (a * c) >> 8、符号は a と c の最上位ビットの排他的論理和、大きさは |a|*|c| の 8〜23 ビット)
;   CPREP: HL = 定数 c → (名前_a) = |c|、(名前_s) = c の最上位ビット(bit7)。リングごと・フレームごとに 1 回
;   FMC:   HL = a(壊す)→ DE = fmul(a, c)。A・BC・HL・F と裏の AF を壊す(割り込み処理は裏のレジスタを使わない)
;   fmul を呼ぶより、呼び出しと定数の符号外しの分だけ軽い(R800 のメモリアクセスが少ない)
; ---------------------------------------------------------------------------
	.macro	CPREP	nm
	ld	a, h
	and	a, #0x80
	ld	(nm'_s), a
	bit	7, h
	jr	z, .+8
	xor	a, a
	sub	a, l
	ld	l, a
	sbc	a, a
	sub	a, h
	ld	h, a
	ld	(nm'_a), hl
	.endm

	.macro	FMC	nm, ?l1, ?l2
	ld	a, (nm'_s)
	xor	a, h
	ex	af, af'			; 積の符号(bit7)を裏の A に
	bit	7, h
	jr	z, l1
	xor	a, a
	sub	a, l
	ld	l, a
	sbc	a, a
	sub	a, h
	ld	h, a
l1:
	ld	bc, (nm'_a)
	MULUW_HL_BC			; DE:HL = |a| * |c|
	ld	d, e
	ld	e, h			; DE = 8〜23 ビット
	ex	af, af'
	rla
	jr	nc, l2
	xor	a, a
	sub	a, e
	ld	e, a
	sbc	a, a
	sub	a, d
	ld	d, a
l2:
	.endm

	.area	_DATA
c_cos_a:	.ds	2	; カメラのヨーの cos・sin、ピッチの cos・sin、断面の向きの cos・sin の絶対値と符号
c_cos_s:	.ds	1
c_sin_a:	.ds	2
c_sin_s:	.ds	1
c_pc_a:		.ds	2
c_pc_s:		.ds	1
c_ps_a:		.ds	2
c_ps_s:		.ds	1
c_cs_a:		.ds	2
c_cs_s:		.ds	1
c_sn_a:		.ds	2
c_sn_s:		.ds	1
	.area	_CODE

; ---------------------------------------------------------------------------
; xf_core: HL = ワールドの点 (x, y, z) → gx, gy, gz(カメラ空間)
;   x' = dx*cos - dz*sin、z' = dx*sin + dz*cos、y' = dy(ピッチがあれば y と z を回す)
; ---------------------------------------------------------------------------
xf_core:
	push	hl
	ld	hl, (_cam_cos)
	CPREP	c_cos
	ld	hl, (_cam_sin)
	CPREP	c_sin
	ld	a, (_cam_pitch)
	or	a, a
	jr	z, 1$
	ld	hl, (_cam_pcos)
	CPREP	c_pc
	ld	hl, (_cam_psin)
	CPREP	c_ps
1$:
	pop	hl
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
	FMC	c_cos			; DE = dx*cos
	push	de
	ld	hl, (xf_dz)
	FMC	c_sin			; DE = dz*sin
	pop	hl
	or	a, a
	sbc	hl, de
	ld	(gx), hl
	ld	hl, (xf_dx)
	FMC	c_sin
	push	de
	ld	hl, (xf_dz)
	FMC	c_cos
	pop	hl
	add	hl, de
	ld	(gz), hl
	; ピッチ(上を向く = 世界を下へ回す): y' = y*cos - z*sin、z' = y*sin + z*cos
	ld	a, (_cam_pitch)
	or	a, a
	ret	z
	ld	hl, (gy)
	FMC	c_pc
	push	de
	ld	hl, (gz)
	FMC	c_ps
	pop	hl
	or	a, a
	sbc	hl, de
	push	hl			; y'
	ld	hl, (gy)
	FMC	c_ps
	push	de
	ld	hl, (gz)
	FMC	c_pc
	pop	hl
	add	hl, de
	ld	(gz), hl
	pop	hl
	ld	(gy), hl
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
; void rail_at(u16 s, V3 *p, u16 *yaw)   HL = s、DE = p、スタック = yaw(void なので呼ばれた側が片付ける)
;   位置 s(1/64m)の点と向きを、前後の点(2m ごと、0xE100 から 8 バイトずつ: x, y, z, yaw)から補間する。
;   s は末尾((rail_len - 2) << 6) - 1 で止める。割合 f = (s & 127) * 2(8.8)。rail.c の C と同じ結果
; ---------------------------------------------------------------------------
	.globl	_rail_len
RAIL_ADDR = 0xE100
	.area	_DATA
c_f_a:		.ds	2
c_f_s:		.ds	1
ra_s:		.ds	2
ra_yaw:		.ds	2
	.area	_CODE

_rail_at::
	ld	(ra_s), hl
	pop	bc			; 戻り番地
	pop	hl
	ld	(ra_yaw), hl
	push	bc
	push	ix
	push	iy
	push	de
	pop	iy			; IY = p
	; s を末尾で止める
	ld	hl, (_rail_len)
	dec	hl
	dec	hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	dec	hl			; HL = 末尾の s
	ex	de, hl
	ld	hl, (ra_s)
	or	a, a
	sbc	hl, de
	jr	c, 1$
	jr	z, 1$
	ld	(ra_s), de
1$:
	; f = (s & 127) * 2(正なので符号 0)
	ld	hl, (ra_s)
	ld	a, l
	and	a, #0x7F
	add	a, a
	ld	(c_f_a), a
	xor	a, a
	ld	(c_f_a + 1), a
	ld	(c_f_s), a
	; IX = rail + (s >> 7) * 8 = rail + ((s >> 4) & ~7)
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	ld	a, l
	and	a, #0xF8
	ld	l, a
	ld	de, #RAIL_ADDR
	add	hl, de
	push	hl
	pop	ix
	; x, y, z: a + fmul(b - a, f)
	ld	b, #3
2$:
	push	bc
	ld	l, 8 (ix)
	ld	h, 9 (ix)
	ld	e, 0 (ix)
	ld	d, 1 (ix)
	or	a, a
	sbc	hl, de			; b - a
	FMC	c_f
	ld	l, 0 (ix)
	ld	h, 1 (ix)
	add	hl, de
	ld	0 (iy), l
	ld	1 (iy), h
	inc	ix
	inc	ix
	inc	iy
	inc	iy
	pop	bc
	djnz	2$
	; yaw: a + fmul((i16)(b - a), f)
	ld	l, 8 (ix)
	ld	h, 9 (ix)
	ld	e, 0 (ix)
	ld	d, 1 (ix)
	or	a, a
	sbc	hl, de
	FMC	c_f
	ld	l, 0 (ix)
	ld	h, 1 (ix)
	add	hl, de
	ex	de, hl
	ld	hl, (ra_yaw)
	ld	(hl), e
	inc	hl
	ld	(hl), d
	pop	iy
	pop	ix
	ret

; ---------------------------------------------------------------------------
; void ring_make(u8 idx, const RailPt *p)   A = リング番号、DE = レール点 (x, y, z, yaw)
;   洞窟の断面(g_rm_prof: 6 頂点の (u, v)。u = 右、v = 上、8.8 m、レール=目の位置からの相対)を
;   カメラ空間で ring_v[idx] に、手前の頂点は投影して ring_s[idx] に置き、
;   ring_front[idx](bit k = 頂点 k が手前)と ring_ok[idx] = 1 を設定する。
;   断面の中心(レール点)も投影して ring_c[idx] = (x, y, 2^23/z) に置く(z が手前でなければ q = 0)。
;   リング面の飾り(レール・枕木・坑道枠・鍾乳石)は、この中心と q から 2D で求める(env.c)。
;   断面の右向き = (cos Δ, -sin Δ)(Δ = レールのヨー - カメラのヨー)。
;   頂点のループは、断面の表を IX、出力(ring_v)を IY で指して回す(変数をメモリに出し入れしない)
; ---------------------------------------------------------------------------
	.globl	_ring_c
	.area	_DATA
_g_rm_prof::	.ds	2
	.area	_CODE

_ring_make::
	push	ix
	push	iy
	ld	(rm_idx), a
	ld	a, #0xFF
	ld	(_g_rp_last), a		; 飾りの点の計算(raster.s の rp_ring)が覚えているリングを捨てる
	ld	a, (rm_idx)
	push	de
	ex	de, hl
	call	xf_core			; 中心(ピッチの定数もここで用意される)
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
	ld	bc, #127
	add	hl, bc
	ld	c, (hl)
	inc	hl
	ld	b, (hl)			; BC = cos Δ、DE = sin Δ
	ex	de, hl
	push	bc
	CPREP	c_sn
	pop	hl
	CPREP	c_cs
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
	push	hl
	pop	iy			; IY = ring_v[idx]
	ld	ix, (_g_rm_prof)	; IX = 断面 (u, v) × 6
	xor	a, a
	ld	(rm_fm), a
	ld	a, #1
	ld	(rm_k), a		; いまの頂点のビット
3$:
	; x = gx + u*cos
	ld	l, 0 (ix)
	ld	h, 1 (ix)
	FMC	c_cs
	ld	hl, (gx)
	add	hl, de
	ld	0 (iy), l
	ld	1 (iy), h
	; 奥行きのずれ dz = -u*sin、上下のずれ dy = v
	ld	l, 0 (ix)
	ld	h, 1 (ix)
	FMC	c_sn
	xor	a, a
	sub	a, e
	ld	e, a
	sbc	a, a
	sub	a, d
	ld	d, a			; DE = dz
	ld	c, 2 (ix)
	ld	b, 3 (ix)		; BC = dy
	ld	a, (_cam_pitch)
	or	a, a
	jp	z, 6$
	; ピッチがあれば回す: dy' = dy*cos - dz*sin、dz' = dy*sin + dz*cos
	ld	(rm_dz), de
	ld	(rm_dy), bc
	ld	h, b
	ld	l, c
	FMC	c_pc
	push	de
	ld	hl, (rm_dz)
	FMC	c_ps
	pop	hl
	or	a, a
	sbc	hl, de
	push	hl			; dy'
	ld	hl, (rm_dy)
	FMC	c_ps
	push	de
	ld	hl, (rm_dz)
	FMC	c_pc
	pop	hl
	add	hl, de
	ex	de, hl			; DE = dz'
	pop	bc			; BC = dy'
6$:
	; y = gy + dy、z = gz + dz
	ld	hl, (gy)
	add	hl, bc
	ld	2 (iy), l
	ld	3 (iy), h
	ld	hl, (gz)
	add	hl, de
	ld	4 (iy), l
	ld	5 (iy), h
	; z >= NEAR_Z なら投影
	ld	de, #-NEAR_Z
	add	hl, de
	bit	7, h
	jr	nz, 4$
	push	iy
	pop	hl
	ld	de, (rm_o)
	call	_project2		; IX・IY は保たれる
	ld	a, (rm_k)
	ld	hl, #rm_fm
	or	a, (hl)
	ld	(hl), a
4$:
	ld	de, #4
	add	ix, de
	ld	hl, (rm_o)
	add	hl, de
	ld	(rm_o), hl
	ld	de, #6
	add	iy, de
	ld	a, (rm_k)
	add	a, a
	ld	(rm_k), a
	cp	a, #0x40
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
	pop	iy
	pop	ix
	ret

; ---------------------------------------------------------------------------
; void draw_model(const V3 *c)   HL = モデルの足元(カメラ空間、z >= NEAR_Z + 32)
;   敵モデル = カメラ正対の矩形の集まり(仕様書 7.4)。g_dm_rects の g_dm_n 個の矩形
;   (x0, y0, x1, y1: 1/16m、色、部位)を、足元を投影した位置から z に反比例した大きさで塗る。
;   g_dm_flash が 0 以外ならその色で塗る(被弾・予備動作の点滅)。
;   g_dm_hit が 0 以外なら、塗った矩形を当たり判定の表 hits(game.c)に g_dm_id として足す。
;   g_dm_grow が 0 以外なら、各矩形を上下左右に 1 ブロック広げて塗る(縁取り: 先に g_dm_flash = 縁の色で広げて塗り、
;   その上に普通に塗ると、絵の外周だけが縁の色で残る)。
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
_g_dm_grow::	.ds	1
dm_c:		.ds	2
dm_sx:		.ds	2
dm_sy:		.ds	2
dm_x0:		.ds	1
dm_x1:		.ds	1
dm_y0:		.ds	1
dm_y1:		.ds	1
DM_CMAX = 24			; 座標の表に入る矩形の数(モデルは最大 18)
dm_mode:	.ds	1	; 0 = 求める、1 = 求めて表に書く(縁取りの回)、2 = 表を使う(縁取りの直後の回)
dm_cv:		.ds	1	; 1 = 表が直前の縁取りの回のもの
dm_cc:		.ds	2	; 表を書いたときの足元の点と矩形の列
dm_cr:		.ds	2
dm_cp:		.ds	2
dm_cache:	.ds	DM_CMAX * 4	; 矩形ごとの x0, x1, y0, y1(ブロック、広げる前)

	.area	_CODE

; dm_cx: A = x(1/16m、符号付き)→ A = clamp((sx + scale(x) + 7) >> 4, 0, 64)
; dm_cy: A = y(1/16m、符号付き)→ A = clamp((sy - scale(y) + 7) >> 4, 0, 46)
;   scale(v) = (|v| * k) >> 8 に v の符号を付けたもの(k = 1/16m あたりの画面量)。k・sx・sy は draw_model が
;   命令の即値(dm_k_i・dm_sx_i・dm_sy_i)に書く(メモリの変数を読まない)。BC・DE・HL を壊す
;   (値ごとに結果を覚えて使い回す形も試したが、覚える・戻す手間のほうが重く、ステージ 3 で +0.6ms だった)
dm_cx:
	call	dm_sc
dm_sx_i:
	ld	de, #0			; sx(draw_model が書く)
	add	hl, de
	ld	c, #64
	jr	dm_blk
dm_cy:
	call	dm_sc
	ex	de, hl
dm_sy_i:
	ld	hl, #0			; sy(draw_model が書く)
	or	a, a
	sbc	hl, de
	ld	c, #46
; HL(画面座標 12.4)→ A = clamp((HL + 7) >> 4, 0, C)。0 <= HL + 7 < 1024 なら (HL + 7) << 4 の上位バイトが答え
dm_blk:
	ld	de, #7
	add	hl, de
	bit	7, h
	jr	nz, 1$
	ld	a, h
	cp	a, #4
	jr	nc, 2$			; 1024 以上 = 64 ブロック以上
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	a, h
	cp	a, c
	ret	c
2$:
	ld	a, c
	ret
1$:
	xor	a, a
	ret
; dm_sc: A = v → HL = scale(v)
dm_sc:
	ld	d, a			; D = v(符号)
	bit	7, a
	jr	z, 1$
	neg
1$:
dm_k_i:
	ld	hl, #0			; k(draw_model が書く)
	inc	h
	dec	h
	jr	nz, 2$
	; 拡大率が 256 未満なら 8×8 の MULUB(14 サイクル。MULUW は 36)。積は 16bit に収まり、結果は同じ
	ld	e, l
	MULUB_A_E			; HL = A * E
	ld	l, h
	ld	h, #0
	jr	3$
2$:
	ld	c, a
	ld	b, #0
	ld	a, d
	ex	af, af'			; MULUW は DE を壊すので v を裏の A に(割り込み処理は裏のレジスタを使わない)
	MULUW_HL_BC			; DE:HL
	ld	l, h
	ld	h, e
	ex	af, af'
	ld	d, a
3$:
	bit	7, d
	ret	z
	xor	a, a
	sub	a, l
	ld	l, a
	sbc	a, a
	sub	a, h
	ld	h, a
	ret

_draw_model::
	push	ix
	ld	(dm_c), hl
	; 縁取りの回(広げて塗る)と直後の普通の回は、同じ敵の同じ矩形なので、座標を表で使い回す
	ld	a, (_g_dm_grow)
	or	a, a
	jr	z, 40$
	ld	a, (_g_dm_n)
	cp	a, #DM_CMAX + 1
	ld	a, #0
	jr	nc, 41$
	ld	(dm_cc), hl
	ld	hl, (_g_dm_rects)
	ld	(dm_cr), hl
	ld	a, #1
	jr	41$
40$:
	ld	a, (dm_cv)
	or	a, a
	jr	z, 41$			; A = 0
	ld	de, (dm_cc)
	or	a, a
	sbc	hl, de
	ld	a, #0
	jr	nz, 41$
	ld	hl, (_g_dm_rects)
	ld	de, (dm_cr)
	or	a, a
	sbc	hl, de
	jr	nz, 41$
	ld	a, #2
41$:
	ld	(dm_mode), a
	ld	b, a
	xor	a, a
	bit	0, b
	jr	z, 42$
	inc	a
42$:
	ld	(dm_cv), a		; 縁取りの回だけ、次の回のために表を残す
	ld	hl, #dm_cache
	ld	(dm_cp), hl
	ld	a, b
	cp	a, #2
	jr	z, 43$			; 中心の投影と拡大率は前の回のまま
	ld	hl, (dm_c)
	ld	de, #dm_sx		; dm_sx, dm_sy は連続
	call	_project2
	ld	hl, (_p_q)
	srl	h
	rr	l
	srl	h
	rr	l
	ld	(dm_k_i + 1), hl	; 座標の計算(dm_cx / dm_cy)の即値に書く
	ld	hl, (dm_sx)
	ld	(dm_sx_i + 1), hl
	ld	hl, (dm_sy)
	ld	(dm_sy_i + 1), hl
43$:
	ld	ix, (_g_dm_rects)
	ld	a, (_g_dm_n)
	ld	b, a
1$:
	push	bc
	ld	a, (dm_mode)
	cp	a, #2
	jr	nz, 44$
	; 表から
	ld	hl, (dm_cp)
	ld	a, (hl)
	ld	(dm_x0), a
	inc	hl
	ld	a, (hl)
	ld	(dm_x1), a
	inc	hl
	ld	a, (hl)
	ld	(dm_y0), a
	inc	hl
	ld	a, (hl)
	ld	(dm_y1), a
	inc	hl
	ld	(dm_cp), hl
	jp	45$
44$:
	; 左右 = sx + scale(x0 / x1)、上下 = sy - scale(y1 / y0)
	ld	a, 0 (ix)
	call	dm_cx
	ld	(dm_x0), a
	ld	a, 2 (ix)
	call	dm_cx
	ld	(dm_x1), a
	ld	a, 3 (ix)
	call	dm_cy
	ld	(dm_y0), a
	ld	a, 1 (ix)
	call	dm_cy
	ld	(dm_y1), a
	ld	a, (dm_mode)
	or	a, a
	jr	z, 45$
	; 縁取りの回: 表に書く
	ld	hl, (dm_cp)
	ld	a, (dm_x0)
	ld	(hl), a
	inc	hl
	ld	a, (dm_x1)
	ld	(hl), a
	inc	hl
	ld	a, (dm_y0)
	ld	(hl), a
	inc	hl
	ld	a, (dm_y1)
	ld	(hl), a
	inc	hl
	ld	(dm_cp), hl
45$:
	; 空なら次へ
	ld	a, (dm_x0)
	ld	c, a
	ld	a, (dm_x1)
	sub	a, c
	jp	z, 8$
	jp	c, 8$
	ld	(_g_rw), a
	ld	a, c
	ld	(_g_rx), a
	ld	a, (dm_y0)
	ld	c, a
	ld	a, (dm_y1)
	sub	a, c
	jp	z, 8$
	jp	c, 8$
	ld	(_g_rh), a
	ld	a, c
	ld	(_g_ry), a
	ld	a, (_g_dm_grow)
	or	a, a
	jr	z, 30$
	ld	hl, #_g_rx
	ld	de, #_g_rw
	ld	c, #64
	call	dm_grow1
	ld	hl, #_g_ry
	ld	de, #_g_rh
	ld	c, #46
	call	dm_grow1
30$:
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

; dm_grow1: HL = 位置、DE = 長さ、C = 画面の端 → 両側に 1 ブロックずつ広げる(画面の外へは出さない)
dm_grow1:
	ld	a, (hl)
	or	a, a
	jr	z, 1$
	dec	(hl)
	ex	de, hl
	inc	(hl)
	ex	de, hl
1$:
	ld	a, (de)
	add	a, (hl)
	cp	a, c
	ret	nc
	ex	de, hl
	inc	(hl)
	ret

