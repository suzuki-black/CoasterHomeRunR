; skyspr.s — 空のスプライト(太陽・月・雲・鳥)の置き方(地形モジュール。空を出す面の env.c の envm_sky から呼ぶ)
;
; void sky_place(void): 空の物 _sky_cnt 個の属性を決める。
;   物 i の向き _sp_az[i](256 分割。カメラのヨーとの差で画面の x)、地平線からの高さ _sp_alt[i](ドット)、
;   パターン _sp_pat[i] は envm_sky が決める。スプライトの番号・組・色・重なりを調べる範囲(ox, oy, w, h)は
;   _sky_defp が指す表(1 物 7 バイト)。画面の外、スコアと警告の行、車両の縁にかかるなら隠す。
;   空の色以外(壁・骨組み・敵)に重なるなら隠す(スプライトは常に手前に出るので)。重なりを調べるのは、
;   組の番号と順番(_sky_turn)が _sky_mask の桁で一致する物だけで、結果は次の順番まで使う(w = 0 の物は同じ組の直前の物の結果に従う)。
;   _sky_mask = 3 なら 4 組に 1 組ずつ(夕暮れの遊園地)、1 なら 2 組に 1 組ずつ(骨組みが前を横切る海の鉄橋。隠すまでの遅れを 1 フレームに)。
;   C で書くと 6 個で 1 フレーム約 0.6ms だった

	.module	skyspr
	.r800

	.globl	_g_buf_hi, _g_spr_attr, _cam_yaw

C_SKY	= 15			; 空の色(env.c)

	.area	_DATA
_sp_az::	.ds	6
_sp_pat::	.ds	6
_sp_alt::	.ds	12
_sky_yh::	.ds	2	; 地平線の高さ(ドット。env.c の sky_draw)
_sky_defp::	.ds	2	; 物の表
_sky_cnt::	.ds	1	; 物の数(6 まで)
_sky_mask::	.ds	1	; 組 ^ 順番 をこれで切って 0 の物を調べる(3 = 4 組に 1 組、1 = 2 組に 1 組)
_sky_turn::	.ds	1
sky_vis:	.ds	6
sp_i:		.ds	1
sp_x:		.ds	2
sp_y:		.ds	2
cl_x:		.ds	2
cl_y:		.ds	2
cl_w:		.ds	1
cl_h:		.ds	1
cl_r:		.ds	1
cl_r1:		.ds	1
cl_c0:		.ds	1
cl_c1:		.ds	1

	.area	_CODE

; ---------------------------------------------------------------------------
_sky_place::
	push	ix
	push	iy
	xor	a, a
1$:
	ld	(sp_i), a
	call	sky_put
	ld	a, (_sky_cnt)
	ld	b, a
	ld	a, (sp_i)
	inc	a
	cp	a, b
	jr	c, 1$
	ld	a, (_sky_turn)
	inc	a
	and	a, #3
	ld	(_sky_turn), a
	pop	iy
	pop	ix
	ret

; 物 (sp_i) を置く。IX = 物の表[i](番号、組、色、ox、oy、w、h)、IY = 属性の 4 バイト
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
	ld	de, (_sky_defp)
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
	xor	a, e
	ld	c, a
	ld	a, (_sky_mask)
	and	a, c
	jr	nz, 4$			; この組の順番でない
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

; 範囲 (cl_x, cl_y)-(+cl_w, +cl_h)(ドット)にかかるブロックが、どれも空の色か(A = 1)。画面の左右の外のブロックは見ない。
; バイト(横 2 ブロック)ごとに 0xFF(空の色 15 が 2 つ)と比べる。
; 上下がスコアと警告の行・車両の縁(行 6 未満・37 以上)にかかれば 0。
; (前は 3×3 の点だけを見ていて、骨組みの細い部材が点の間をすり抜け、鳥が骨組みの手前に出た)
sky_clear:
	; 行 r0 = y >> 2、r1 = (y + h) >> 2(6〜36 のときだけ)
	ld	hl, (cl_y)
	sra	h
	rr	l
	sra	h
	rr	l
	ld	a, h
	or	a, a
	jp	nz, 9$
	ld	a, l
	cp	a, #6
	jp	c, 9$
	ld	(cl_r), a
	ld	hl, (cl_y)
	ld	a, (cl_h)
	ld	e, a
	ld	d, #0
	add	hl, de
	sra	h
	rr	l
	sra	h
	rr	l
	ld	a, h
	or	a, a
	jp	nz, 9$
	ld	a, l
	cp	a, #37
	jp	nc, 9$
	ld	(cl_r1), a
	; 列 c0 = max(x >> 2, 0)、c1 = min((x + w) >> 2, 63)
	ld	hl, (cl_x)
	sra	h
	rr	l
	sra	h
	rr	l
	ld	a, h
	or	a, a
	jr	z, 1$
	jp	p, 8$			; 右の外
	xor	a, a
	jr	2$
1$:
	ld	a, l
	cp	a, #64
	jp	nc, 8$
2$:
	ld	(cl_c0), a
	ld	hl, (cl_x)
	ld	a, (cl_w)
	ld	e, a
	ld	d, #0
	add	hl, de
	sra	h
	rr	l
	sra	h
	rr	l
	ld	a, h
	or	a, a
	jp	m, 8$			; 左の外
	ld	a, #63
	jr	nz, 3$
	ld	a, l
	cp	a, #64
	jr	c, 3$
	ld	a, #63
3$:
	ld	(cl_c1), a
	; 見るバイト: 列 c0..c1 を含むバイト(1 バイト = 横 2 ブロック。端の 1 ブロックを余分に見ることがある)。
	; C = バイトの数、E = 最初のバイトの下位(列の位置 << 2)
	ld	a, (cl_c0)
	and	a, #0x3E
	ld	e, a
	ld	a, (cl_c1)
	and	a, #0x3E
	sub	a, e
	rrca
	inc	a
	ld	c, a
	ld	a, e
	add	a, a
	add	a, a
	ld	e, a
4$:
	; この行: H = 上位、L = 最初のバイト
	ld	a, (cl_r)
	ld	b, a
	rrca
	rrca
	rrca
	and	a, #0x1F
	ld	hl, #_g_buf_hi
	add	a, (hl)
	ld	h, a
	ld	a, b
	and	a, #7
	or	a, e
	ld	l, a
	ld	b, c
5$:
	ld	a, (hl)
	inc	a			; 2 ブロックとも空の色(0xFF)なら 0
	jp	nz, 9$
	ld	a, l
	add	a, #8
	ld	l, a
	djnz	5$
	ld	hl, #cl_r
	inc	(hl)
	ld	a, (cl_r1)
	cp	a, (hl)
	jr	nc, 4$
8$:
	ld	a, #1
	ret
9$:
	xor	a, a
	ret
