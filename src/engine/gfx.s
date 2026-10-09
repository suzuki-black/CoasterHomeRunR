; gfx.s — 描画バッファへの塗りつぶし・矩形・絵・文字・点
;
; 描画バッファは VRAM のパターンジェネレータと同じ並びに詰めた形(1536 バイト、0xC000-0xC5FF)。
;   番地 = バッファ + (行 >> 3) * 256 + (列 >> 1) * 8 + (行 & 7)
;   1 バイト = 横 2 ブロック。上位 4bit が左(偶数の列)、下位 4bit が右(奇数の列)
; 転送(vdp.s の vdp_xfer)は頭から送るだけでよい。横に隣り合うバイトは 8 バイト離れる(+8 で進む)。
; 多角形の塗りは raster.s。

	.module	gfx


	.area	_DATA
_g_buf_hi::	.ds	1	; 描画バッファの上位バイト(0xC0)
_g_color::	.ds	1	; 塗る色
_g_rx::		.ds	1	; rect_fill・pset・glyph1: 左上(と幅・高さ。クリップ済み)
_g_ry::		.ds	1
_g_rw::		.ds	1
_g_rh::		.ds	1

	.area	_CODE

; PADDR: B = 列、C = 行 → HL = その列のバイトの番地(列の bit0 は見ない)。A を壊す
	.macro	PADDR
	ld	a, c
	rrca
	rrca
	rrca
	and	a, #0x1F
	ld	h, a
	ld	a, (_g_buf_hi)
	add	a, h
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
	.endm

; ---------------------------------------------------------------------------
; pf_band: H = 帯の上位バイト、A = 色のバイト → 帯の 256 バイトを push で塗る(2 バイトずつ。ld (hl), a より約 3 倍速い)
;   SP を帯の終わりに向けるので、その間は割り込みを止める(割り込みが積むと前の帯やバッファの外を壊す)。HL・DE を壊す
; ---------------------------------------------------------------------------
pf_band:
	ld	e, a
	ld	d, a
	inc	h
	ld	l, #0			; 帯の終わり(push は先に SP を減らしてから書く)
	ld	(pf_sp), sp
	di
	ld	sp, hl
	.rept	128
	push	de
	.endm
	ld	sp, (pf_sp)
	ei
	ret

; ---------------------------------------------------------------------------
; void buf_fill(u8 color)   A = 色。バッファ全体(48 行)を塗る
; ---------------------------------------------------------------------------
_buf_fill::
	ld	c, a
	add	a, a
	add	a, a
	add	a, a
	add	a, a
	or	a, c			; 両方のブロックが同じ色のバイト
	ld	c, a
	ld	a, (_g_buf_hi)
	ld	h, a
	ld	b, #6			; 帯 6 本
1$:
	ld	a, c
	push	hl
	call	pf_band
	pop	hl
	inc	h
	djnz	1$
	ret

; ---------------------------------------------------------------------------
; void buf_fill_rows(u8 color, u8 row)   A = 色、L = 最初の行。row から g_rh 行を塗る
;   8 行の帯をまるごと含むところは 256 バイト続けて、残りの行は 1 行 32 バイトを +8 ずつ
; ---------------------------------------------------------------------------
_buf_fill_rows::
	ld	c, a
	add	a, a
	add	a, a
	add	a, a
	add	a, a
	or	a, c
	ld	(fr_cc), a
	ld	c, l			; C = 行
	ld	a, (_g_rh)
	or	a, a
	ret	z
	add	a, c
	ld	b, a			; B = 終わりの行(含まない)
1$:
	ld	a, c
	cp	a, b
	ret	nc
	; 帯の頭で、帯の終わり(+8)まで塗るなら 256 バイトまとめて
	and	a, #7
	jp	nz, 3$
	ld	a, c
	add	a, #8
	cp	a, b
	jr	z, 2$
	jp	nc, 3$
