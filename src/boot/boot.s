; boot.s — ROM 起動処理とローダー(ASCII8 メガロムの bank0、0x4000-0x5FFF)
;
; ROM の中身(tools/mkrom.py が組み立てる。8KB バンク):
;   bank0      このローダー
;   bank1      ページ0に置く表とコード(エンジンが起動後に読み込む)
;   bank2-3    エンジンの常駐部 0x4000-0x7FFF(→ RAM ページ1。ここで写す)
;   bank4-5    地形モジュール、bank6〜15 ステージごとのゲームモジュール(エンジンがページ2へ読み込む)
;
; 流れ(仕様書 7.3「ローダーの流れ」):
;   1. 自分(カートリッジ)のスロットと、RAM(ページ3 と同じスロット)のスロットを求める。
;   2. R800 DRAM モードへ。実ファームの turbo R(MSXVER>=3)は CHGCPU(0180h)。
;      C-BIOS の混成機(MSXVER<3 かつ S1990 あり)だけ S1990 を直接叩く
;      (実機ファームで直書きすると暴走する。TurboRVoxelShooter の知見)。
;      S1990 が無ければ「turbo R required」を出して停止。
;   3. コピー処理をページ3(BLOB)へ移して実行する。
;   4. ASCII8 のバンクレジスタ(6000h-7FFFh)はページ1にしか無いので、
;      「ページ1=カートリッジでバンクを選ぶ → 反対側のページを RAM にしてコピー」を2回行う。
;   5. ページ1・2を RAM にして 0x4000 のエンジンへ飛ぶ。ページ0は BIOS のまま。
;
; スロット切替は BIOS の ENASLT を使う(ページ1/2 専用で安全。ENASLT は DI で戻る)。

	.module	boot

RSLREG	= 0x0138
ENASLT	= 0x0024
CHPUT	= 0x00A2
INITXT	= 0x006C
EXPTBL	= 0xFCC1
MSXVER	= 0x002D
CHGCPU	= 0x0180

BLOB	= 0xE800		; コピー処理の実行場所(ページ3 RAM。エンジンの常駐部 0xE000 とは重ねない)
; スロット ID(F000SSPP)。エンジン側 src/engine/sys.h の SLOT_CART / SLOT_RAM と一致させる
V_CART	= 0xE0F0
V_RAM	= 0xE0F1

	.area	_CODE
	.area	_DATA

	.area	_HEADER (ABS)
	.org	0x4000
	.db	0x41, 0x42		; "AB"
	.dw	init
	.dw	0, 0, 0
	.db	0, 0, 0, 0, 0, 0

init:
	di
	call	slotid_page1
	ld	(V_CART), a
	call	slotid_page3
	ld	(V_RAM), a

	; --- R800 DRAM モードへ
	ld	a, (MSXVER)
	cp	a, #3
	jr	c, no_fw
	ld	a, #0x82		; bit7=ターボ LED、bit0-1=2(R800 DRAM)
	call	CHGCPU
	di
	jr	cpu_done
no_fw:
	ld	a, #6
	out	(0xE4), a
	in	a, (0xE5)
	cp	a, #0xFF
	jr	z, not_turbo_r		; S1990 が無い = turbo R ではない
	xor	a, a			; S1990 reg6: bit5=0(R800) bit6=0(DRAM)
	out	(0xE5), a
cpu_done:
	ld	hl, #blob
	ld	de, #BLOB
	ld	bc, #blob_end - blob
	ldir
	jp	BLOB

not_turbo_r:
	call	INITXT
	ld	hl, #msg
1$:
	ld	a, (hl)
	or	a, a
	jr	z, 2$
	call	CHPUT
	inc	hl
	jr	1$
2$:
	di
	halt
msg:
	.ascii	"TROLLEY HOME RUN"
	.db	13, 10
	.ascii	"MSX turbo R required."
	.db	0

; ページ1/ページ3 のスロット ID(F000SSPP)。RSLREG と EXPTBL/SLTTBL から求める
slotid_page1:
	call	RSLREG
	rrca
	rrca
	and	a, #0x03
	ld	c, a
	ld	b, #0
	ld	hl, #EXPTBL
	add	hl, bc
	ld	c, a
	ld	a, (hl)
	and	a, #0x80
	or	a, c
	ld	c, a
	ld	de, #4
	add	hl, de			; SLTTBL
	ld	a, (hl)
	and	a, #0x0C
	or	a, c
	ret

slotid_page3:
	call	RSLREG
	rlca
	rlca
	and	a, #0x03
	ld	c, a
	ld	b, #0
	ld	hl, #EXPTBL
	add	hl, bc
	ld	c, a
	ld	a, (hl)
	and	a, #0x80
	or	a, c
	ld	c, a
	ld	de, #4
	add	hl, de
	ld	a, (hl)
	and	a, #0xC0
	rrca
	rrca
	rrca
	rrca				; bit6-7 → bit2-3
	or	a, c
	ret

; ===========================================================================
; コピー処理(BLOB で実行。位置独立: 相対ジャンプと固定番地だけ使う)
; ===========================================================================
blob:
	; --- ページ2 = カートリッジ。ページ1 もまだカートリッジなのでバンクレジスタに届く
	ld	a, (V_CART)
	ld	h, #0x80
	call	ENASLT
	ld	a, #2
	ld	(0x7000), a		; 0x8000-0x9FFF = bank2
	ld	a, #3
	ld	(0x7800), a		; 0xA000-0xBFFF = bank3
	; --- ページ1 = RAM にして bank2-3 → 0x4000
	ld	a, (V_RAM)
	ld	h, #0x40
	call	ENASLT
	ld	hl, #0x8000
	ld	de, #0x4000
	ld	bc, #0x4000
	ldir
	; --- ページ2 = RAM に戻す(ページ2 に置くモジュールは、エンジンが ROM から読み込む)
	ld	a, (V_RAM)
	ld	h, #0x80
	call	ENASLT
	; --- ページ1 = RAM。エンジン先頭の印("TH")を確かめて飛ぶ
	ld	a, (V_RAM)
	ld	h, #0x40
	call	ENASLT
	ld	a, (0x4003)
	cp	a, #0x54		; 'T'
	jr	nz, b_fail
	ld	a, (0x4004)
	cp	a, #0x48		; 'H'
	jr	nz, b_fail
	ld	sp, #0xF380
	jp	0x4000

b_fail:
	ld	a, #0x08		; 周辺色 = 8 で停止(コピー失敗)
	out	(0x99), a
	ld	a, #0x87
	out	(0x99), a
	di
	halt
blob_end:
