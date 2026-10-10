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
	.r800				; IXH・IXL(xfer_prep)
	.include "r800.inc"


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
;   差分転送: 8 バイトの組(列ペア 1 つ × 8 行 = パターン 1 つ)ごとに、そのページへ最後に送った中身の写しと比べ、
;   変わった組の続き(区間)だけを送る。2 枚を交互に使うので、比べる相手は 2 フレーム前の絵。
;   ブロックが粗いので、隣り合うフレームでも絵の大半は変わらない(ステージ 1 で組の 7〜9 割、ステージ 3 で 5〜8 割が同じ)。
;   写しは、比べるコード(下の XF_PROG)の即値として持つ。
;   xfer_prep が比べたあと、送る処理そのもの(区間ごとに「書き込み先を付け直す → 描画バッファの番地 → OUTI の列へ call」)を
;   XF_CODE に書き出す。書き出したコードは Z80 でも R800 でも動く(R800 専用の命令を使わない)。
;   R800 では VDP への 1 バイトごとに 62 サイクル待たされる(S1990。8.67µs/バイト)。ファームウェアに CHGCPU がある機種(実機)では、
;   送る量が多いときだけ Z80 に切り替えて送る(5.45µs/バイト。切り替えの往復 約 0.9ms。sys.s の xfer_z80)。
; ---------------------------------------------------------------------------
	.globl	_g_buf_hi, _g_z80x, _xfer_z80, _g_seg2
	.globl	z80x_out256, z80x_out_end
	.globl	_pcm_mode

SEG_XFER = 6			; 写し(比べるコード)を置くマッパーのセグメント。ページ p 用は SEG_XFER + p(比べる間だけページ2に出す。0-5 は使用中)
XF_CODE	== 0xC800		; 書き出す転送のコード(0xC800-0xCBFF。区間 1 つ 13 バイト + call 1 つ 3 バイト + ret)
XF_NCH	= 192			; 組の数
XF_MAXRUN = 60			; 区間の数の上限(これに達したら、残りを全部 1 つの区間で送る。コードは 1024 バイトに収まる)
XF_Z80_MIN = 20			; 組の数がこれ以上なら Z80 で送る(下の _vdp_xfer)

	.area	_DATA
_g_xfer_ok::	.ds	1	; bit p = ページ p の写しが正しい(vdp_init で VRAM を消したら 0 にする)
_g_xf_nrun::	.ds	1	; 送る区間の数
_g_xf_nch::	.ds	1	; 送る組の数
xf_page:	.ds	1
xf_seg:		.ds	1
xf_delta:	.ds	1	; 描画バッファの番地の上位 + これ = VRAM の書き込み先の上位(0x40 = 書き込み)
	.area	_CODE

; void xfer_invalidate(void) — 写しを捨てる(VRAM のパターンジェネレータを vdp_xfer 以外で書き換えたとき)
_xfer_invalidate::
	xor	a, a
	ld	(_g_xfer_ok), a
	ret

; ---- 比べるコード(写しのセグメント SEG_XFER + p に、ページ p 用を 0x8000 から)
;   組 k ごとに 36 バイト(XF_PROG + k * 36): 2 バイトずつ 4 回「pop hl / ld de, #写し / sbc hl, de / jp nz, 組 k の後始末」。
;   最後(XF_PROG + 192 * 36)は jp xp_fin。SP を描画バッファに向けて(割り込みを止めて)頭から走らせる。
;   写しはコードの即値として持つので、写しを読むメモリアクセスが無い(R800 はアクセスする 256 バイトのページが変わるたびに
;   待たされる。写しを別の表に置いて 1 バイトずつ比べると約 2 倍かかった)。
; ---- 組 k の後始末(XF_STUB + k * 34): 違う組があると飛んでくる。番地はすべて組ごとに埋め込んである
;   ld sp, #組の頭 / (pop de / ld (即値), de) × 4 / ld a, #k / ld (ix), a / inc ix / or a / jp 組 k + 1
;   即値を新しい中身に書き換え、組の番号を XF_LIST に書いて、次の組から続ける
XF_PROG	= 0x8000
XF_CH	= 36			; 組 1 つの比べるコードのバイト数
XF_STUB	= 0x9C00
XF_ST	= 34			; 後始末 1 つのバイト数(192 個で 0x9C00-0xB57F)
XF_LIST	= 0xC700		; 変わった組の番号の表(最大 192。0xC700-0xC7BF)
XP_EVERY = 8			; 声を鳴らしている間、この組ごとに区切って割込みを受ける(1 回 約 0.09ms。16 組では約 3 行遅れることがあった)
XP_NBRK	= XF_NCH / XP_EVERY - 1	; 区切りの数(8, 16, … 184 組目の頭。23 か所)

	.area	_DATA
