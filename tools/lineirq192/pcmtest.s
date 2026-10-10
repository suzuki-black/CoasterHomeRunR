; pcmtest.s — 検証 ROM: SCREEN 3(192 行)で、走査線割込みから PCM を 1 サンプルずつ出し、声(「FIRE」)をゲームを止めずに鳴らす。
;   lineirq.s の測定(2〜234 は値の行で来る。235〜261 行目の 27 行は来ない)を使う。ZeroNoHoukouR の hot_pcm.s と同じ考え方。
;
;   ・R800(DRAM モード。プログラムと声は RAM の 0xD000〜 に写して動かす)/ SCREEN 3 / IM2(表 0xE100、入口 0xE2E2)。BIOS を通さない(ゲームの自前の割込み処理と同じ重さで測る)
;   ・R#15 は普段から 1(S#1)。割込みのたびに 0x99 を読んで FH を落とす。VBLANK 割込み(IE0)は切る
;   ・鳴っていない間は 2 行目に割込みを張る(基準の行)。鳴らし始めるのも、この割込みから
;   ・鳴っている間: 張った行の割込みが来たら、すぐに 1 サンプル出し(A5h = 03h のダブルバッファ。次の 15.7kHz の刻みで出る)、
;     次のサンプルの行を張る。行は 9.7 の固定小数点で持ち(次のフレームの頭まで数えるので 255 を超える)、1 サンプルごとに 3.938 行(= 64 カウント、3,996Hz)足すだけ。
;     張るのは切り上げの行(サンプルの時刻より後の行。出力は刻みでしか変わらないので、1 行の遅れは音にほぼ出ない)。
;     タイマも割り算も使わない(最初は割込みのたびにタイマを読んで行を割り算で出していたが、遅くて張る行を過ぎ、1 フレーム止まった)
;   ・割込みが来ない行(235〜261)に入るサンプルは、その手前の割込みの中で、行の時刻(1 行 16.25 カウント)まで待って出す
;   ・メイン: CPU の仕事を回し、毎フレーム DIW カウントだけ割込みを止める(ゲームの転送の前の比較をまねる)
;
;   RAM: 0xE000 = 1 を書くと鳴らし始める / 0xE002 = DIW(16bit)/ 0xC000〜 = A4h に書いた時刻の記録(2 バイトずつ)
;   目印: 0xE020 = 0xA55A(初期化が済んだ)
	.module	pcmtest
	.area	ROMA (ABS)

CHGCPU	= 0x0180
CHGMOD	= 0x005F
RG0SAV	= 0xF3DF
RG1SAV	= 0xF3E0

GO	= 0xE000
DUE	= 0xE009	; 次のサンプルの予定の時刻
DIW	= 0xE002
ACTIVE	= 0xE004
PTR	= 0xE005
LEFT	= 0xE007
TA	= 0xE00B
ARM	= 0xE00D	; 0 = 基準(2 行目)を張った / 1 = サンプルの行を張った
TICK	= 0xE00E
LOGP	= 0xE010
NLATE	= 0xE012	; 1ms 以上遅れて合わせ直した回数
FBASE	= 0xE014	; メインのフレームの区切りの時刻
DLINE	= 0xE01C	; 次のサンプルの行(9.7)
PEND	= 0xE020 + 4	; 1 = 次の基準の行から鳴らす
NSKIP	= 0xE028	; 遅れて飛ばしたサンプルの数
LOG	= 0xC000
LOGEND	= 0xD000
IM2TAB	= 0xE100
IM2ENT	= 0xE2E2
.ifndef NOLOG
NOLOG	= 0
.endif
.ifndef FAST
FAST	= 0
.endif
RAMCODE	= 0xD000		; プログラムと声を写す先(ROM では 0x5000〜。4KB まで)

STEP	= 504		; 1 サンプルの行数(9.7)= 3.938 行 = 64 カウント相当(3,996Hz)
LINES	= 262		; 1 フレームの行数
PERIOD	= 64		; 1 サンプルのカウント(3.911µs × 64 = 250µs)
FRAME	= 4267		; 1 フレーム(262 行)
GAPLINE	= 235		; ここから下の行は割込みが来ない
ALINE	= 2		; 基準の行

	.org	0x4000
	.db	0x41, 0x42
	.dw	boot
	.dw	0, 0, 0, 0, 0, 0

	; ROM の 0x5000〜 に置いたプログラムと声を、RAM の 0xD000〜 へ写して動かす(ROM の上で動くと R800 は読むたびに
	; 待たされ、割込みの重さがゲーム(RAM の上で動く)と合わない)
	.org	0x4010
boot:
	di
	ld	hl, #0x5000
	ld	de, #RAMCODE
	ld	bc, #0x1000
	ldir
	jp	init

	.org	RAMCODE
