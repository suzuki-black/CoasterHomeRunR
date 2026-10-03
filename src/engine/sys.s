; sys.s — 割り込み(H.TIMI)・入力・PSG・タイマ
;
; ■ 割り込み(仕様書 7.6)
;   起動直後は BIOS の割り込み処理(ページ0)が垂直帰線ごとに H.TIMI(FD9Fh)を呼び、そこから htimi へ飛ぶ。
;   sys_page0_ram の後はページ0が自前の RAM になり、0x0038 の isr0 が S#0 を読んで htimi を呼ぶ。
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
	.globl	_g_seg2

; ページ2(0x8000-0xBFFF)のマッパーのセグメント(仕様書 7.3 のオーバーレイ)
SEG_GAME = 1			; ゲームモジュール(ステージごとに読み込む。BGM のデータもここ)
SEG_ENV	= 5			; 地形モジュール(起動時に 1 回読み込む)
MOD_ADDR = 0x8000		; モジュールの先頭(0x8000: 入口の jp、0x8003: ゲームは Stage へのポインタ / 地形は 2 つ目の jp)
MOD_BSS	= 0xBC00		; モジュールの変数(読み込んだら 0 で埋める)。〜0xBFFF
ENVM_BUILD = MOD_ADDR		; 地形モジュールの入口(jp envm_build / jp envm_draw)
ENVM_DRAW = MOD_ADDR + 3

H_TIMI	= 0xFD9F
; ページ3 の固定番地(常駐ローダーからも使う)
V_CART	= 0xE0F0		; カートリッジのスロット ID(ローダーが書く)
V_RAM	= 0xE0F1		; RAM のスロット ID(F000SSPP)
P0_BIOS	= 0xE0F2		; 起動時のページ0の基本スロット(BIOS)
P0_SEG	= 4			; ページ0に使うマッパーのセグメント(0-3 は起動時のページ0-3)

; ページ0を自前の RAM(V_RAM のスロット、セグメント P0_SEG)にする。
;   RAM は拡張スロットでもページ3と同じ基本スロットなので、拡張スロットのレジスタ(FFFF)はそのまま書ける
	.macro	P0_TO_RAM ?skip
	in	a, (0xA8)
	and	a, #0xFC
	ld	b, a
	ld	a, (V_RAM)
	and	a, #0x03
	or	a, b
	out	(0xA8), a
	ld	a, (V_RAM)
	bit	7, a
	jr	z, skip
	rrca
	rrca
	and	a, #0x03
	ld	b, a
	ld	a, (0xFFFF)
	cpl
	and	a, #0xFC
	or	a, b
	ld	(0xFFFF), a
skip:
	.endm

; ページ0を BIOS に戻す。基本スロットのレジスタだけ戻せばよい
;   (BIOS 側の拡張スロットのレジスタは触っていない。実機の BIOS はスロット 0-0)
	.macro	P0_TO_BIOS
	in	a, (0xA8)
	and	a, #0xFC
	ld	b, a
	ld	a, (P0_BIOS)
	or	a, b
	out	(0xA8), a
	.endm
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
	; サウンドはゲームモジュールの BGM を読むので、ページ2をゲームのセグメントにしてから呼び、元に戻す
	ld	a, #SEG_GAME
	out	(0xFE), a
	call	_sound_tick
	ld	a, (_g_seg2)
	out	(0xFE), a
	ret

; ---------------------------------------------------------------------------
; void sys_seg2(u8 seg) — ページ2のセグメントを切り替える(割り込みが戻せるよう g_seg2 に先に書く)
; ---------------------------------------------------------------------------
_sys_seg2::
	ld	(_g_seg2), a
	out	(0xFE), a
	ret

; ---------------------------------------------------------------------------
; void env_build(void) / void env_draw(u16 s, u16 look) — 地形モジュールを呼ぶ(ページ2を地形のセグメントにして呼び、戻す)
;   引数はレジスタ(HL, DE)だけで渡すこと。スタックで渡す引数があると、ここで積む戻り番地の分ずれる
; ---------------------------------------------------------------------------
_env_build::
	ld	a, #SEG_ENV
	ld	(_g_seg2), a
	out	(0xFE), a
	call	ENVM_BUILD
	jr	env_back
