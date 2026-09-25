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
	ret

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
;   描画バッファ(g_buf_hi << 8)を SCREEN 3 形式に詰めながら、パターンジェネレータ page へ転送する。
;   バッファの各バイトは 0-15 であること(上位 4bit は見ない前提で詰める)。
;   R800 では 1 バイトごとに 62 サイクル待たされるので、詰める計算(約 20 サイクル)はその待ちに隠れる。
;   ★「描画の裏で 1 バイトずつ送る(ストリーム転送)」も試したが、1 バイト送る処理のオーバーヘッド
;     (約 40 サイクル)が待ち時間とほぼ同じで、かえって遅くなった(docs/検証記録.md)。
; ---------------------------------------------------------------------------
	.globl	_g_buf_hi
_vdp_xfer::
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
	ld	de, #63			; 次の行(同じ列ペア)へ
	ld	c, #6			; 8 行の帯 × 6
1$:
	ld	b, #32			; 列ペア × 32
2$:
	push	hl
	.rept	8
	ld	a, (hl)
	add	a, a
	add	a, a
	add	a, a
	add	a, a
	inc	l			; 列ペアは偶数番地から始まるので下位だけで足りる
	or	a, (hl)
	out	(0x98), a
	add	hl, de
	.endm
	pop	hl
	inc	l			; 次の列ペア(行内なので下位だけで足りる)
	inc	l
	djnz	2$
	ld	a, l			; この帯の先頭(列 0 の直後 = +64)から 8 行下へ: +512-64
	sub	a, #64
	ld	l, a
	inc	h
	inc	h			; +512
	dec	c
	jr	nz, 1$
	ret
