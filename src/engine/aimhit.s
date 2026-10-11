; aimhit.s — 照準ブロックに重なる当たり判定の矩形のうち、最も手前のものを探す(game.c の aim_target。ゲームモジュールに入れる)
;   照準の色(毎フレーム)と射撃で使う。C で書くと変数を IX から読み書きして、敵が多い場面で 1 フレーム 0.5ms かかっていた
;   hits は HitRect {x0, y0, x1, y1, z(16bit、符号つき), id} の 7 バイトずつ(game.h)
	.module	aimhit
	.include "r800.inc"
	.globl	_hits, _n_hits
	.area	_CODE

; u8 aim_scan(u8 bx, u8 by)   A = bx、L = by → A = 最も手前の矩形の hits の番号(無ければ 0xFF)
_aim_scan::
	ld	c, a			; C = bx
	ld	b, l			; B = by
	ld	a, (_n_hits)
	or	a, a
	jr	z, 9$
	ld	d, a			; D = 残り
	ld	e, #0			; E = 番号
	push	ix
	ld	ix, #_hits
	ld	hl, #0x7FFF		; HL = いちばん手前の z
	ld	a, #0xFF
	ex	af, af'			; A' = いちばん手前の番号
1$:
	ld	a, c
	cp	a, 0 (ix)
	jr	c, 3$			; bx < x0
	cp	a, 2 (ix)
	jr	nc, 3$			; bx >= x1
	ld	a, b
	cp	a, 1 (ix)
	jr	c, 3$			; by < y0
	cp	a, 3 (ix)
	jr	nc, 3$			; by >= y1
	ld	a, 4 (ix)		; z < いちばん手前(符号つき)?
	sub	a, l
	ld	a, 5 (ix)
	sbc	a, h
	jp	po, 2$
	xor	a, #0x80
2$:
	jp	p, 3$
	ld	l, 4 (ix)
	ld	h, 5 (ix)
	ld	a, e
	ex	af, af'
3$:
	push	bc
	ld	bc, #7
	add	ix, bc
	pop	bc
	inc	e
	dec	d
	jr	nz, 1$
	pop	ix
	ex	af, af'
	ret
9$:
	ld	a, #0xFF
	ret

; ---------------------------------------------------------------------------
; void ce_load(Enemy *e) / void ce_store(Enemy *e)   HL = e
;   敵の更新の写し g_ce(game.c)との間で、カメラ座標 c を除く頭の 19 バイトを写す(敵の更新は c を触らない)。
;   R800 の LDIR は 1 バイト 7 サイクルなので LDI を並べる
; ---------------------------------------------------------------------------
	.globl	_g_ce
CE_N	= 19
_ce_load::
	ld	de, #_g_ce
	.rept	CE_N
	ldi
	.endm
	ret
_ce_store::
	ex	de, hl
	ld	hl, #_g_ce
	.rept	CE_N
	ldi
	.endm
	ret

; ---------------------------------------------------------------------------
; void alt_qa(u8 color)   A = 色。分岐の線路の四角形 g_aq[0]→[1]→[2]→[3](AltP: 頂点 V3、投影 s[2]、in)を塗る(game.c の alt_draw)
;   4 点とも手前(in)なら投影済みの座標を scr に並べて raster、そうでなければ頂点のポインタのまま poly3p で切って塗る
; ---------------------------------------------------------------------------
	.globl	_scr, _raster, _poly3p
AP_S	= 6			; AltP の s
AP_IN	= 10			; AltP の in
	.area	_DATA
_g_aq::	.ds	8
aq_c:	.ds	1
	.area	_CODE
_alt_qa::
	ld	(aq_c), a
	ld	hl, #_g_aq
	ld	de, #_scr
	ld	b, #4
1$:
	ld	c, (hl)
	inc	hl
	ld	a, (hl)
	inc	hl
	push	hl
	ld	h, a
	ld	l, c			; HL = 頂点
	push	bc
	ld	bc, #AP_IN
	add	hl, bc
	ld	a, (hl)
	or	a, a
	jr	z, 8$			; 手前に無い点がある
	ld	bc, #AP_S - AP_IN
	add	hl, bc
	ldi
	ldi
	ldi
	ldi
	pop	bc
	pop	hl
	djnz	1$
	ld	a, (aq_c)
	ld	l, a
	ld	a, #4
	jp	_raster