2$:
	push	bc
	ld	a, c
	rrca
	rrca
	rrca
	and	a, #0x1F
	ld	h, a
	ld	a, (_g_buf_hi)
	add	a, h
	ld	h, a
	ld	a, (fr_cc)
	call	pf_band
	pop	bc
	ld	a, c
	add	a, #8
	ld	c, a
	jp	1$
3$:
	; 1 行: 32 バイトを +8 ずつ
	push	bc
	ld	b, #0
	PADDR
	ld	bc, #8
	ld	a, (fr_cc)
	.rept	32
	ld	(hl), a
	add	hl, bc
	.endm
	pop	bc
	inc	c
	jp	1$

	.area	_DATA
fr_cc:		.ds	1
fr_lo:		.ds	1
fr_hi:		.ds	1
pf_sp:		.ds	2	; pf_band の間の SP の退避
	.area	_CODE

; ---------------------------------------------------------------------------
; void rect_fill(void)   g_rx, g_ry, g_rw, g_rh(クリップ済み、幅・高さ 1 以上)を g_color で塗る
;   列(バイトの列)ごとに縦に塗る。詰めた並びでは、帯の中の上下の行は隣のバイト(+1)なので、
;   1 バイトあたり書いて inc l だけで済む(帯をまたぐときだけ上位を +1、下位を -8)。
;   色は塗る命令の即値に書き込む(自己書き換え。コードは RAM 上)。敵の矩形は小さい(平均 3×4 ブロック)ので、準備を軽くする
; ---------------------------------------------------------------------------
	.area	_DATA
rq_tail:	.ds	1	; 1 = 右端に左半分だけの列が残る
	.area	_CODE

_rect_fill::
	ld	a, (_g_color)
	and	a, #0x0F
	ld	(rq_lo + 1), a
	ld	b, a
	add	a, a
	add	a, a
	add	a, a
	add	a, a
	ld	(rq_hi + 1), a
	or	a, b
	ld	(rq_cc + 1), a
	ld	a, (_g_rx)
	ld	b, a
	ld	a, (_g_ry)
	ld	c, a
	PADDR				; HL = 左上のバイト
	ld	a, c
	and	a, #7
	ld	c, a
	ld	a, #8
	sub	a, c
	ld	d, a			; 帯の終わりまでの行数
	ld	a, (_g_rh)
	ld	e, a			; E = 行数
	cp	a, d
	jr	nc, rqx1
	ld	d, a			; D = 最初の帯の中で塗る行数
rqx1:
	ld	a, (_g_rw)
	ld	c, a			; C = 幅
	bit	0, b
	jr	z, rqx3
	; 左端が奇数: 右半分だけの列
	push	hl
	push	bc
	; 縦に 1 列: HL = 一番上のバイト、D = 最初の帯の中の行数、E = 行数
	ld	b, d
	ld	a, e
	sub	a, d
	ld	c, a			; 最初の区切りの後に残る行数
rql1:
	ld	a, (hl)
	and	a, #0xF0
rq_lo:
	or	a, #0			; 色(書き換える)
	ld	(hl), a
	inc	l
	djnz	rql1
	ld	a, c
	or	a, a
	jr	z, rql9
	inc	h			; 次の帯の同じ列
	ld	a, l
	sub	a, #8
	ld	l, a
	ld	a, c
	cp	a, #8
	jr	c, rql2
	ld	b, #8
	sub	a, b
	ld	c, a
	jr	rql1
rql2:
	ld	b, c
	ld	c, #0
	jr	rql1
rql9:
	pop	bc
	pop	hl
	ld	a, l
	add	a, #8
	ld	l, a
	dec	c
	ret	z
rqx3:
	ld	a, c
	and	a, #1
	ld	(rq_tail), a
	ld	a, c
	srl	a			; 丸ごとの列の数
	jr	z, rqx5
	ld	b, a
rqx4:
	push	hl
	push	bc
	; 縦に 1 列: HL = 一番上のバイト、D = 最初の帯の中の行数、E = 行数
	ld	b, d
	ld	a, e
	sub	a, d
	ld	c, a			; 最初の区切りの後に残る行数