xp_sp:		.ds	2	; 比べる間の SP の退避
pp_s:		.ds	1	; いまの区間の最初の組
xp_bsp:		.ds	2	; 区切りで割込みを受ける間の、比べる側の SP(描画バッファの中)
xp_bp:		.ds	2	; 次の区切りの表の位置
xp_tab:		.ds	XP_NBRK * 5	; 区切りごとに(組の頭の番地 2 バイト、書き換える前の 3 バイト)
	.area	_CODE

; xfer_prep: A = ページ → 写しと比べて XF_CODE に転送のコードを書き、写しを新しくする。
;   g_xf_nrun = 区間の数、g_xf_nch = 送る組の数(0 なら送るものが無い)
_xfer_prep::
	push	ix
	push	iy
	ld	(xf_page), a
	ld	b, a
	add	a, a
	add	a, a
	add	a, a
	ld	c, a			; C = page * 8
	ld	a, (_g_buf_hi)
	ld	h, a
	ld	a, c
	sub	a, h
	add	a, #0x40		; VRAM の上位 = page * 8 + 帯の番号 + 0x40(書き込み)
	ld	(xf_delta), a
	xor	a, a
	ld	(_g_xf_nch), a
	ld	(_g_xf_nrun), a
	; 写しのセグメントをページ2に(割り込みが戻せるよう g_seg2 に先に書く)
	ld	a, (_g_seg2)
	ld	(xf_seg), a
	ld	a, b
	add	a, #SEG_XFER
	ld	(_g_seg2), a
	out	(0xFE), a
	ld	a, b
	inc	a			; ページ 0 → bit0、ページ 1 → bit1
	ld	c, a
	ld	a, (_g_xfer_ok)
	and	a, c
	jp	z, xp_new
	; ---- 比べる(IX = 変わった組の番号の書き先)
	ld	ix, #XF_LIST
	ld	a, (_pcm_mode)
	or	a, a
	call	nz, xp_arm		; 声を鳴らしている間は、比べる途中で割込みを受ける(pcm.s の走査線割込みが遅れないように)
	ld	(xp_sp), sp
	ld	a, (_g_buf_hi)
	ld	h, a
	ld	l, #0
	di
	ld	sp, hl
	or	a, a
	jp	XF_PROG

; 比べるコードの、8 組ごとの組の頭(23 か所)の 3 バイトを「jp xp_brk」に書き換え、元の 3 バイトを xp_tab に覚える。
;   比べる間は割込みを止めて SP を描画バッファに向けるので、そのままでは割込みを受けられない(割込みが戻り番地をバッファに積む)。
;   声を鳴らしている間、比べるのに約 2.2ms 割込みを止めると、その間のサンプルが出せなかった
xp_arm:
	ld	hl, #xp_tab
	ld	(xp_bp), hl
	ld	de, #XF_PROG + XP_EVERY * XF_CH
	ld	b, #XP_NBRK