8$:
	pop	bc
	pop	hl
	ld	a, (aq_c)
	ld	d, a
	ld	e, #4
	push	de
	ld	hl, #_g_aq
	call	_poly3p
	ret

; ---------------------------------------------------------------------------
; 分岐の選ばれていない側の線路の点(game.c の alt_draw)。もと常駐部の geom.s にあったものを、線形の求め方にしてここへ移した
;   void alt_cen(const RailPt *p, V3 *o)   HL = p、DE = o
;     レールの点の中心 (p.x, p.y + g_al_fy, p.z) をカメラ空間へ変換して o に書く(xf_core)
;   void alt_mk_a(const RailPt *p, AltP *o)   HL = p(向きのヨーだけ使う)、DE = o
;     g_al_lat の g_al_n 個の横位置 lat について、中心 + lat × 横の向き をカメラ空間で求め、z >= NEAR_Z なら投影して
;     AltP(V3 v、i16 s[2]、u8 in。11 バイト)を順に書く。中心は g_al_cp(V3)。g_al_cp2 が 0 でなければ、
;     g_al_cp から g_al_cp2 へ 1/8 進んだ点(枕木の位置)を中心にする。
;   変換は線形なので、点ごとに xf_core(掛け算 8 回)を通す代わりに、横の向き (cos h, 0, -sin h) × 64 をヨーごとに 1 回だけ
;   回し(カメラの位置に足した点を xf_core に通すと、16bit の折り返しで平行移動が打ち消される)、各点は (lat × 向き) >> 14 を
;   足す。点ごとに変換していたときと丸めの順が違うので、1〜2 単位(1/256m)違うことがある。向きは g_al_dv = 0 で作り直す
;   (alt_draw がフレームの頭で 0 にする)
; ---------------------------------------------------------------------------
	.globl	_xf_core, _g_gxyz, _project2, _sin_tab, _cam_x, _cam_y, _cam_z
AL_NEAR	= 128			; NEAR_Z(render.h)
	.area	_DATA
_g_al_lat::	.ds	2
_g_al_n::	.ds	1
_g_al_fy::	.ds	2
_g_al_cp::	.ds	2
_g_al_cp2::	.ds	2
_g_al_dv::	.ds	1
al_dh:		.ds	1	; 向きを求めたヨー
al_w:		.ds	6	; ワールドの点
al_p:		.ds	6	; 求めた点(カメラ空間)
al_cx:		.ds	2	; 中心(カメラ空間)
al_cy:		.ds	2
al_cz:		.ds	2
al_o:		.ds	2
al_k:		.ds	1
d_x_a:		.ds	2	; 横の向き(カメラ空間、× 16384)の絶対値と符号
d_x_s:		.ds	1
d_y_a:		.ds	2
d_y_s:		.ds	1
d_z_a:		.ds	2
d_z_s:		.ds	1
	.area	_CODE

; CPREP nm: HL → nm_a = |HL|、nm_s = 符号(bit7)
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

; LOFF nm: HL = lat → DE = (lat × 向き nm) >> 14(0 へ向けて切り捨て)。BC・HL・A を壊す
	.macro	LOFF	nm, ?l1, ?l2
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
	MULUW_HL_BC			; DE:HL = |lat| * |向き|
	add	hl, hl			; DE:HL >> 14 → DE
	rl	e
	rl	d
	add	hl, hl
	rl	e
	rl	d
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

_alt_cen::
	push	de
	ld	de, #al_w
	ldi
	ldi				; x
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	inc	hl
	push	hl
	ld	hl, (_g_al_fy)
	add	hl, de
	ld	(al_w + 2), hl		; y + fy
	pop	hl
	ld	de, #al_w + 4
	ldi
	ldi				; z
	ld	hl, #al_w
	call	_xf_core		; IX・IY は保たれる
	pop	de
	ld	hl, #_g_gxyz
	ldi
	ldi
	ldi
	ldi
	ldi
	ldi
	ret

