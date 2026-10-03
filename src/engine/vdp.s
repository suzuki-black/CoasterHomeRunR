; vdp.s — VDP(V9958)アクセス。SCREEN 3(マルチカラー)のダブルバッファ
;
; ■ VRAM マップ(仕様書 7.4)
;   0000h-05FFh  パターンジェネレータ A   R#4 = 00h
;   0800h-0DFFh  パターンジェネレータ B   R#4 = 01h
;   1000h-12FFh  ネームテーブル            R#2 = 04h
;   1400h-147Fh  スプライトアトリビュート  R#5 = 28h, R#11 = 00h
;   1800h-1FFFh  スプライトパターン        R#6 = 03h
;
; ■ ブロック座標(bx 0-63, by 0-47)と VRAM の対応
;   ネームテーブルの行 r の値 = (r >> 2) * 32 + 列 なので、パターン番号 = (by >> 3) * 32 + (bx >> 1)、
;   パターン内のバイト = by & 7。よって
;     addr = 先頭 + (by >> 3) * 256 + (bx >> 1) * 8 + (by & 7)   (bx 偶数=上位4bit / 奇数=下位4bit)
;   8 行ずつの帯ごとに「列ペアごとに縦 8 バイト」が並ぶ。vdp_xfer はこの順で 1536 バイトを連続出力する。
;
; ■ R800 と VDP I/O
;   R800 モードでは VDP ポートへの連続アクセスは S1990 が最低 62 サイクル(≒8.7µs)空ける。
;   1536 バイトの転送は約 13ms。パック変換の計算はこの待ちの中に隠れる。
;
; ■ 99h への 2 バイト書き込みは DI で囲む(BIOS の割り込み処理が S#0 を読むため)。

	.module	vdp


	.area	_CODE

; void vdp_wreg(u8 reg, u8 val)   A=reg, L=val
_vdp_wreg::
	di
	ld	c, a
	ld	a, l
	out	(0x99), a
	ld	a, c
	or	a, #0x80
	out	(0x99), a
	ei
	dec	c			; R#1 は写しを覚えておく(Z80 転送が割り込みを一時的に止めるため)
	ret	nz
	ld	a, l
	ld	(_g_vdp_r1), a
	ret

	.area	_DATA
_g_vdp_r1::	.ds	1	; 最後に書いた R#1
	.area	_CODE

; void vdp_set_write(u16 addr)   HL=addr(0000h-3FFFh)
_vdp_set_write::
	di
	xor	a, a
	out	(0x99), a
	ld	a, #0x8E		; R#14 = 0
	out	(0x99), a
	ld	a, l
	out	(0x99), a
	ld	a, h
	and	a, #0x3F
	or	a, #0x40
	out	(0x99), a
	ei
	ret

; void vdp_out(const u8 *src, u16 len)   HL=src, DE=len(直前の vdp_set_write から連続)
_vdp_out::
	ld	c, #0x98
1$:
	ld	a, d
	or	a, e
	ret	z
	outi
	dec	de
	jr	1$

; void vdp_fill(u8 val, u16 len)   A=val, DE=len
_vdp_fill::
	ld	b, a
1$:
	ld	a, d
	or	a, e
	ret	z
	ld	a, b
	out	(0x98), a
	dec	de
	jr	1$

; void vdp_set_palette(u8 first, const u8 *pal)   A=最初の番号, DE=(RB, G)×n。n は 16-first
_vdp_set_palette::
	di
	ld	c, a
	out	(0x99), a
	ld	a, #0x90		; R#16 = パレット番号
	out	(0x99), a
	ld	a, #16
	sub	a, c
	add	a, a
	ld	b, a
	ex	de, hl
	ld	c, #0x9A
	otir
	ei
	ret

; ---------------------------------------------------------------------------
; void vdp_xfer(u8 page)   A=0/1
;   描画バッファ(g_buf_hi << 8、VRAM と同じ並びに詰めた 1536 バイト。gfx.s)を、パターンジェネレータ page へ送る。
;   R800 では 1 バイトごとに 62 サイクル待たされる(S1990)ので、OTIR で十分(8.67µs/バイト)。
;   ファームウェアに CHGCPU がある機種(実機)では、Z80 に切り替えて OUTI で送る(5.45µs/バイト。sys.s の xfer_z80)。
;   下の帯(40-47 行目)の列ペア 2-29(224 バイト)はコースターの車体とゲージで覆われていて、ふだんは変わらない。
;   ページごとに最後に送った中身の写しと比べ、同じなら送らない(xfer_prep。2 枚を交互に使うので比べる相手は 2 フレーム前)。
; ---------------------------------------------------------------------------
	.globl	_g_buf_hi, _g_z80x, _xfer_z80

XF_SKIP_OFS = 0x500 + 16	; 下の帯の列ペア 2 から
XF_SKIP_LEN = 224		; 列ペア 2-29

	.area	_DATA
_g_xfer_ok::	.ds	1	; bit p = ページ p の写しが正しい(vdp_init で VRAM を消したら 0 にする)
_g_xfer_skip::	.ds	1	; 1 = このフレームは下の帯の中央を送らない
xf_page:	.ds	1
xf_shadow:	.ds	XF_SKIP_LEN * 2
	.area	_CODE

; void xfer_invalidate(void) — 写しを捨てる(VRAM のパターンジェネレータを vdp_xfer 以外で書き換えたとき)
_xfer_invalidate::
	xor	a, a
	ld	(_g_xfer_ok), a
	ret

; xfer_prep: A = ページ → g_xfer_skip を決める。送るときは写しを更新する
_xfer_prep::
	ld	(xf_page), a
	ld	hl, #xf_shadow
	or	a, a
	jr	z, 1$
	ld	de, #XF_SKIP_LEN
	add	hl, de
1$:
	ex	de, hl			; DE = このページの写し
	ld	a, (_g_buf_hi)
	add	a, #>XF_SKIP_OFS
	ld	h, a
	ld	l, #<XF_SKIP_OFS	; HL = 描画バッファの下の帯の列ペア 2
	ld	a, (xf_page)
	inc	a			; ページ 0 → bit0、ページ 1 → bit1
	ld	b, a
	ld	a, (_g_xfer_ok)
	and	a, b
	jr	z, 4$
	push	hl
	push	de
	ld	c, #XF_SKIP_LEN		; CPI は BC を 1 ずつ減らす。C = 224 なら 224 回の間に B へ繰り下がらない
	ld	b, #XF_SKIP_LEN / 8	; B = 8 バイトの組の数(djnz)
2$:
	.rept	8
	ld	a, (de)
	inc	de
	cpi				; A と (HL) を比べて HL を進める
	jr	nz, 3$
	.endm
	djnz	2$
	pop	de
	pop	hl
	ld	a, #1
	ld	(_g_xfer_skip), a
	ret
3$:
	pop	de
	pop	hl
4$:
	ld	bc, #XF_SKIP_LEN
	ldir
	ld	a, (xf_page)
	inc	a
	ld	b, a
	ld	a, (_g_xfer_ok)
	or	a, b
	ld	(_g_xfer_ok), a
	xor	a, a
	ld	(_g_xfer_skip), a
	ret

; xfer_skip_addr: 書き込み先を、いまのページの下の帯の列ペア 30 へ付け直す(割り込みを止めて)。HL を 224 進める
_xfer_skip_addr::
	ld	a, (xf_page)
	add	a, a
	add	a, a
	add	a, a
	add	a, #>(XF_SKIP_OFS + XF_SKIP_LEN)
	or	a, #0x40
	ld	b, a
	di
	ld	a, #<(XF_SKIP_OFS + XF_SKIP_LEN)
	out	(0x99), a
	ld	a, b
	out	(0x99), a
	ei
	ld	de, #XF_SKIP_LEN
	add	hl, de
	ret

_vdp_xfer::
	push	af
	call	_xfer_prep
	pop	af
	ld	c, a
	ld	a, (_g_z80x)
	or	a, a
	ld	a, c
	jp	nz, _xfer_z80		; ファームウェアに CHGCPU があれば Z80 で送る(sys.s)
	; 書き込み先 = page * 0x800
	add	a, a
	add	a, a
	add	a, a			; 上位バイト = page * 8
	di
	ld	c, a
	xor	a, a
	out	(0x99), a
	ld	a, #0x8E
	out	(0x99), a
	xor	a, a
	out	(0x99), a
	ld	a, c
	or	a, #0x40
	out	(0x99), a
	ei
	ld	a, (_g_buf_hi)
	ld	h, a
	ld	l, #0
	ld	c, #0x98
	ld	d, #5			; 256 バイト × 5
1$:
	ld	b, #0
	otir
	dec	d
	jr	nz, 1$
	ld	b, #16			; 下の帯の列ペア 0-1
	otir
	ld	a, (_g_xfer_skip)
	or	a, a
	jr	nz, 2$
	ld	b, #XF_SKIP_LEN
	otir
	jr	3$
2$:
	call	_xfer_skip_addr
	ld	c, #0x98
3$:
	ld	b, #16			; 列ペア 30-31
	otir
	ret