_env_draw::
	push	af
	ld	a, #SEG_ENV
	ld	(_g_seg2), a
	out	(0xFE), a
	pop	af
	call	ENVM_DRAW
env_back::
	ld	a, #SEG_GAME
	ld	(_g_seg2), a
	out	(0xFE), a
	ret

; ---------------------------------------------------------------------------
; void sys_page0_ram(void) — ページ0(0x0000-0x3FFF)を自前の RAM にする(起動時に 1 回)
;   ゲーム中は BIOS を呼ばない(入力・PSG・VDP はポート直接)ので、ページ0も使える 16KB にする。
;   0x0038 に自前の割り込み処理(isr0)、0x0100 に塗りの展開ブロック(raster.s)を置く。
;   BIOS が要るのはステージの読み込み(ENASLT)だけで、そのときは常駐ローダーがページ0を BIOS に戻す。
; ---------------------------------------------------------------------------
	.globl	_fillblk_tpl, _fillblk_end
_sys_page0_ram::
	di
	in	a, (0xA8)
	and	a, #0x03
	ld	(P0_BIOS), a
	ld	a, #P0_SEG
	out	(0xFC), a		; ページ0のセグメント(RAM スロットに切り替えたときに効く)
	P0_TO_RAM
	ld	a, #0xC3		; 0x0038: JP isr0
	ld	(0x0038), a
	ld	hl, #isr0
	ld	(0x0039), hl
	ld	hl, #_fillblk_end
	ld	de, #_fillblk_tpl
	or	a, a
	sbc	hl, de
	ld	b, h
	ld	c, l
	ex	de, hl
	ld	de, #0x0100
	ldir
	ei
	ret

; 自前の割り込み処理(ページ0の 0x0038 から)。S#0 を読んで垂直帰線なら htimi
;   裏レジスタは使わない(描画側が exx を使うため)
isr0:
	push	af
	in	a, (0x99)
	rlca
	jr	nc, 9$
	push	bc
	push	de
	push	hl
	push	ix
	push	iy
	call	htimi
	pop	iy
	pop	ix
	pop	hl
	pop	de
	pop	bc
9$:
	pop	af
	ei
	ret

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
	ld	l, a
	ld	a, h
	and	a, #0xC0		; 行2: bit6 = A キー、bit7 = B キー → 戻り値の bit6・bit7(隠しコマンド用)
	or	a, l
	ret

; ---------------------------------------------------------------------------
; u8 sys_cpu_status(void) — S1990 のレジスタ 6(bit5: 0 = R800 / bit6: 0 = DRAM モード)。確認表示用
; ---------------------------------------------------------------------------
_sys_cpu_status::
	di
	ld	a, #6
	out	(0xE4), a
	in	a, (0xE5)
	ei
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

; ---------------------------------------------------------------------------
; ROM バンクからの読み込み(仕様書 7.3「ステージ切替時は、常駐ローダーが一時的にページ1を ROM に戻し、
; 該当バンクのデータを RAM へコピーする」)
;
;   ASCII8 のバンクレジスタ(6000h-7FFFh)はページ1にしか無く、読み出す窓もページ1にある。
;   ページ1(エンジンのコード)を一時的にカートリッジへ切り替えるので、コピー処理はページ3の
;   常駐領域(RL_ADDR)に置いて実行する。切り替えている間は割り込みを止める
;   (割り込みの飛び先がページ0・1にあるため)。スロット切替は BIOS の ENASLT(ページ1/2 用)なので、
;   その間はページ0も BIOS に戻す。読み出し先はページ2・3(ページ0・1 は切り替え中なので不可)。
;   常駐ローダーは位置に依存しない書き方にする(内部の call や絶対ジャンプを使わない)。
; ---------------------------------------------------------------------------
ENASLT	= 0x0024
RL_ADDR	= 0xE000		; 常駐ローダー(0xE0F0 からはスロット ID など)
RL_LEN	= 0xE0F4		; 常駐ローダーが写す大きさ(rom_load が書く)
P0D_BANK = 1			; ページ0に置く表(エリア _PAGE0)の ROM バンク
P0D_TMP	= 0xB400		; その一時置き場(0xB400-0xCBFF。起動時はモジュールを読み込む前のページ2と描画バッファなので空き)

