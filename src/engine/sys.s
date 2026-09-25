; sys.s — 割り込み(H.TIMI)・入力・PSG・タイマ
;
; ■ 割り込み(仕様書 7.6)
;   BIOS の割り込み処理(ページ0)が垂直帰線ごとに H.TIMI(FD9Fh)を呼ぶ。そこから htimi へ飛ぶ。
;   BIOS がレジスタを全部退避してから呼ぶので、ここでは自由に壊してよい。
;   htimi がすること:
;     ・g_tick(60Hz のフレームカウンタ)を +1
;     ・表示ページ切替の予約(g_flip_req)があり、前の切替から 2 ティック経っていれば
;       R#4 を書き換え、スプライトアトリビュートを転送(30fps が上限)
;     ・サウンドドライバの 1 ティック(sound_tick)
;
; ■ VDP の取り決め(ISR と本体の干渉を避ける)
;   本体はストリーム転送(vdp.s)を「切替の予約が消えてから」始め、送り切ってから予約を立てる。
;   つまり ISR が VRAM アドレスを設定するのは、本体が VDP を触っていない間だけ。
;   本体の 99h への 2 バイト書き込み(アドレス・レジスタ設定)は DI で囲む。
;
; ■ 入力(仕様書 2 章)
;   キーボード(PPI 直読み)とジョイスティック ポート1(PSG R#14/R#15)を毎フレーム読む。
;   BIOS ISR もキーボードを走査するので、読んでいる間は DI。

	.module	sys

	.globl	_sound_tick
	.globl	_g_spr_attr

H_TIMI	= 0xFD9F
SPR_BYTES = 17			; スプライト 4 枚分 + 終端(Y=208)

	.area	_DATA
_g_tick::	.ds	2	; 60Hz ごとに +1
_g_flip_req::	.ds	1	; 1 = 次の垂直帰線で g_flip_page を表示する
_g_flip_page::	.ds	1	; R#4 に書く値(0 または 1)
flip_tick:	.ds	1	; 最後に切り替えたときの g_tick(下位)

	.area	_CODE

; ---------------------------------------------------------------------------
; void sys_install_isr(void)
; ---------------------------------------------------------------------------
_sys_install_isr::
	di
	ld	a, #0xC3		; JP htimi
	ld	(H_TIMI), a
	ld	hl, #htimi
	ld	(H_TIMI + 1), hl
	ld	a, #0xC9
	ld	(H_TIMI + 3), a
	ld	(H_TIMI + 4), a
	ei
	ret

htimi:
	ld	hl, (_g_tick)
	inc	hl
	ld	(_g_tick), hl
	ld	a, (_g_flip_req)
	or	a, a
	jr	z, 1$
	ld	a, (_g_tick)		; 前の切替から 2 ティック以上(30fps が上限)
	ld	hl, #flip_tick
	sub	a, (hl)
	cp	a, #2
	jr	c, 1$
	ld	a, (_g_tick)
	ld	(hl), a
	ld	a, (_g_flip_page)
	out	(0x99), a
	ld	a, #0x84		; R#4 = パターンジェネレータ
	out	(0x99), a
	xor	a, a
	out	(0x99), a
	ld	a, #0x8E		; R#14 = 0
	out	(0x99), a
	xor	a, a
	out	(0x99), a		; VRAM 0x1400(スプライトアトリビュート)へ書き込み
	ld	a, #0x54
	out	(0x99), a
	ld	hl, #_g_spr_attr
	ld	bc, #(SPR_BYTES << 8) | 0x98
	otir
	xor	a, a
	ld	(_g_flip_req), a
1$:
	jp	_sound_tick

; ---------------------------------------------------------------------------
; void wait_tick(void) — g_tick が変わるまで待つ
; ---------------------------------------------------------------------------
_wait_tick::
	ld	hl, #_g_tick
	ld	a, (hl)
1$:
	ei
	halt
	cp	a, (hl)
	jr	z, 1$
	ret

; ---------------------------------------------------------------------------
; u8 input_raw(void)
;   bit0=上 bit1=下 bit2=左 bit3=右(照準) bit4=A(射撃) bit5=B(前後切替)。1 = 押下
;   キーボード: カーソルキー / スペース=A / M・B=B(B ボタン相当キーは仕様書 2 章【要確認】)
;   ジョイスティック ポート1: 方向 / トリガ A / トリガ B
; ---------------------------------------------------------------------------
_input_raw::
	di
	in	a, (0xAA)
	and	a, #0xF0
	or	a, #8
	out	(0xAA), a
	in	a, (0xA9)
	cpl
	ld	e, a			; 行8: bit0=SPACE 4=左 5=上 6=下 7=右
	in	a, (0xAA)
	and	a, #0xF0
	or	a, #4
	out	(0xAA), a
	in	a, (0xA9)
	cpl
	ld	d, a			; 行4: bit2=M
	in	a, (0xAA)
	and	a, #0xF0
	or	a, #2
	out	(0xAA), a
	in	a, (0xA9)
	cpl
	ld	h, a			; 行2: bit7=B
	ld	a, #15
	out	(0xA0), a
	in	a, (0xA2)
	and	a, #0xAF		; ポート1 選択、ピン8 = L
	or	a, #0x03
	out	(0xA1), a
	ld	a, #14
	out	(0xA0), a
	in	a, (0xA2)
	ei
	cpl
	and	a, #0x3F		; ジョイスティックの並びがそのまま戻り値の並び
	bit	5, e
	jr	z, 1$
	or	a, #0x01
1$:
	bit	6, e
	jr	z, 2$
	or	a, #0x02
2$:
	bit	4, e
	jr	z, 3$
	or	a, #0x04
3$:
	bit	7, e
	jr	z, 4$
	or	a, #0x08
4$:
	bit	0, e
	jr	z, 5$
	or	a, #0x10
5$:
	bit	2, d
	jr	z, 6$
	or	a, #0x20
6$:
	bit	7, h
	jr	z, 7$
	or	a, #0x20
7$:
	ret

; ---------------------------------------------------------------------------
; void psg_write(u8 reg, u8 val)   A=reg, L=val
; ---------------------------------------------------------------------------
_psg_write::
	out	(0xA0), a
	ld	a, l
	out	(0xA1), a
	ret

; ---------------------------------------------------------------------------
; turbo R システムタイマ(E6h/E7h、約 3.911µs/カウント)。処理時間の計測用
; ---------------------------------------------------------------------------
_timer_reset::
	out	(0xE6), a
	ret

; u16 timer_read(void)
_timer_read::
	in	a, (0xE6)
	ld	e, a
	in	a, (0xE7)
	ld	d, a
	in	a, (0xE6)
	cp	a, e
	ret	nc
	ld	e, a
	in	a, (0xE7)
	ld	d, a
	ret