rqc1:
rq_cc:
	ld	(hl), #0		; 両方のブロックが同じ色のバイト(書き換える)
	inc	l
	djnz	rqc1
	ld	a, c
	or	a, a
	jr	z, rqc9
	inc	h			; 次の帯の同じ列
	ld	a, l
	sub	a, #8
	ld	l, a
	ld	a, c
	cp	a, #8
	jr	c, rqc2
	ld	b, #8
	sub	a, b
	ld	c, a
	jr	rqc1
rqc2:
	ld	b, c
	ld	c, #0
	jr	rqc1
rqc9:
	pop	bc
	pop	hl
	ld	a, l
	add	a, #8
	ld	l, a
	djnz	rqx4
rqx5:
	ld	a, (rq_tail)
	or	a, a
	ret	z
	; 右端が奇数で残った列: 左半分だけ
	; 縦に 1 列: HL = 一番上のバイト、D = 最初の帯の中の行数、E = 行数
	ld	b, d
	ld	a, e
	sub	a, d
	ld	c, a			; 最初の区切りの後に残る行数
rqh1:
	ld	a, (hl)
	and	a, #0x0F
rq_hi:
	or	a, #0			; 色 << 4(書き換える)
	ld	(hl), a
	inc	l
	djnz	rqh1
	ld	a, c
	or	a, a
	jr	z, rqh9
	inc	h			; 次の帯の同じ列
	ld	a, l
	sub	a, #8
	ld	l, a
	ld	a, c
	cp	a, #8
	jr	c, rqh2
	ld	b, #8
	sub	a, b
	ld	c, a
	jr	rqh1
rqh2:
	ld	b, c
	ld	c, #0
	jr	rqh1
rqh9:
	ret

; ---------------------------------------------------------------------------
; void pset(void)   (g_rx, g_ry) の 1 ブロックを g_color にする(画面内であること)
; ---------------------------------------------------------------------------
_pset::
	ld	a, (_g_rx)
	ld	b, a
	ld	a, (_g_ry)
	ld	c, a
	PADDR
	ld	a, (_g_color)
	and	a, #0x0F
	ld	c, a
	bit	0, b
	jr	nz, 1$
	add	a, a
	add	a, a
	add	a, a
	add	a, a
	ld	c, a
	ld	a, (hl)
	and	a, #0x0F
	or	a, c
	ld	(hl), a
	ret
1$:
	ld	a, (hl)
	and	a, #0xF0
	or	a, c
	ld	(hl), a
	ret

; ---------------------------------------------------------------------------
; void blit_trans(u8 count)   A = ブロック数。g_bt_src(1 ブロック 1 バイト、0 = 透過)から
;   (g_bt_x, g_bt_y) の行へ、左から count ブロック写す(画面内に収まっていること)
; ---------------------------------------------------------------------------
	.area	_DATA
_g_bt_src::	.ds	2
_g_bt_x::	.ds	1
_g_bt_y::	.ds	1

	.area	_CODE
_blit_trans::
	or	a, a
	ret	z
	ld	e, a			; E = 残り
	ld	a, (_g_bt_x)
	ld	b, a
	ld	a, (_g_bt_y)
	ld	c, a
	PADDR
	ex	de, hl			; DE = 描き先、L = 残り
	ld	c, l
	ld	hl, (_g_bt_src)
	; B = 列(偶奇を見る)、C = 残り、HL = 元、DE = 描き先
1$:
	ld	a, (hl)
	inc	hl
	and	a, #0x0F
	jr	z, 3$
	bit	0, b
	jr	nz, 2$
	add	a, a
	add	a, a
	add	a, a
	add	a, a
	push	bc
	ld	c, a
	ld	a, (de)
	and	a, #0x0F
	or	a, c
	ld	(de), a
	pop	bc
	jr	3$
2$:
	push	bc
	ld	c, a
	ld	a, (de)
	and	a, #0xF0
	or	a, c
	ld	(de), a
	pop	bc