1$:
	ld	(hl), e
	inc	hl
	ld	(hl), d
	inc	hl
	ld	a, (de)
	ld	(hl), a
	inc	hl
	ld	a, #0xC3		; jp xp_brk
	ld	(de), a
	inc	de
	ld	a, (de)
	ld	(hl), a
	inc	hl
	ld	a, #<xp_brk
	ld	(de), a
	inc	de
	ld	a, (de)
	ld	(hl), a
	inc	hl
	ld	a, #>xp_brk
	ld	(de), a
	push	hl
	ld	hl, #XP_EVERY * XF_CH - 2
	add	hl, de
	ex	de, hl			; 次の区切りの組の頭
	pop	hl
	djnz	1$
	ret

; 区切り(比べるコードの組の頭から飛んでくる。割込みは止まっていて、SP はその組の頭を指す。生きているのは IX だけ)。
;   ふつうのスタックに戻して一瞬だけ割込みを受け、書き換えた 3 バイトを戻して、その組の頭から続ける。
;   スタックを使わない(SP は描画バッファの中)
xp_brk:
	ld	(xp_bsp), sp
	ld	sp, (xp_sp)
	ei
	nop				; ここで割込みを受ける(EI の次の命令のあと)
	di
	ld	hl, (xp_bp)
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	inc	hl			; DE = その組の頭
	ld	a, (hl)
	ld	(de), a
	inc	hl
	inc	de
	ld	a, (hl)
	ld	(de), a
	inc	hl
	inc	de
	ld	a, (hl)
	ld	(de), a
	inc	hl
	ld	(xp_bp), hl
	dec	de
	dec	de
	ex	de, hl
	ld	sp, (xp_bsp)
	or	a, a
	jp	(hl)

; 比べ終わった(比べるコードの最後の jp): 変わった組の番号の並びから区間を作り、転送のコードを書く
;   HL = コードの書き先、DE = 番号の並び、B = 残りの数、C = いまの区間の最後の組
xp_fin:
	ld	sp, (xp_sp)
	ei
	push	ix
	pop	hl
	ld	de, #XF_LIST
	or	a, a
	sbc	hl, de
	ld	b, l			; B = 変わった組の数
	ld	hl, #XF_CODE
	ld	a, b
	or	a, a
	jp	z, xp_end
	ld	a, (de)
	inc	de
pp_open:
	ld	c, a
	ld	(pp_s), a
	call	pp_head
	jr	c, pp_rest
pp_next:
	dec	b
	jr	z, pp_last
	ld	a, (de)
	inc	de
	sub	a, c
	dec	a
	jr	nz, pp_brk		; 前の組の続きではない
	inc	c
	jr	pp_next
pp_brk:
	call	pp_tail
	dec	de
	ld	a, (de)
	inc	de
	jr	pp_open
pp_rest:
	ld	c, #XF_NCH - 1		; 区間の数が上限: この組から最後の組までを 1 つの区間で送る(写しはもう新しい)
pp_last:
	call	pp_tail
	jp	xp_end

	; ---- 写しが無いとき: 比べるコードと後始末を描画バッファの中身で作り直し、全部を 1 つの区間で送る
xp_new:
	; 比べるコード: HL = 書き先、DE = 描画バッファ、BC = 組 k の後始末の番地、IXL = 残りの組
	ld	hl, #XF_PROG
	ld	a, (_g_buf_hi)
	ld	d, a
	ld	e, #0
	ld	bc, #XF_STUB
	ld	ixl, #XF_NCH
