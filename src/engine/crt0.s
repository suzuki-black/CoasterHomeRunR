; crt0.s — エンジン(RAM 実行部)のスタートアップ
;
; ローダー(src/boot/boot.s)がこのイメージを RAM のページ1・2(0x4000-0xBFFF)へ写し、0x4000 へ飛ぶ。
;   0x4000  JP init
;   0x4003  "TH"      (ローダーが正しいイメージか確かめる印)
; コードと定数は 0x4000-0xBFFF、変数(DATA/BSS)は 0xCC00-0xDFFF(Makefile の --data-loc)。
; tools/mkrom.py が範囲を検査する。

	.module	crt0
	.globl	_main
	.globl	l__INITIALIZER, s__INITIALIZER, s__INITIALIZED
	.globl	l__DATA, s__DATA, l__BSS, s__BSS

	.area	_HEADER (ABS)
	.org	0x4000
	jp	init
	.ascii	"TH"

	; 領域の並び順をここで確定させる(リンカは最初に現れた順に並べる)
	.area	_HOME
	.area	_CODE
	.area	_INITIALIZER
	.area	_GSINIT
	.area	_GSFINAL
	.area	_DATA
	.area	_INITIALIZED
	.area	_BSEG
	.area	_BSS
	.area	_HEAP

	.area	_CODE
init:
	di
	ld	sp, #0xF380
	; RAM の中身は保証されないので DATA/BSS を 0 にする
	ld	hl, #s__DATA
	ld	bc, #l__DATA
	call	zero_fill
	ld	hl, #s__BSS
	ld	bc, #l__BSS
	call	zero_fill
	ld	bc, #l__INITIALIZER
	ld	a, b
	or	a, c
	jr	z, 1$
	ld	de, #s__INITIALIZED
	ld	hl, #s__INITIALIZER
	ldir
1$:
	call	gsinit
	call	_main
2$:
	di
	halt
	jr	2$

; HL から BC バイトを 0 にする(BC=0 なら何もしない)
zero_fill:
	ld	a, b
	or	a, c
	ret	z
	ld	(hl), #0
	dec	bc
	ld	a, b
	or	a, c
	ret	z
	ld	e, l
	ld	d, h
	inc	de
	ldir
	ret

	.area	_GSINIT
gsinit::
	.area	_GSFINAL
	ret