3$:
	bit	0, b
	jr	z, 4$
	ld	a, e			; 奇数の列を描いたら次のバイトへ(+8)
	add	a, #8
	ld	e, a
4$:
	inc	b
	dec	c
	jr	nz, 1$
	ret

; ---------------------------------------------------------------------------
; void runs_draw(const u8 *tbl, u16 y0)   HL = 表、E = 描き始めの行
;   表(gfx.s の trolley_runs が作る): 5 バイトずつ、2 バイト目 = 0xFF で終わり。
;     (番地の下位, 帯 | 行 << 4, n, 元の下位, 元の上位)   n >= 1: 丸ごとのバイト n 個を元から写す(+8 ずつ)
;     (番地の下位, 帯 | 行 << 4, 0, 残すビット, 値)       半分だけのバイト: (描き先 & 残すビット) | 値
;   番地の下位と帯は、揺れの無い位置(y0 = TR_Y)のもの。そのときは番地を計算せずにそのまま使う。
;   揺れているとき(y0 = TR_Y + 1)は、行 >= g_rh を描かず、行と列(番地の下位 >> 3)から番地を求め直す
; ---------------------------------------------------------------------------
TR_Y	= 33			; 乗り物の絵の一番上の行(trolley.h の TROLLEY_Y = GAME_ROWS - TROLLEY_H)
	.area	_DATA
rd_y0:		.ds	1
	.area	_CODE
_runs_draw::
	ld	a, (_g_buf_hi)
	ld	(3$ + 1), a
	ld	a, e
	cp	a, #TR_Y
	jr	nz, 20$
	; 揺れていない: 表の番地をそのまま使う
1$:
	ld	e, (hl)
	inc	hl
	ld	a, (hl)
	inc	hl
	cp	a, #0xFF
	ret	z
	and	a, #0x0F
3$:
	add	a, #0			; 描画バッファの上位(書き換える)
	ld	d, a			; DE = 描き先
	ld	a, (hl)
	inc	hl
	or	a, a
	jr	z, 5$
	; 丸ごとのバイトを n 個
	ld	b, a
	ld	c, (hl)
	inc	hl
	ld	a, (hl)
	inc	hl
	push	hl
	ld	h, a
	ld	l, c			; HL = 元
2$:
	ld	a, (hl)
	inc	hl
	ld	(de), a
	ld	a, e
	add	a, #8
	ld	e, a
	djnz	2$
	pop	hl
	jr	1$
5$:
	; 半分だけ
	ld	a, (de)
	and	a, (hl)
	inc	hl
	or	a, (hl)
	inc	hl
	ld	(de), a
	jr	1$
	; 揺れている: 行と列から番地を求め直す
20$:
	ld	(rd_y0), a
21$:
	inc	hl
	ld	a, (hl)			; 帯 | 行 << 4
	dec	hl
	cp	a, #0xFF
	ret	z
	rrca
	rrca
	rrca
	rrca
	and	a, #0x0F
	ld	c, a			; C = 絵の行
	ld	a, (_g_rh)
	cp	a, c
	jr	z, 29$
	jr	c, 29$
	push	hl
	ld	a, (rd_y0)
	add	a, c
	ld	c, a			; C = 画面の行
	ld	a, (hl)			; 番地の下位 = 列ペア << 3 | 行の下位
	rrca
	rrca
	and	a, #0x3E
	ld	b, a			; B = 列
	inc	hl
	inc	hl
	ld	a, (hl)			; n
	inc	hl
	ld	e, (hl)
	inc	hl
	ld	d, (hl)			; DE = 元 / (残すビット, 値)
	push	af
	PADDR
	pop	af
	or	a, a
	jr	z, 25$
	ld	b, a
	ld	c, #8
22$:
	ld	a, (de)
	inc	de
	ld	(hl), a
	ld	a, l
	add	a, c
	ld	l, a
	djnz	22$
	pop	hl
	jr	29$