_alt_mk_a::
	push	ix
	push	iy
	ld	(al_o), de
	push	hl
	pop	iy			; IY = p
	; 中心
	ld	hl, (_g_al_cp)
	ld	de, #al_cx		; al_cx, al_cy, al_cz は連続
	ldi
	ldi
	ldi
	ldi
	ldi
	ldi
	ld	hl, (_g_al_cp2)
	ld	a, h
	or	a, l
	jr	z, 6$
	; 枕木の位置: 中心 = a + ((b - a) >> 3)(符号つき)
	ld	de, #al_cx
	ld	b, #3
5$:
	push	bc
	ld	c, (hl)
	inc	hl
	ld	b, (hl)
	inc	hl			; BC = b の成分
	push	hl
	ex	de, hl			; HL = 中心の成分の番地
	push	hl
	ld	e, (hl)
	inc	hl
	ld	d, (hl)			; DE = a の成分
	ld	h, b
	ld	l, c
	or	a, a
	sbc	hl, de			; HL = b - a
	sra	h
	rr	l
	sra	h
	rr	l
	sra	h
	rr	l
	add	hl, de
	ex	de, hl			; DE = a + ((b - a) >> 3)
	pop	hl
	ld	(hl), e
	inc	hl
	ld	(hl), d
	inc	hl
	ex	de, hl			; DE = 次の中心の成分
	pop	hl			; HL = 次の b の成分
	pop	bc
	djnz	5$
6$:
	ld	ix, (_g_al_lat)
	ld	a, (_g_al_n)
	ld	(al_k), a
	; 横の向き(同じヨーなら前の値): (cam_x + cos h × 64, cam_y, cam_z - sin h × 64) を変換して回転だけを取り出す
	ld	a, (_g_al_dv)
	or	a, a
	jr	z, 7$
	ld	a, (al_dh)
	cp	a, 7 (iy)
	jp	z, 1$
7$:
	ld	a, 7 (iy)
	ld	(al_dh), a
	ld	a, #1
	ld	(_g_al_dv), a
	ld	l, 7 (iy)		; h = ヨーの上位
	ld	h, #0
	add	hl, hl
	ld	de, #_sin_tab
	add	hl, de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)			; DE = sin h
	ld	bc, #127
	add	hl, bc
	ld	c, (hl)
	inc	hl
	ld	b, (hl)			; BC = cos h
	push	de
	ld	h, b
	ld	l, c
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	de, (_cam_x)
	add	hl, de
	ld	(al_w), hl
	ld	hl, (_cam_y)
	ld	(al_w + 2), hl
	pop	hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ex	de, hl
	ld	hl, (_cam_z)
	or	a, a
	sbc	hl, de
	ld	(al_w + 4), hl
	ld	hl, #al_w
	call	_xf_core
	ld	hl, (_g_gxyz)
	CPREP	d_x
	ld	hl, (_g_gxyz + 2)
	CPREP	d_y
	ld	hl, (_g_gxyz + 4)
	CPREP	d_z
1$:
	ld	l, 0 (ix)
	ld	h, 1 (ix)
	LOFF	d_x
	ld	hl, (al_cx)
	add	hl, de
	ld	(al_p), hl
	ld	l, 0 (ix)
	ld	h, 1 (ix)
	LOFF	d_y
	ld	hl, (al_cy)
	add	hl, de
	ld	(al_p + 2), hl
	ld	l, 0 (ix)
	ld	h, 1 (ix)
	LOFF	d_z
	ld	hl, (al_cz)
	add	hl, de
	ld	(al_p + 4), hl
	ld	de, (al_o)
	ld	hl, #al_p
	ldi
	ldi
	ldi
	ldi
	ldi
	ldi				; o->v。DE = o->s
	ld	hl, (al_p + 4)
	ld	bc, #-AL_NEAR
	add	hl, bc
	bit	7, h
	jr	nz, 2$
	push	de
	ld	hl, #al_p
	call	_project2		; o->s(IX・IY は保たれる)
	pop	de
	ld	hl, #4
	add	hl, de
	ld	(hl), #1		; o->in
	jr	3$
2$:
	ld	hl, #4
	add	hl, de
	ld	(hl), #0
3$:
	inc	hl
	ld	(al_o), hl		; 次の AltP
	inc	ix
	inc	ix
	ld	a, (al_k)
	dec	a
	ld	(al_k), a
	jp	nz, 1$
	pop	iy
	pop	ix
	ret