1$:
	.rept	4
	ld	(hl), #0xE1		; pop hl
	inc	hl
	ld	(hl), #0x11		; ld de, #写し
	inc	hl
	ld	a, (de)
	ld	(hl), a
	inc	de
	inc	hl
	ld	a, (de)
	ld	(hl), a
	inc	de
	inc	hl
	ld	(hl), #0xED		; sbc hl, de
	inc	hl
	ld	(hl), #0x52
	inc	hl
	ld	(hl), #0xC2		; jp nz, 後始末
	inc	hl
	ld	(hl), c
	inc	hl
	ld	(hl), b
	inc	hl
	.endm
	ld	a, c
	add	a, #XF_ST
	ld	c, a
	ld	a, b
	adc	a, #0
	ld	b, a
	dec	ixl
	jp	nz, 1$
	ld	(hl), #0xC3		; jp xp_fin
	inc	hl
	ld	(hl), #<xp_fin
	inc	hl
	ld	(hl), #>xp_fin
	; 後始末: HL = 書き先、DE = 組 k の最初の即値の番地、BC = 組 k の頭(描画バッファ)、IXL = k
	ld	hl, #XF_STUB
	ld	de, #XF_PROG + 2
	ld	a, (_g_buf_hi)
	ld	b, a
	ld	c, #0
	ld	ixl, #0
2$:
	ld	(hl), #0x31		; ld sp, #組の頭
	inc	hl
	ld	(hl), c
	inc	hl
	ld	(hl), b
	inc	hl
	.rept	4
	ld	(hl), #0xD1		; pop de
	inc	hl
	ld	(hl), #0xED		; ld (即値), de
	inc	hl
	ld	(hl), #0x53
	inc	hl
	ld	(hl), e
	inc	hl
	ld	(hl), d
	inc	hl
	ld	a, e
	add	a, #9			; 次の即値
	ld	e, a
	ld	a, d
	adc	a, #0
	ld	d, a
	.endm
	ld	(hl), #0x3E		; ld a, #k
	inc	hl
	ld	a, ixl
	ld	(hl), a
	inc	hl
	ld	(hl), #0xDD		; ld 0 (ix), a
	inc	hl
	ld	(hl), #0x77
	inc	hl
	ld	(hl), #0x00
	inc	hl
	ld	(hl), #0xDD		; inc ix
	inc	hl
	ld	(hl), #0x23
	inc	hl
	ld	(hl), #0xB7		; or a(sbc の前にキャリーを消す)
	inc	hl
	ld	(hl), #0xC3		; jp 組 k + 1(= DE - 2)
	inc	hl
	ld	a, e
	sub	a, #2
	ld	(hl), a
	inc	hl
	ld	a, d
	sbc	a, #0
	ld	(hl), a
	inc	hl
	ld	a, c
	add	a, #8
	ld	c, a
	ld	a, b
	adc	a, #0
	ld	b, a
	inc	ixl
	ld	a, ixl
	cp	a, #XF_NCH
	jp	nz, 2$
	; 全部を 1 つの区間で送る
	ld	hl, #XF_CODE
	xor	a, a
	ld	(pp_s), a
	call	pp_head
	ld	c, #XF_NCH - 1
	call	pp_tail
	ld	a, (xf_page)
	inc	a
	ld	b, a
	ld	a, (_g_xfer_ok)
	or	a, b
	ld	(_g_xfer_ok), a
xp_end:
	ld	(hl), #0xC9		; ret
	ld	a, (xf_seg)
	ld	(_g_seg2), a
	out	(0xFE), a
	pop	iy
	pop	ix
	ret