; void sys_install_loader(void) — 常駐ローダーをページ3へ写す(起動時に 1 回)
_sys_install_loader::
	ld	hl, #rl_blob
	ld	de, #RL_ADDR
	ld	bc, #rl_end - rl_blob
	ldir
	ret

; void mod_load(u8 bank)   A = 最初のバンク。bank と bank+1(16KB)を、いまページ2に出ているセグメントの 0x8000- へ写し、
;   モジュールの変数(0xBC00-0xBFFF)を 0 で埋める
_mod_load::
	ld	hl, #0x4000
	ld	(RL_LEN), hl
	ld	de, #MOD_ADDR
	call	RL_ADDR
	ld	hl, #MOD_BSS
	ld	de, #MOD_BSS + 1
	ld	bc, #0x03FF
	ld	(hl), #0
	ldir
	ret

; void bios_font(u8 *dst) — BIOS のフォント(CGTABL の指す 2KB。ひらがなを含む)を dst(ページ2・3)へ写す。
;   エンディングの SCREEN 1 で使う。写す間だけページ0を BIOS に戻す
_bios_font::
	ex	de, hl
	di
	P0_TO_BIOS
	ld	hl, (0x0004)		; CGTABL
	ld	bc, #2048
	ldir
	P0_TO_RAM
	ei
	ret

; void sys_page0_data(void) — ページ0の表(sin 表・逆数表・フォント・敵の絵など、エリア _PAGE0)を
;   ROM のバンク P0D_BANK から読み込む。sys_page0_ram の後、表を使う前に 1 回。
;   ページ0へは直接写せない(読み込み中はページ0が BIOS)ので、ページ2の空きを経由する
	.globl	s__PAGE0, l__PAGE0
_sys_page0_data::
	ld	hl, #l__PAGE0
	ld	(RL_LEN), hl
	ld	a, #P0D_BANK
	ld	de, #P0D_TMP
	call	RL_ADDR
	ld	hl, #P0D_TMP
	ld	de, #s__PAGE0
	ld	bc, #l__PAGE0
	ldir
	ret

; 常駐ローダー本体(RL_ADDR で動く。位置に依存しない: 相対・内部ジャンプなし、固定番地だけ使う)
rl_blob:
	di
	push	de
	push	af
	P0_TO_BIOS			; ENASLT のためにページ0を BIOS に戻す
	ld	a, (V_CART)
	ld	h, #0x40
	call	ENASLT			; ページ1 = カートリッジ
	pop	af
	ld	(0x6000), a		; 0x4000-0x5FFF = bank
	inc	a
	ld	(0x6800), a		; 0x6000-0x7FFF = bank + 1
	pop	de
	ld	hl, #0x4000
	ld	bc, (RL_LEN)
	ldir
	ld	a, (V_RAM)
	ld	h, #0x40
	call	ENASLT			; ページ1 = RAM(エンジン)に戻す
	P0_TO_RAM			; ページ0 = 自前の RAM に戻す
	ei
	ret
rl_end:

