; lineirq.s — 検証 ROM: SCREEN 3(192 行)で、走査線割込み(R#19)の値ごとに「来るか」「0 行目から何カウント後か」を測る。
;   誘導弾の「FIRE」の声を、ゲームを止めずに PCM で鳴らす準備(走査線割込みで 1 サンプルずつ出す。
;   ZeroNoHoukouR の docs/PCM調査_2026-10-07.md)。212 行の画面では割込みが出せない行が分かっているが、
;   192 行の画面ではまだ誰も確かめていない。
;
;   ・R800 / SCREEN 3 / IM1(BIOS の割込み処理から H.KEYI を呼ばせる)
;   ・R#15 は普段から 1(S#1)。BIOS が H.KEYI のあと「S#0 のつもりで」読む 0x99 は S#1 になり、
;     その読み出しが FH を落とす。VBLANK 割込み(IE0)は切り、走査線割込み(IE1)だけを出す
;   ・値 v ごとに 4 フレーム: 0 行目に割込みを張り、来たら時刻 T0 を取って v を張る。v が来たら時刻 T1 を取り、
;     T1 - T0 を表に書いて 0 行目を張り直す。v が来なければ表は 0xFFFF のまま
;   ・時刻はシステムタイマ(E6h/E7h、1 カウント 3.911µs)。上位・下位・上位と読み、上位が揃うまで読み直す
;
;   結果(RAM): 0xE200 + v * 2 = T1 - T0(16bit)、0xE400 + v = v の割込みの回数
;   目印: 0xE020 = 0xA55A(初期化が済んだ)、0xE022 = 0x5AA5(256 個を測り終えた)
	.module	lineirq
	.area	ROMA (ABS)

H_KEYI	= 0xFD9A
RG0SAV	= 0xF3DF
RG1SAV	= 0xF3E0
CHGCPU	= 0x0180
CHGMOD	= 0x005F

V	= 0xE000	; いま測っている値
STATE	= 0xE001	; 0 = 0 行目を待つ / 1 = v を待つ
T0	= 0xE002
TAB	= 0xE200	; v ごとの T1 - T0
CNTT	= 0xE400	; v ごとの回数
FRAME	= 4267		; 1 フレーム(16.688ms)のカウント

	.org	0x4000
	.db	0x41, 0x42
	.dw	init
	.dw	0, 0, 0, 0, 0, 0

	.org	0x4010
init:
	di
	ld	a, #0x81		; R800(ROM モード)
	call	CHGCPU
	ld	a, #3
	call	CHGMOD			; SCREEN 3
	di
	; 表を 0xFFFF / 0 で埋める
	ld	hl, #TAB
	ld	de, #TAB + 1
	ld	bc, #511
	ld	(hl), #0xFF
	ldir
	ld	hl, #CNTT
	ld	de, #CNTT + 1
	ld	bc, #255
	ld	(hl), #0
	ldir
	xor	a
	ld	(V), a
	ld	(STATE), a
	; H.KEYI へ JP isr
	ld	a, #0xC3
	ld	(H_KEYI), a
	ld	hl, #isr
	ld	(H_KEYI + 1), hl
	; R#15 = 1、R#19 = 0、R#23 = 0
	ld	a, #1
	out	(0x99), a
	ld	a, #0x8F
	out	(0x99), a
	xor	a
	out	(0x99), a
	ld	a, #0x80 + 19
	out	(0x99), a
	xor	a
	out	(0x99), a
	ld	a, #0x80 + 23
	out	(0x99), a
	; IE0 を切る
	ld	a, (RG1SAV)
	and	a, #0xDF
	ld	(RG1SAV), a
	out	(0x99), a
	ld	a, #0x81
	out	(0x99), a
	; IE1 を立てる
	ld	a, (RG0SAV)
	or	a, #0x10
	ld	(RG0SAV), a
	out	(0x99), a
	ld	a, #0x80
	out	(0x99), a
	ld	hl, #0xA55A
	ld	(0xE020), hl
	ei

main:
	; v を測る: 0 行目を張って 4 フレーム待つ
	di
	xor	a
	ld	(STATE), a
	out	(0x99), a
	ld	a, #0x80 + 19
	out	(0x99), a
	ei
	ld	b, #4
1$:
	push	bc
	call	now
	ld	de, #FRAME
	add	hl, de
	ex	de, hl			; DE = 待ち終わりの時刻
2$:
	call	now
	or	a, a
	sbc	hl, de
	bit	7, h
	jr	nz, 2$
	pop	bc
	djnz	1$
	ld	a, (V)
	inc	a
	ld	(V), a
	jr	nz, main
	ld	hl, #0x5AA5
	ld	(0xE022), hl
	; 0 行目だけを張って止まる
	di
	xor	a
	ld	(STATE), a
	out	(0x99), a
	ld	a, #0x80 + 19
	out	(0x99), a
	ei
3$:
	jr	3$

; 今の時刻(16bit)を HL へ。壊すのは A とフラグ
now:
	in	a, (0xE7)
	ld	h, a
	in	a, (0xE6)
	ld	l, a
	in	a, (0xE7)
	cp	a, h
	jr	nz, now
	ret

; H.KEYI(BIOS の割込み処理が最初に呼ぶ)。ステータスは BIOS が読む(R#15 = 1 なので S#1 になり FH が落ちる)
isr:
	call	now
	ld	a, (STATE)
	or	a, a
	jr	nz, 5$
	; 0 行目: T0 を取って v を張る
	ld	(T0), hl
	ld	a, #1
	ld	(STATE), a
	ld	a, (V)
	jr	9$
5$:
	; v: T1 - T0 を表へ、回数を +1、0 行目を張り直す
	ld	de, (T0)
	or	a, a
	sbc	hl, de
	ex	de, hl
	ld	a, (V)
	ld	l, a
	ld	h, #0
	add	hl, hl
	ld	bc, #TAB
	add	hl, bc
	ld	(hl), e
	inc	hl
	ld	(hl), d
	ld	a, (V)
	ld	l, a
	ld	h, #>CNTT
	inc	(hl)
	xor	a
	ld	(STATE), a
9$:
	out	(0x99), a
	ld	a, #0x80 + 19
	out	(0x99), a
	ret