25$:
	ld	a, (hl)
	and	a, e
	or	a, d
	ld	(hl), a
	pop	hl
29$:
	ld	de, #5
	add	hl, de
	jr	21$

; ---------------------------------------------------------------------------
; void trolley_runs(void)   乗り物の絵 timg(64×13、1 点 1 バイト、0 = 透過)から、runs_draw の表 runs と
;   丸ごと不透明なバイトの詰めた値 pimg を作る(trolley.c。振り向いて真横を過ぎたときと被弾のときに呼ぶ)。
;   列ペア(2 点 = 1 バイト)ごとに: 両方不透明 → 続くかぎり 1 つの区間、片方だけ → 半分のバイト、両方透過 → 飛ばす。
;   表は 120 個まで(trolley.c の MAX_RUNS)。HL = timg の位置、DE = pimg の位置、IY = 表、B = 列ペア、C = 行
; ---------------------------------------------------------------------------
	.area	_DATA
tr_left:	.ds	1	; 表にあと足せる個数
tr_hb:		.ds	1	; いまの行の「帯 | 行 << 4」
	.area	_CODE
_trolley_runs::
	push	iy
	ld	iy, #_runs
	ld	a, #120
	ld	(tr_left), a
	ld	hl, #_timg
	ld	de, #_pimg
	ld	c, #0
1$:
	; この行の、番地の下位の行の部分(y & 7)と「帯 | 行 << 4」(y = 行 + TR_Y)
	ld	a, c
	add	a, #TR_Y
	ld	b, a
	and	a, #7
	ld	(tr_y7 + 1), a
	ld	a, b
	rrca
	rrca
	rrca
	and	a, #0x1F
	ld	b, a
	ld	a, c
	add	a, a
	add	a, a
	add	a, a
	add	a, a
	or	a, b
	ld	(tr_hb), a
	ld	b, #0
2$:
	ld	a, (hl)
	or	a, a
	jr	z, 5$
	inc	hl
	ld	a, (hl)
	dec	hl
	or	a, a
	jr	z, 4$
	; 両方不透明: 続くかぎり 1 つの区間(値は pimg に詰める)
	call	tr_head
	ld	2 (iy), b		; 始めの列ペア(あとで個数に直す)
	ld	3 (iy), e
	ld	4 (iy), d
3$:
	ld	a, (hl)
	add	a, a
	add	a, a
	add	a, a
	add	a, a
	inc	hl
	or	a, (hl)
	inc	hl
	ld	(de), a
	inc	de
	inc	b
	ld	a, b
	cp	a, #32
	jr	z, 31$
	ld	a, (hl)
	or	a, a
	jr	z, 31$
	inc	hl
	ld	a, (hl)
	dec	hl
	or	a, a
	jr	nz, 3$
31$:
	ld	a, b
	sub	a, 2 (iy)
	ld	2 (iy), a
	jr	7$
4$:
	; 左だけ不透明
	ld	a, (hl)
	add	a, a
	add	a, a
	add	a, a
	add	a, a
	ld	4 (iy), a
	ld	3 (iy), #0x0F		; 残すビット
	jr	6$
5$:
	inc	hl
	ld	a, (hl)
	dec	hl
	or	a, a
	jr	z, 9$			; 両方透過
	; 右だけ不透明
	ld	4 (iy), a
	ld	3 (iy), #0xF0
6$:
	call	tr_head
	ld	2 (iy), #0
	inc	hl
	inc	hl
	inc	de
	inc	b
7$:
	inc	iy
	inc	iy
	inc	iy
	inc	iy
	inc	iy
	ld	a, (tr_left)
	dec	a
	ld	(tr_left), a
	jr	z, 11$			; 表が一杯(元の C も、ここから先の行では何も足さない)
	jr	10$
9$:
	inc	hl
	inc	hl
	inc	de
	inc	b
10$:
	ld	a, b
	cp	a, #32
	jp	nz, 2$
	inc	c
	ld	a, c
	cp	a, #13
	jp	nz, 1$