init:
	di
	ld	a, #0x82		; R800(DRAM モード)
	call	CHGCPU
	ld	a, #3
	call	CHGMOD
	di
	ld	hl, #0xE000
	ld	de, #0xE001
	ld	bc, #0x00FF
	ld	(hl), #0
	ldir
	ld	hl, #LOG
	ld	(LOGP), hl
	; IM2: 表を 0xE2 で埋め、入口に jp isr
	ld	hl, #IM2TAB
	ld	de, #IM2TAB + 1
	ld	bc, #256
	ld	(hl), #0xE2
	ldir
	ld	a, #0xC3
	ld	(IM2ENT), a
	ld	hl, #isr
	ld	(IM2ENT + 1), hl
	ld	a, #>IM2TAB
	ld	i, a
	im	2
	; R#15 = 1、R#19 = 基準の行、IE0 を切る、IE1 を立てる
	ld	a, #1
	out	(0x99), a
	ld	a, #0x8F
	out	(0x99), a
	ld	a, #ALINE
	out	(0x99), a
	ld	a, #0x80 + 19
	out	(0x99), a
	ld	a, (RG1SAV)
	and	a, #0xDF
	out	(0x99), a
	ld	a, #0x81
	out	(0x99), a
	ld	a, (RG0SAV)
	or	a, #0x10
	out	(0x99), a
	ld	a, #0x80
	out	(0x99), a
	ld	a, #0x80
	out	(0xA4), a
	ld	a, #0x03		; ダブルバッファ、音あり
	out	(0xA5), a
	ld	hl, #0xA55A
	ld	(0xE020), hl
	ei

main:
	ld	a, (GO)
	or	a, a
	call	nz, start
	; 次のフレームまで CPU の仕事(数えるだけ)。フレームは時刻で区切る(鳴っている間は基準の行の割込みが来ないため)
1$:
	ld	hl, (0xE030)
	inc	hl
	ld	(0xE030), hl
	call	now
	ld	de, (FBASE)
	or	a, a
	sbc	hl, de
	ld	de, #FRAME
	or	a, a
	sbc	hl, de
	jr	c, 1$
	ld	hl, (FBASE)
	add	hl, de
	ld	(FBASE), hl
	; 割込みを止める区間(DIW カウント)
	ld	hl, (DIW)
	ld	a, h
	or	a, l
	jr	z, main
	ex	de, hl
	di
	call	now
	add	hl, de
	ex	de, hl
2$:
	call	now
	or	a, a
	sbc	hl, de
	bit	7, h
	jr	nz, 2$
	ei
	jr	main

start:
	; 鳴らし始めるのは基準の行(2 行目)の割込みから(行の数え始めをそろえるため)
	xor	a, a
	ld	(GO), a
	di
	ld	hl, #snd
	ld	(PTR), hl
	ld	hl, #SND_LEN
	ld	(LEFT), hl
	ld	a, #1
	ld	(PEND), a
	ei
	ret

now:
	in	a, (0xE7)
	ld	h, a
	in	a, (0xE6)
	ld	l, a
	in	a, (0xE7)
	cp	a, h
	jr	nz, now
	ret

; 割込み: 張った行が来たら、すぐにサンプルを出して次の行を張る(タイマも割り算も使わない)
isr:
	push	af
	push	bc
	push	de
	push	hl
	in	a, (0x99)		; S#1: FH を落とす
	ld	a, (ACTIVE)
	or	a, a
	jr	nz, 20$
	; 基準の行(2 行目)
	call	now
	ld	(TA), hl
	ld	(DUE), hl		; サンプルの予定の時刻(行と並べて 64 カウントずつ進める)
	ld	hl, #TICK
	inc	(hl)
	ld	a, (PEND)
	or	a, a
	jp	z, 90$			; 鳴らすものが無い: 2 行目を張ったまま
	xor	a, a
	ld	(PEND), a
	inc	a
	ld	(ACTIVE), a
	ld	a, #0x03
	out	(0xA5), a
	ld	hl, #ALINE * 128
	ld	(DLINE), hl
20$:
	; この行のサンプルを出す
	call	feed
	ld	a, (ACTIVE)
	or	a, a
	jp	z, 40$			; 鳴り終わった
50$:
	; 次のサンプルの行と時刻。262 行を超えたら次のフレームへ(行から 262 を引き、TA を 1 フレーム進め、
	; 時刻を行から出し直す。行の 3.9375 行と時刻の 64 カウントのわずかなずれを、フレームごとにそろえる)
	ld	hl, (DUE)
	ld	de, #PERIOD
	add	hl, de
	ld	(DUE), hl
	ld	hl, (DLINE)
	ld	de, #STEP
	add	hl, de
	ld	(DLINE), hl
	ld	de, #-LINES * 128
	add	hl, de
	jr	nc, 51$
	ld	(DLINE), hl
	ld	hl, (TA)
	ld	de, #FRAME
	add	hl, de
	ld	(TA), hl
	call	ltime
	ld	(DUE), hl