; ---------------------------------------------------------------------------
; Z80 転送(vdp.s の vdp_xfer から)。A = ページ(0/1)
;   S1990 は R800 から VDP への書き込みに 1 バイト 8.67µs の待ちを入れるが、Z80 には入れない(5.45µs。
;   実機の FS-A1ST と openMSX で同じ。docs/検証記録.md)。描画バッファは VRAM と同じ並びなので、
;   CPU を Z80 に切り替えて OUTI で頭から送る。切り替えは BIOS の CHGCPU(往復 約 0.9ms)。
;   ・CHGCPU を呼ぶ間はページ0を BIOS に戻す。CHGCPU は戻るときに割り込みを許可するので、その間は VDP の割り込み
;     (R#1 の IE0)を止めておく。止めている間に帰線が始まっても S#0 の F は立つので、IE0 を戻すとすぐ割り込みが入る
;   ・転送中はページ0を自前の RAM に戻して割り込みを許可する(割り込み処理と音は Z80 でも動く。R800 専用の命令なし)
;   ・CHGCPU に渡す値の bit7 は立てない(立てるとターボ LED が毎フレーム点滅する)
; ---------------------------------------------------------------------------
CHGCPU	= 0x0180
MSXVER	= 0x002D

	.area	_DATA
_g_z80x::	.ds	1	; 1 = Z80 で転送する(隠しコンフィグの XFER で R800 に固定できる)
_g_z80x_ok::	.ds	1	; 1 = ファームウェアに CHGCPU がある(sys_z80x_init が決める)
	.area	_CODE

	.globl	_g_vdp_r1, _g_buf_hi, _g_xfer_skip, _xfer_skip_addr

; void sys_z80x_init(void) — ファームウェアに CHGCPU があるか(MSXVER >= 3)で g_z80x_ok と g_z80x を決める。起動時に 1 回
_sys_z80x_init::
	di
	P0_TO_BIOS
	ld	a, (MSXVER)
	ld	c, a
	P0_TO_RAM
	ei
	xor	a, a
	ld	b, a
	ld	a, c
	cp	a, #3
	ccf
	rl	b			; MSXVER >= 3 なら 1
	ld	a, b
	ld	(_g_z80x_ok), a
	ld	(_g_z80x), a
	ret

; IE0 を止める / 戻す(割り込みを止めた状態で呼ぶ)
z80x_ie_off:
	ld	a, (_g_vdp_r1)
	and	a, #0xDF
	jr	z80x_r1
z80x_ie_on:
	ld	a, (_g_vdp_r1)
z80x_r1:
	out	(0x99), a
	ld	a, #0x81
	out	(0x99), a
	ret

; CPU を切り替える: A = CHGCPU の値(0x00 = Z80、0x02 = R800 DRAM)。割り込みを許可して戻る
z80x_switch:
	di
	push	af
	call	z80x_ie_off
	P0_TO_BIOS
	pop	af
	call	CHGCPU
	di
	P0_TO_RAM
	call	z80x_ie_on
	ei
	ret

_xfer_z80::
	ld	(z80x_page), a
	xor	a, a
	call	z80x_switch		; → Z80
	; 書き込み先 = page * 0x800
	ld	a, (z80x_page)
	add	a, a
	add	a, a
	add	a, a
	ld	c, a
	di
	xor	a, a
	out	(0x99), a
	ld	a, #0x8E
	out	(0x99), a		; R#14 = 0
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
	push	ix
	ld	d, #5			; 256 バイト × 5
1$:
	call	z80x_out256
	dec	d
	jr	nz, 1$
	ld	ix, #z80x_out_end - 16 * 2	; 下の帯の列ペア 0-1(16 バイト)
	call	z80x_jpix
	ld	a, (_g_xfer_skip)
	or	a, a
	jr	nz, 2$
	ld	ix, #z80x_out_end - 224 * 2	; 列ペア 2-29(224 バイト)
	call	z80x_jpix
	jr	3$
2$:
	call	_xfer_skip_addr		; 前に送った中身と同じなので飛ばす(vdp.s)
	ld	c, #0x98
3$:
	ld	ix, #z80x_out_end - 16 * 2	; 列ペア 30-31
	call	z80x_jpix
	pop	ix
	ld	a, #2
	jp	z80x_switch		; → R800(DRAM モード)

z80x_jpix:
	jp	(ix)

; OUTI を 256 個並べたもの。途中へ飛び込めば n 個だけ送る(終わりの番地 - n * 2)
z80x_out256:
	.rept	256
	outi
	.endm
z80x_out_end:
	ret

	.area	_DATA
z80x_page:	.ds	1
	.area	_CODE