11$:
	ld	1 (iy), #0xFF
	pop	iy
	ret
; 表の 1・2 バイト目: 番地の下位 = 列ペア << 3 | (y & 7)、帯 | 行 << 4
tr_head:
	ld	a, b
	add	a, a
	add	a, a
	add	a, a
tr_y7:
	or	a, #0			; y & 7(行ごとに書き換える)
	ld	0 (iy), a
	ld	a, (tr_hb)
	ld	1 (iy), a
	ret

; ---------------------------------------------------------------------------
; void glyph1(const u8 *g)   HL = 3×5 の字形(5 バイト、下位 3bit。bit2 が左)。g_rx, g_ry の位置に g_color で描く
;   倍率 1 の文字用。画面内(x <= 61, y <= 43)であること(hud.c が確かめる)
;   列ごとに縦に描く: 先に 3 列それぞれの「5 行分のビット」を作り、番地は 1 回だけ求める。
;   次の列へは、いまの列が偶数なら同じバイトの右半分、奇数なら次のバイト(+8)の左半分
; ---------------------------------------------------------------------------
	.area	_DATA
gl_col:		.ds	3	; 列 0, 1, 2 のビット(bit0 = 一番上の行)
	.area	_CODE
_glyph1::
	; 列ごとのビット: 下の行から順に、bit0 → 列 2、bit1 → 列 1、bit2 → 列 0 へ送り込む
	ld	de, #4
	add	hl, de			; 一番下の行
	ld	e, #5
1$:
	ld	a, (hl)
	dec	hl
	rra
	rl	d			; 列 2
	rra
	rl	c			; 列 1
	rra
	rl	b			; 列 0
	dec	e
	jr	nz, 1$
	ld	a, b
	ld	(gl_col), a
	ld	a, c
	ld	(gl_col + 1), a
	ld	a, d
	ld	(gl_col + 2), a
	ld	a, (_g_color)
	and	a, #0x0F
	ld	(gl_lo + 1), a
	add	a, a
	add	a, a
	add	a, a
	add	a, a
	ld	(gl_hi + 1), a
	ld	a, (_g_rx)
	ld	b, a
	ld	a, (_g_ry)
	ld	c, a
	PADDR
	ld	e, b			; E の bit0 = いまの列の偶奇
	ld	a, (gl_col)
	call	gl_one
	ld	a, (gl_col + 1)
	call	gl_one
	ld	a, (gl_col + 2)
	; そのまま gl_one へ

; 1 列: A = ビット、HL = 一番上のバイト、E の bit0 = 列の偶奇。描いたら HL・E を次の列へ進める
gl_one:
	push	hl
	ld	c, a
	ld	b, #5
	bit	0, e
	jr	nz, glx5
glx2:
	; 偶数の列: 上位 4bit
	srl	c
	jr	nc, glx3
	ld	a, (hl)
	and	a, #0x0F
gl_hi:
	or	a, #0			; 色 << 4(書き換える)
	ld	(hl), a
glx3:
	inc	l
	ld	a, l
	and	a, #7
	jr	nz, glx4
	inc	h			; 帯をまたぐ
	ld	a, l
	sub	a, #8
	ld	l, a
glx4:
	djnz	glx2
	pop	hl
	inc	e			; 次の列は同じバイトの右半分
	ret
glx5:
	; 奇数の列: 下位 4bit
	srl	c
	jr	nc, glx6
	ld	a, (hl)
	and	a, #0xF0
gl_lo:
	or	a, #0			; 色(書き換える)
	ld	(hl), a
glx6:
	inc	l
	ld	a, l
	and	a, #7
	jr	nz, glx7
	inc	h
	ld	a, l
	sub	a, #8
	ld	l, a
glx7:
	djnz	glx5
	pop	hl
	ld	a, l			; 次の列は次のバイト(+8)の左半分
	add	a, #8
	ld	l, a
	inc	e
	ret