51$:
	.if	FAST
	; (下限を測る版)遅れの判定をせず、張る行の判定だけ
	jp	52$
	.endif
	; d = その時刻 - 今
	ld	de, (DUE)
	call	now
	ex	de, hl
	or	a, a
	sbc	hl, de
	ld	de, #16
	add	hl, de			; d + 16
	bit	7, h
	jr	nz, 55$			; 1 行より過ぎた: 飛ばす
	ld	de, #-24
	add	hl, de			; d - 8
	jr	nc, 60$			; あと 8 カウントより近い: 待って出す(張る行は切り上げなので、行の始まりはさらに先)
52$:
	; 張る行(切り上げ)。割込みが来ない行(235 以降、次のフレームの 0〜2 行)なら待って出す。
	;   235 以降かは 2 倍する前に見る(262 行の手前を 2 倍すると 16bit を超え、小さい行に見えて 1 フレーム外した)
	ld	hl, (DLINE)
	ld	de, #-((GAPLINE - 1) * 128 + 1)
	add	hl, de
	jr	c, 60$
	ld	hl, (DLINE)
	ld	de, #127
	add	hl, de
	add	hl, hl
	ld	a, h
	cp	a, #3
	jr	c, 60$
	out	(0x99), a
	ld	a, #0x80 + 19
	out	(0x99), a
	jr	90$
55$:
	; 飛ばす(声の長さは変えない)
	ld	hl, (PTR)
	inc	hl
	ld	(PTR), hl
	ld	hl, #NSKIP
	inc	(hl)
	ld	hl, (LEFT)
	dec	hl
	ld	(LEFT), hl
	ld	a, h
	or	a, l
	jr	nz, 50$
	xor	a, a
	ld	(ACTIVE), a
	jr	40$
60$:
	; その時刻まで待って出す
	ld	de, (DUE)
61$:
	call	now
	or	a, a
	sbc	hl, de
	bit	7, h
	jr	nz, 61$
	call	feed
	ld	a, (ACTIVE)
	or	a, a
	jp	nz, 50$
40$:
	; 鳴っていない: 基準の行を張る
	ld	a, #ALINE
	out	(0x99), a
	ld	a, #0x80 + 19
	out	(0x99), a
90$:
	pop	hl
	pop	de
	pop	bc
	pop	af
	ei
	reti

; HL = DLINE の行の時刻 = TA + D / 8 + D / 512(D = DLINE - 2 行、9.7。1 行 = 16.25 カウント = 128 × (1/8 + 1/512))。
;   D は 0〜33,000 ほど(符号つき 16bit を超える)か、負(次のフレームの 0〜1 行)。正なら符号なし、負なら符号つきでずらす
;   (最初は全部符号つきでずらし、260 行目あたりの時刻を大きく間違えて、そのサンプルを飛ばしていた)
ltime:
	ld	hl, (DLINE)
	ld	de, #-ALINE * 128
	add	hl, de
	jr	nc, 5$			; 負
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l			; D / 8
	push	hl
	ld	b, #6
1$:
	srl	h
	rr	l
	djnz	1$			; D / 512
	jr	6$
5$:
	sra	h
	rr	l
	sra	h
	rr	l
	sra	h
	rr	l
	push	hl
	ld	b, #6
2$:
	sra	h
	rr	l
	djnz	2$
6$:
	pop	de
	add	hl, de
	ld	de, (TA)
	add	hl, de
	ret

; 1 サンプル出す。最後なら止める
feed:
	ld	hl, (PTR)
	ld	a, (hl)
	out	(0xA4), a
	inc	hl
	ld	(PTR), hl
	.if	NOLOG
	jr	1$
	.endif
	ld	hl, (LOGP)
	ld	de, #LOGEND
	or	a, a
	sbc	hl, de
	jr	nc, 1$
	add	hl, de
	push	hl
	call	now
	ex	de, hl
	pop	hl
	ld	(hl), e
	inc	hl
	ld	(hl), d
	inc	hl
	ld	de, (DLINE)		; (調べ用)そのときの行
	ld	(hl), e
	inc	hl
	ld	(hl), d
	inc	hl
	ld	(LOGP), hl
1$:
	ld	hl, (LEFT)
	dec	hl
	ld	(LEFT), hl
	ld	a, h
	or	a, l
	ret	nz
	xor	a, a
	ld	(ACTIVE), a
	ld	a, #0x80
	out	(0xA4), a
	ret

snd:
	.include "fire.inc"