; pp_head: A = 区間の最初の組 → (HL) に「di / 書き込み先 / ei / ld hl, 描画バッファの番地」の 13 バイトを書く。BC・DE は保つ。
;   区間の数が上限に達したら(この区間で最後)、キャリーを立てて戻る
pp_head:
	push	de
	ld	d, a
	add	a, a
	add	a, a
	add	a, a
	ld	e, a			; E = 番地の下位(列ペア * 8)
	ld	a, d
	rlca
	rlca
	rlca
	and	a, #0x07		; 帯
	ld	d, a
	ld	a, (_g_buf_hi)
	add	a, d
	ld	d, a			; DE = 描画バッファの番地
	ld	(hl), #0xF3		; di
	inc	hl
	ld	(hl), #0x3E		; ld a, 下位
	inc	hl
	ld	(hl), e
	inc	hl
	ld	(hl), #0xD3		; out (0x99), a
	inc	hl
	ld	(hl), #0x99
	inc	hl
	ld	(hl), #0x3E		; ld a, 上位
	inc	hl
	ld	a, (xf_delta)
	add	a, d
	ld	(hl), a
	inc	hl
	ld	(hl), #0xD3		; out (0x99), a
	inc	hl
	ld	(hl), #0x99
	inc	hl
	ld	(hl), #0xFB		; ei
	inc	hl
	ld	(hl), #0x21		; ld hl, 描画バッファの番地
	inc	hl
	ld	(hl), e
	inc	hl
	ld	(hl), d
	inc	hl
	pop	de
	ld	a, (_g_xf_nrun)
	inc	a
	ld	(_g_xf_nrun), a
	cp	a, #XF_MAXRUN		; キャリー = まだ上限ではない
	ccf
	ret

; pp_tail: 組 pp_s〜C の区間を閉じる: (HL) に OUTI の列(sys.s の z80x_out256)への call を書く。256 バイトを超える分は
;   256 バイトずつ。BC・DE は保つ
pp_tail:
	push	de
	ld	a, (pp_s)
	neg
	add	a, c
	inc	a			; A = 組の数(1..192)
	ld	e, a
	ld	a, (_g_xf_nch)
	add	a, e
	ld	(_g_xf_nch), a
	ld	a, e
1$:
	cp	a, #33
	jr	c, 2$
	ld	(hl), #0xCD		; call z80x_out256(256 バイト)
	inc	hl
	ld	(hl), #<z80x_out256
	inc	hl
	ld	(hl), #>z80x_out256
	inc	hl
	sub	a, #32
	jr	1$
2$:
	ld	e, a
	ld	d, #0
	.rept	4
	sla	e
	rl	d
	.endm				; DE = 組 * 16 = バイト数 * 2(OUTI 1 つ 2 バイト)
	ld	a, #<z80x_out_end
	sub	a, e
	ld	e, a
	ld	a, #>z80x_out_end
	sbc	a, d
	ld	d, a
	ld	(hl), #0xCD		; call z80x_out_end - バイト数 * 2
	inc	hl
	ld	(hl), e
	inc	hl
	ld	(hl), d
	inc	hl
	pop	de
	ret

_vdp_xfer::
	call	_xfer_prep
	ld	a, (_g_xf_nch)
	or	a, a
	ret	z			; 何も変わっていない
	ld	a, (_g_z80x)
	or	a, a
	jr	z, 1$
	; 声を鳴らしている間(pcm.s)は CPU を切り替えない。切り替え(BIOS の CHGCPU)の間は走査線割込みを止めるしかなく、
	; 張った行が過ぎると割込みが来ないまま消えて、声が止まった。R800 のまま送ると 1 フレーム 1ms ほど遅いが、声の間だけ
	ld	a, (_pcm_mode)
	or	a, a
	jr	nz, 1$
	; 送る量で決める。1 バイトあたり Z80 は 3.22µs 速く、切り替えの往復は 922µs なので、見積もりでは 36 組が境目。
	; 実際に測ると(FS-A1ST、ステージ 1・3。docs/検証記録.md)、0 / 20 / 36 / 50 組の順に 44.25 / 44.22 / 44.28 / 44.37ms と
	; 64.91 / 64.91 / 64.99 / 65.03ms、R800 だけで送ると 44.76 / 66.14ms だったので、20 組にした
	ld	a, (_g_xf_nch)
	cp	a, #XF_Z80_MIN
	jp	nc, _xfer_z80
1$:
	; R800 のまま、書き出したコードで送る
	di
	xor	a, a
	out	(0x99), a
	ld	a, #0x8E
	out	(0x99), a		; R#14 = 0
	ei
	ld	c, #0x98
	jp	XF_CODE
