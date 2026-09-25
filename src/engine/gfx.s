; gfx.s — 描画バッファ(0xC000、64×48、1 ブロック 1 バイト)への塗りつぶし
;
; 多角形の塗りは raster.s。ここはバッファの塗り分けと矩形。

	.module	gfx


	.area	_DATA
_g_buf_hi::	.ds	1	; 描画先バッファの上位バイト(0xC0 / 0x94。main.c が交互に切り替える)
_g_color::	.ds	1	; 塗る色
_g_rx::		.ds	1	; rect_fill: 左上と幅・高さ(クリップ済み)
_g_ry::		.ds	1
_g_rw::		.ds	1
_g_rh::		.ds	1

	.area	_CODE

; ---------------------------------------------------------------------------
; void buf_fill(u8 color, u8 *dst)   A=色, DE=先頭。dst から 48 行(3072 バイト)を塗る
; ---------------------------------------------------------------------------
_buf_fill::
	ld	c, a
	ld	a, #48
	ld	(_g_rh), a
	ld	a, c
	; そのまま buf_fill_rows へ

; ---------------------------------------------------------------------------
; void buf_fill_rows(u8 color, u8 *dst)   のうち行数を g_rh で指定する版
;   dst から g_rh 行(64 バイト単位)を塗る
; ---------------------------------------------------------------------------
_buf_fill_rows::
	ex	de, hl
	ld	c, a
	ld	a, (_g_rh)
	or	a, a
	ret	z
	ld	b, a
	ld	a, c
1$:
	.rept	64
	ld	(hl), a
	inc	hl
	.endm
	dec	b
	jp	nz, 1$
	ret

; ---------------------------------------------------------------------------
; void rect_fill(void)   g_rx, g_ry, g_rw, g_rh(クリップ済み、幅・高さ 1 以上)を g_color で塗る
; ---------------------------------------------------------------------------
_rect_fill::
	ld	a, (_g_ry)
	ld	l, a
	ld	h, #0
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	a, (_g_rx)
	ld	e, a
	ld	d, #0
	add	hl, de
	ld	a, (_g_buf_hi)
	ld	d, a
	ld	e, #0
	add	hl, de
	ld	a, (_g_rh)
	ld	c, a
	ld	a, (_g_rw)
	ld	e, a
	ld	a, #64
	sub	a, e
	ld	e, a			; DE = 行末から次の行頭までの差
	ld	a, (_g_color)
1$:
	ld	b, a
	ld	a, (_g_rw)
	ld	d, a
	ld	a, b
2$:
	ld	(hl), a
	inc	hl
	dec	d
	jr	nz, 2$
	add	hl, de			; D は 0
	dec	c
	jr	nz, 1$
	ret

; ---------------------------------------------------------------------------
; void blit_trans(u8 count)   A=バイト数。g_bt_src → g_bt_dst へ、0 を透過して写す
; ---------------------------------------------------------------------------
	.area	_DATA
_g_bt_src::	.ds	2
_g_bt_dst::	.ds	2

	.area	_CODE
_blit_trans::
	or	a, a
	ret	z
	ld	b, a
	ld	hl, (_g_bt_src)
	ld	de, (_g_bt_dst)
1$:
	ld	a, (hl)
	or	a, a
	jr	z, 2$
	ld	(de), a
2$:
	inc	hl
	inc	de
	djnz	1$
	ret

; ---------------------------------------------------------------------------
; void bench_fill64(void) — 計測用(make BENCH=1): 64 バイトの展開した塗り
; void bench_nop64(void)  — 計測用: 64 個の nop
; ---------------------------------------------------------------------------
_bench_fill64::
	ld	hl, #0xC000
	ld	a, #1
	.rept	64
	ld	(hl), a
	inc	l
	.endm
	ret
_bench_nop64::
	.rept	64
	nop
	.endm
	ret
; void bench_push64(void) — 計測用: push で 64 バイト塗る
	.area	_DATA
bp_sp:	.ds	2
	.area	_CODE
_bench_push64::
	di
	ld	(bp_sp), sp
	ld	sp, #0xC040
	ld	bc, #0x0101
	.rept	32
	push	bc
	.endm
	ld	sp, (bp_sp)
	ei
	ret

; ---------------------------------------------------------------------------
; void runs_draw(const u8 *tbl, u8 *base)   HL=表、DE=描き先の先頭行
;   表: (行, 列, 長さ, 元のアドレス 2 バイト) × n、行 = 0xFF で終わり。行 >= g_rh は描かない
;   トロッコの縁の不透明な部分を、連続区間ごとに LDIR で写す(透過判定を 1 バイトずつしない)
; ---------------------------------------------------------------------------
	.area	_DATA
rd_base:	.ds	2
	.area	_CODE
_runs_draw::
	ld	(rd_base), de
1$:
	ld	a, (hl)			; 行
	cp	a, #0xFF
	ret	z
	ld	b, a
	ld	a, (_g_rh)
	cp	a, b
	jr	z, 2$
	jr	c, 2$			; 行 >= g_rh(揺れで下にはみ出す行)
	push	hl
	; 描き先 = base + 行 * 64 + 列
	inc	hl
	ld	c, (hl)			; 列
	inc	hl
	ld	a, (hl)			; 長さ
	inc	hl
	ld	e, (hl)
	inc	hl
	ld	d, (hl)			; DE = 元
	push	de
	push	af
	ld	l, b
	ld	h, #0
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	b, #0
	add	hl, bc
	ld	de, (rd_base)
	add	hl, de
	ex	de, hl			; DE = 描き先
	pop	af
	ld	c, a
	ld	b, #0
	pop	hl			; HL = 元
	ldir
	pop	hl
2$:
	ld	de, #5
	add	hl, de
	jr	1$
