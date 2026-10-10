; pcm.s — 誘導弾の声を、ゲームを止めずにターボ R の内蔵 PCM で鳴らす
;
; ■ 仕組み(検証は tools/lineirq192/ と docs/検証記録.md「SCREEN 3 で、走査線割込みから PCM の声を止めずに鳴らす」)
;   ターボ R の PCM は CPU が 1 サンプルずつ A4h に書くしかなく、サンプルの周期で割込みを出す時計も無い。
;   そこで鳴っている間だけ、VDP の走査線割込み(R#19)を次のサンプルの行に張り、割込みのたびに 1 サンプル出す。
;   ・ふだんは今までどおり垂直帰線の割込み(IE0)。pcm_fire で頼まれると、次の垂直帰線の割込みの中で切り替える:
;     R#15 = 1(割込みで読む 0x99 が S#1 になり FH が落ちる)、IE0 を切る、IE1 を立てる。次のフレームの 3 行目から出す
;   ・サンプルは 3.9375 行(64 カウント = 250µs、3,996Hz)ごと。行は 9.7 の固定小数点で持ち、張るのは切り上げの行
;   ・SCREEN 3(192 行)では 235〜261 行目に割込みが来ない(tools/lineirq192/lineirq.s で測った)。次の行がそこに入るときは
;     234 行目に「合図」を張る。合図では毎フレームの処理(htimi: 表示ページの切り替え・音)を呼び、次のフレームの 3 行目から
;     続ける。その間の 30 行(約 1.9ms)は声を止めたまま待たない(待つと声の間の CPU が約 25%、待たなければ約 15%。
;     声は 1 割ほど伸びる。ユーザーが聞いて選んだ)
;   ・割込みを止める区間などで 2.5 行以上遅れて入ったら、今の行から張り直す(声は遅れた分だけ伸びる。飛ばさない)
;   ・出し終わったら、次の合図で毎フレームの処理をしてから、ふだんに戻す(S#0 を読んで、たまった垂直帰線のフラグを落とす)
;   ・鳴らしている間は、転送で CPU を Z80 に切り替えない(vdp.s の vdp_xfer)。切り替え(BIOS の CHGCPU)の間は走査線割込みを
;     止めるしかなく、張った行が過ぎると割込みが来ないまま消えて声が止まった。ステージの読み込みなどページ0を BIOS に戻す前には
;     pcm_stop で止める(BIOS が走査線割込みを受けると、次の行が張られず止まる)
;   ・声のデータはゲームモジュールにある(voice_fire.s)。割込みの中ではページ2をゲームのセグメントにして読む
;   ・割込みの処理(サンプルごとに走る所)はページ0の RAM(エリア _PAGE0)に置く。切り替え(pcm_begin / pcm_leave)は常駐部。Z80 で転送している間も動くので、R800 専用の命令は使わない
;
; ■ 割込みの中で触る VDP: S#1 の読み 1 回と R#19 の書き込み(2 バイト)。本体は 99h への 2 バイトの書き込みを DI で囲んでいる

	.module	pcm
	.globl	htimi, _g_seg2, _g_vdp_r1

SEG_GAME = 1			; ゲームモジュールのセグメント(sys.s と同じ)
R0_BASE	= 0x00			; R#0(main.c の vdp_init が書く値。BIOS の控え RG0SAV は使わない: 本体が直接書くので合わない)
STEP	= 504			; 1 サンプルの行数(9.7)= 3.9375 行
PERIOD	= 64			; 1 サンプルのカウント(システムタイマ 3.911µs × 64 = 250µs)
FIRSTLINE = 3			; フレームの最初のサンプルの行
TICKLINE = 234			; 合図の行(割込みが来る最後の行)
LATE	= 24			; これ以上(カウント、約 1.5 行)遅れて入ったら張り直す(40 では、2 行ほど遅れたまま次の行を張り、張り終える前に過ぎることがあった)

	.area	_DATA
_pcm_mode::	.ds	1	; 0 = ふだん(垂直帰線の割込み) / 1 = 走査線割込みで鳴らしている
_pcm_req::	.ds	1	; 1 = 次の垂直帰線で鳴らし始める
_pcm_r0::	.ds	1	; R#0(IE1 を除いた値)
_g_dbg_fire::	.ds	1	; (調べ用)1 を書くと game.c が「FIRE」を鳴らす
pcm_rptr::	.ds	2	; 頼まれた声の先頭と長さ
pcm_rlen::	.ds	2
pcm_ptr::	.ds	2	; 次に出すサンプル(ゲームモジュールの中)
pcm_left::	.ds	2	; 残りのサンプル数
pcm_dline::	.ds	2	; 次のサンプルの行(9.7)
pcm_due::	.ds	2	; いま出したサンプルの予定の時刻
pcm_cur::	.ds	2	; 遅れて入ったときの、今の行
pcm_arm::	.ds	1	; 張った行
pcm_kind::	.ds	1	; 0 = サンプルの行を張った / 1 = 合図の行を張った
pcm_first::	.ds	1	; 1 = 鳴らし始めの最初のサンプル(予定の時刻と行の基準をそこで取る。以降のフレームは合図で出す)
pcm_t0::	.ds	2	; 合図の時刻
pcm_end::	.ds	1	; 1 = 出し終わった(次の合図でふだんに戻る)

	.area	_CODE

; ---------------------------------------------------------------------------
; void pcm_fire(const u8 *p, u16 len)   HL = 声の先頭(ゲームモジュールの中)、DE = サンプル数
;   鳴っていなければ次の垂直帰線から鳴らし始める。鳴っていれば頭から差し替える
; ---------------------------------------------------------------------------
_pcm_fire::
	di
	ld	(pcm_rptr), hl
	ld	(pcm_rlen), de
	ld	a, (_pcm_mode)
	or	a, a
	jr	nz, 1$
	ld	a, #1
	ld	(_pcm_req), a
	ei
	ret
1$:
	ld	(pcm_ptr), hl
	ld	(pcm_left), de
	xor	a, a
	ld	(pcm_end), a
	ei
	ret

; ---------------------------------------------------------------------------
; void pcm_stop(void) — すぐ止めて、ふだんの割込みに戻す(ページ0を BIOS に戻す前に呼ぶ)
; ---------------------------------------------------------------------------
_pcm_stop::
	di
	xor	a, a
	ld	(_pcm_req), a
	ld	a, (_pcm_mode)
	or	a, a
	call	nz, pcm_leave
	ei
	ret


; 垂直帰線の割込み(sys.s の isr0)から、htimi のあとに呼ぶ(割込みは止まっている)。走査線割込みで鳴らす形に切り替える
_pcm_begin::
	xor	a, a
	ld	(_pcm_req), a
	ld	(pcm_end), a
	ld	(pcm_kind), a
	ld	hl, (pcm_rptr)
	ld	(pcm_ptr), hl
	ld	hl, (pcm_rlen)
	ld	(pcm_left), hl
	ld	a, #1
	ld	(_pcm_mode), a
	ld	(pcm_first), a
	ld	hl, #FIRSTLINE * 128
	ld	(pcm_dline), hl
	ld	a, #FIRSTLINE
	ld	(pcm_arm), a
	ld	a, #1			; R#15 = 1
	out	(0x99), a
	ld	a, #0x80 + 15
	out	(0x99), a
	ld	a, #FIRSTLINE		; R#19 = 次のフレームの 3 行目
	out	(0x99), a
	ld	a, #0x80 + 19
	out	(0x99), a
	ld	a, (_g_vdp_r1)		; IE0 を切る
	and	a, #0xDF
	out	(0x99), a
	ld	a, #0x80 + 1
	out	(0x99), a
	ld	a, #R0_BASE		; IE1 を立てる
	ld	(_pcm_r0), a
	or	a, #0x10
	out	(0x99), a
	ld	a, #0x80
	out	(0x99), a
	ld	a, #0x03		; ダブルバッファ(書いた値は次の 15.7kHz の刻みで出る)、音あり
	out	(0xA5), a
	ret

; ふだんの割込みに戻す(割込みは止まっている)
pcm_leave:
	xor	a, a			; R#15 = 0
	out	(0x99), a
	ld	a, #0x80 + 15
	out	(0x99), a
	in	a, (0x99)		; S#0: 鳴らしている間にたまった垂直帰線のフラグを落とす
	ld	a, (_pcm_r0)		; IE1 を切る
	out	(0x99), a
	ld	a, #0x80
	out	(0x99), a
	ld	a, (_g_vdp_r1)		; IE0 を戻す
	out	(0x99), a
	ld	a, #0x80 + 1
	out	(0x99), a
	xor	a, a
	ld	(_pcm_mode), a
	ld	a, #0x80
	out	(0xA4), a
	ret

	.area	_PAGE0

; 今の時刻(システムタイマ 16bit)を HL へ。上位・下位・上位と読み、上位が揃うまで読み直す(繰り上がりで 256 狂わないため)
pcm_now::
	in	a, (0xE7)
	ld	h, a
	in	a, (0xE6)
	ld	l, a
	in	a, (0xE7)
	cp	a, h
	jr	nz, pcm_now
	ret

; A = 行 → サンプルの行として張る。234 より後の行(割込みが来ない)は張らず、合図の行を張る
;   (来ない行を張ると、声の形のまま割込みが二度と来ず、毎フレームの処理も止まって固まった)
pcm_set:
	cp	a, #TICKLINE + 1
	jr	nc, pcm_set_tick
	ld	(pcm_arm), a
	out	(0x99), a
	ld	a, #0x80 + 19
	out	(0x99), a
	xor	a, a
	ld	(pcm_kind), a
	ret

; 合図の行(234)を張る
pcm_set_tick:
	ld	a, #TICKLINE
	ld	(pcm_arm), a
	out	(0x99), a
	ld	a, #0x80 + 19
	out	(0x99), a
	ld	a, #1
	ld	(pcm_kind), a
	ret

; 合図(pcm_cur = 今の行): 毎フレームの処理(表示ページの切り替え・音)をして、次のフレームの最初の行を張る。
;   出し終わっていれば、ふだんの割込みに戻す。割込みは止まっている
pcm_tick_do:
	call	pcm_now
	ld	(pcm_t0), hl		; この合図の時刻(pcm_cur の行)。次のフレームの最初のサンプルの予定の時刻をここから出す
	ld	a, (pcm_end)
	or	a, a
	jr	z, 1$
	call	pcm_htimi		; 出し終わった: 毎フレームの処理をして、ふだんに戻す
	jp	pcm_leave
1$:
	; 次のフレームの最初の行を、毎フレームの処理(htimi)より先に張る(htimi が長いと、後から張ったときには
	; もう過ぎていて 1 フレーム来ず、遅れの扱いでまた合図をして、1 フレームに 1 サンプルしか出なくなった)。
	; 行は、今の行が 262 を超えていれば(遅れて入った)そのフレームの今の行 + 2、でなければ 3
	ld	hl, (pcm_cur)
	ld	de, #-262
	add	hl, de
	ld	a, #FIRSTLINE
	jr	nc, 2$
	ld	a, h
	or	a, a
	ld	a, #FIRSTLINE
	jr	nz, 2$			; 1 フレーム以上遅れた(見積もりが壊れている): 3 行目から
	ld	a, l
	add	a, #2
	cp	a, #TICKLINE - 4
	jr	c, 3$
	ld	a, #FIRSTLINE
3$:
	cp	a, #FIRSTLINE
	jr	nc, 2$
	ld	a, #FIRSTLINE
2$:
	; その行の予定の時刻 = 合図の時刻 + (行 + 262 - 今の行) × 16.25(= ×16 + ÷4)
	;   (最初は最初のサンプルの割込みが来た時刻を予定にしていて、比較で遅れて入っても遅れと見なさなかった)
	ld	l, a
	ld	h, #0
	ld	de, #262
	add	hl, de
	ld	de, (pcm_cur)
	or	a, a
	sbc	hl, de			; n = 行数
	ld	d, h
	ld	e, l
	srl	d
	rr	e
	srl	d
	rr	e			; n / 4
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl			; n × 16
	add	hl, de
	ld	de, (pcm_t0)
	add	hl, de
	ld	(pcm_due), hl
	ld	h, a
	ld	l, #0
	srl	h
	rr	l
	ld	(pcm_dline), hl		; 行 × 128
	call	pcm_set
pcm_htimi:
	push	ix
	push	iy
	call	htimi
	pop	iy
	pop	ix
	ret

; pcm_cur = 今の行 から張り直す(遅れて入ったとき・CPU を切り替えたあと)。割込みは止まっている
pcm_rebase::
	ld	hl, (pcm_cur)
	ld	de, #-(TICKLINE - 3)
	add	hl, de
	jr	c, pcm_tick_do		; 合図の行まで 3 行を切った(か過ぎた): すぐ合図
	ld	a, (pcm_end)
	or	a, a
	jp	nz, pcm_set_tick
	ld	a, (pcm_cur)
	add	a, #4			; 次は今の行 + 4(+2 では、見積もりの誤差と張るまでの時間で過ぎていて 1 フレーム止まった)
	cp	a, #TICKLINE - 2
	jp	nc, pcm_set_tick
	ld	h, a
	ld	l, #0
	srl	h
	rr	l
	ld	(pcm_dline), hl
	push	af
	call	pcm_now
	ld	de, #64			; 予定の時刻 ≒ 今 + 4 行
	add	hl, de
	ld	(pcm_due), hl
	pop	af
	jp	pcm_set

; 鳴らしている間の割込み(sys.s の isr0 が AF を積んで飛んでくる)
_pcm_isr::
	in	a, (0x99)		; S#1: FH を落とす
	push	bc
	push	de
	push	hl
	ld	a, (pcm_kind)
	or	a, a
	jr	z, 1$
	ld	hl, #TICKLINE		; 合図の行
	ld	(pcm_cur), hl
	call	pcm_tick_do
	jp	pcm_ret
1$:
	; ---- サンプルの行
	call	pcm_now
	ld	a, (pcm_first)
	or	a, a
	jr	z, 2$
	xor	a, a			; 鳴らし始めの最初のサンプル: 予定の時刻をいまに取る
	ld	(pcm_first), a
	ld	(pcm_due), hl
2$:
	push	hl			; 今
	ld	a, (pcm_end)
	or	a, a
	jr	nz, 3$
	ld	a, #SEG_GAME		; 声はゲームモジュールにある
	out	(0xFE), a
	ld	hl, (pcm_ptr)
	ld	a, (hl)
	out	(0xA4), a
	inc	hl
	ld	(pcm_ptr), hl
	ld	a, (_g_seg2)
	out	(0xFE), a
	ld	hl, (pcm_left)
	dec	hl
	ld	(pcm_left), hl
	ld	a, h
	or	a, l
	jr	nz, 3$
	inc	a
	ld	(pcm_end), a		; 出し終わった
3$:
	pop	de			; DE = 今
	ld	hl, (pcm_due)
	ex	de, hl
	or	a, a
	sbc	hl, de			; 遅れ = 今 - 予定
	bit	7, h
	jr	nz, 4$			; 早い(ふつうは切り上げの行の頭なので 0〜16 カウント遅い)
	ld	de, #-LATE
	add	hl, de
	jr	c, 6$			; 遅れて入った
4$:
	; ---- 次のサンプル: 予定の時刻 + 64、行 + 3.9375
	ld	hl, (pcm_due)
	ld	de, #PERIOD
	add	hl, de
	ld	(pcm_due), hl
	ld	hl, (pcm_dline)
	ld	de, #STEP
	add	hl, de
	ld	(pcm_dline), hl
	ld	a, (pcm_end)
	or	a, a
	jr	nz, 5$
	ld	de, #-(TICKLINE * 128 + 1)
	add	hl, de
	jr	c, 5$			; 張る行(切り上げ)が 235 以降: 割込みが来ないので合図へ
	ld	hl, (pcm_dline)
	ld	de, #127
	add	hl, de
	add	hl, hl
	ld	a, h			; 張る行(切り上げ)
	call	pcm_set
	jr	pcm_ret
5$:
	; 合図の行(234)を張る。張った行が 231 以降なら、すぐ合図(1〜2 行先を張ると、割込みが少し遅れて入っただけで
	; 張り終える前にその行が過ぎ、1 フレーム止まった。231 行目は表示の外なので、そこで表示ページを切り替えてもよい)
	ld	a, (pcm_arm)
	cp	a, #TICKLINE - 3
	jr	nc, 7$
	call	pcm_set_tick
	jr	pcm_ret
6$:
	; 遅れて入った: HL = 遅れ - LATE。今の行 ≒ 張った行 + 遅れ / 16
	ld	de, #LATE
	add	hl, de
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	srl	h
	rr	l
	ld	a, (pcm_arm)
	ld	e, a
	ld	d, #0
	add	hl, de
	ld	(pcm_cur), hl
	call	pcm_rebase
	jr	pcm_ret
7$:
	ld	l, a
	ld	h, #0
	ld	(pcm_cur), hl
	call	pcm_tick_do
pcm_ret:
	pop	hl
	pop	de
	pop	bc
	pop	af
	ei
	ret
