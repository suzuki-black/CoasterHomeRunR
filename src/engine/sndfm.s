; sndfm.s — BGM: FM 音源(MSX-MUSIC = YM2413)のドライバ(音のモジュールの中。セグメント SEG_SND の 0x8000-)
;
; 曲のデータは同じモジュールの SONG_TAB から(形式は tools/gen_music.py の冒頭)。
; 曲ごとにオリジナル音色(音色 0)を持てる(内蔵の音色だけだと YM2413 らしい音になりがちなので、どの曲も使う)。
; リズムが打った音は g_drm に入れ、PSG のノイズ(ch B)を重ねる(sndtick.c)。曲の PSG のパート(ch A)の番地は g_psg_top へ。
; 割り込み(sys.s)はページ2を SEG_SND にしてから 0x8000(sndtick.c の sndm_tick)を呼び、そこから fm_bgm_tick を呼ぶ。
; 曲を鳴らす・止める(fm_play・fm_stop)も、常駐部の中継がページ2を SEG_SND にしてから sndtick.c 経由で呼ぶ。
; メロディは FM の ch0〜5、リズムは ch6〜8(リズムモード)。レジスタへの書き込みは fm_write(sys.s。書くたびに待つ)。
; 割り込みは Z80 転送の間(CPU が Z80)にも入るので、R800 専用の命令は使わない。

	.module	sndfm
	.globl	_fm_write, _g_drm, _g_hha

SONG_TAB = 0x8B40		; 曲の表(tools/gen_music.py の BASE。ドライバのコードはその手前まで。tools/mkrom.py が確かめる)
FMC_P	= 0			; チャンネル: 今読んでいる所(0 = 止まっている)
FMC_TOP	= 2			; 頭
FMC_LOOP = 4			; ループで戻る所(0 = 頭)
FMC_WAIT = 6			; 今の音・休みの残りティック
FMC_GATE = 7			; 音の終わりのこのティック前に音を切る(0 = 切らない)
FMC_HI	= 8			; レジスタ 0x20+ch の値(キーオンを除く)
FMC_ON	= 9			; 1 = キーオン中
FMC_RSP	= 10			; くり返しの段(0〜2)
FMC_R0P	= 11			; くり返し 1 段目: 戻る所(2)と残りの回数(1)
FMC_R0N	= 13
FMC_R1P	= 14			; 2 段目
FMC_R1N	= 16
FMC_DET	= 17			; 音程のずらし(F ナンバーに足す。符号つき。同じ旋律を 2 チャンネルで鳴らしてうならせる)
FMC_SIZE = 18
FM_MEL	= 6

	.area	_DATA
fm_ch:		.ds	FMC_SIZE * (FM_MEL + 1)	; メロディ 6 + リズム
fm_nmel:	.ds	1
fm_rhy:		.ds	1
fm_flags:	.ds	1
fm_hdr:		.ds	2	; 見出しを読んでいる所(リズムの初期化の間とっておく)
_g_bgm_on::	.ds	1	; 1 = 鳴らしている
_g_psg_top::	.ds	2	; 曲の PSG のパート(無ければ 0)
fm_v5:		.ds	1	; 曲が ch5 に書いた音色・音量(レジスタ 0x35)。効果音が ch5 を返すときに戻す
fse_on:		.ds	1	; 1 = FM の効果音が ch5 を使っている(曲の ch5 は書かない)
fse_p:		.ds	2	; 効果音の次の段
fse_t:		.ds	1	; 今の段の残りティック(ここから 7 バイトは段の並びと同じ順。段を読むときにまとめて写す)
fse_iv:		.ds	1	; 音色 << 4 | 音量(0 が最大、15 が無音)
fse_f:		.ds	2	; F ナンバー << 7(bit15 が立つように保つ。下がって 0x8000 を割ったら 2 倍してブロックを 1 つ下げる)
fse_blk:	.ds	1	; ブロック
fse_k:		.ds	1	; 毎ティック F ナンバーを 1/2^k 下げる(0 = 下げない)
fse_vm:		.ds	1	; 残りティック & fse_vm が 0 のティックに音量を 1 下げる(0 = 下げない)

	.area	_CODE

; 音程の表: 1 オクターブ 12 音の F ナンバーの下位 8 ビット(上位の bit8 はどれも 1)。MIDI の音番号 n は、n - 19 を 12 で割った
; 商がブロック、余りが表の位置(MIDI 24〜107 = ブロック 0〜7。もとは 84 音ぶんの表だったのを、同じ値になる形にした)。
; F ナンバー = 周波数 × 2^(19 - ブロック) / (3579545 / 72)、512 未満になる一番小さいブロック
fm_ft12:
	.db	0x02, 0x12, 0x22, 0x33, 0x46, 0x59, 0x6D, 0x83, 0x9A, 0xB3, 0xCC, 0xE8

; リズムモードの準備: ch6〜8 の F ナンバー(YM2413 の推奨値)と音量(中くらいから。曲が 0x84 で変える)
rhy_init:
	.db	0x16,0x20, 0x17,0x50, 0x18,0xC0, 0x26,0x05, 0x27,0x05, 0x28,0x01
	.db	0x36,0x04, 0x37,0x44, 0x38,0x44, 0x0E,0x20

; ---------------------------------------------------------------------------
; void fm_play(u8 song)   A = 曲の番号(songs.h の SONG_。0 = 止める)— 曲の見出しを読む(ページ2 = SEG_SND)
; ---------------------------------------------------------------------------
_fm_play::
	push	af
	call	_fm_stop
	xor	a, a
	ld	(fm_nmel), a
	ld	(fm_rhy), a
	ld	(_g_psg_top), a
	ld	(_g_psg_top + 1), a
	pop	af
	or	a, a
	ret	z
	push	ix
	dec	a
	add	a, a
	ld	e, a
	ld	d, #0
	ld	hl, #SONG_TAB + 1	; 曲の表
	add	hl, de
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	ex	de, hl			; HL = 曲の見出し
	ld	a, (hl)
	inc	hl
	ld	(fm_nmel), a
	ld	b, a
	ld	a, (hl)
	inc	hl
	ld	c, a
	ld	(fm_flags), a
	and	a, #1
	ld	(fm_rhy), a		; bit0 = リズム
	bit	1, c
	jr	z, 6$
	; bit1 = オリジナル音色(音色 0。YM2413 のレジスタ 0x00〜0x07)
	call	fm_voice
6$:
	ld	ix, #fm_ch
	ld	a, b
	or	a, a
	jr	z, 3$
2$:
	call	ch_init
	djnz	2$
3$:
	ld	a, (fm_rhy)
	or	a, a
	jr	z, 5$
	ld	ix, #fm_ch + FMC_SIZE * FM_MEL
	call	ch_init
	ld	(fm_hdr), hl
	ld	hl, #rhy_init
	ld	b, #10
4$:
	ld	a, (hl)
	inc	hl
	push	hl
	ld	l, (hl)
	call	_fm_write
	pop	hl
	inc	hl
	djnz	4$
	; リズムの初期化の表を読んだので、見出しの続きの番地を戻す
	ld	hl, (fm_hdr)
5$:
	ld	a, (fm_flags)		; bit2 = PSG のパート(見出しの最後の番地)
	bit	2, a
	jr	z, 7$
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	ld	(_g_psg_top), de
7$:
	ld	a, #1
	ld	(_g_bgm_on), a
	pop	ix
	ret

; ch_init: HL = チャンネルのデータの番地の番地、IX = チャンネル → 初期化して HL・IX を次へ
ch_init:
	ld	e, (hl)
	inc	hl
	ld	d, (hl)
	inc	hl
	ld	FMC_P (ix), e
	ld	FMC_P + 1 (ix), d
	ld	FMC_TOP (ix), e
	ld	FMC_TOP + 1 (ix), d
	xor	a, a
	ld	FMC_LOOP (ix), a
	ld	FMC_LOOP + 1 (ix), a
	ld	FMC_GATE (ix), a
	ld	FMC_HI (ix), a
	ld	FMC_ON (ix), a
	ld	FMC_RSP (ix), a
	ld	FMC_DET (ix), a
	inc	a
	ld	FMC_WAIT (ix), a
	ld	de, #FMC_SIZE
	add	ix, de
	ret

; ---------------------------------------------------------------------------
; void fm_stop(void) — 止めて、FM の全チャンネルをキーオフ(リズムモードも切る)
; ---------------------------------------------------------------------------
_fm_stop::
	xor	a, a
	ld	(_g_bgm_on), a
	ld	b, #9
	ld	c, #0x20
1$:
	ld	a, c
	ld	l, #0
	call	_fm_write
	inc	c
	djnz	1$
	ld	a, #0x0E
	ld	l, #0
	jp	_fm_write

; ---------------------------------------------------------------------------
; void fm_bgm_tick(void) — 1 ティック進める(sound_tick から。ページ2 = SEG_SND)
; ---------------------------------------------------------------------------
_fm_bgm_tick::
	ld	a, (_g_bgm_on)
	or	a, a
	ret	z
	push	ix
	ld	ix, #fm_ch
	ld	a, (fm_nmel)
	or	a, a
	jr	z, 8$
	ld	b, a
	ld	c, #0			; C = FM のチャンネル
1$:
	push	bc
	ld	a, FMC_P (ix)
	or	a, FMC_P + 1 (ix)
	call	nz, mel_tick
	pop	bc
	ld	de, #FMC_SIZE
	add	ix, de
	inc	c
	djnz	1$
8$:
	ld	a, (fm_rhy)
	or	a, a
	jr	z, 9$
	ld	ix, #fm_ch + FMC_SIZE * FM_MEL
	ld	a, FMC_P (ix)
	or	a, FMC_P + 1 (ix)
	call	nz, rhy_tick
9$:
	pop	ix
	ret

; mel_tick: IX = チャンネル、C = FM のチャンネル番号
mel_tick:
	dec	FMC_WAIT (ix)
	jr	z, 10$
	; 音を切る位置(次が「伸ばす」なら切らない)
	ld	a, FMC_ON (ix)
	or	a, a
	ret	z
	ld	a, FMC_GATE (ix)
	or	a, a
	ret	z
	cp	a, FMC_WAIT (ix)
	ret	nz
	ld	l, FMC_P (ix)
	ld	h, FMC_P + 1 (ix)
	ld	a, (hl)
	cp	a, #0x85
	ret	z
	jp	key_off
10$:
	ld	l, FMC_P (ix)
	ld	h, FMC_P + 1 (ix)
11$:
	ld	a, (hl)
	inc	hl
	bit	7, a
	jr	nz, 20$
	; 音: A = MIDI の音番号
	ld	e, (hl)
	inc	hl
	ld	FMC_WAIT (ix), e
	ld	FMC_P (ix), l
	ld	FMC_P + 1 (ix), h
	cp	a, #24
	jr	nc, 12$
	ld	a, #24
12$:
	cp	a, #108
	jr	c, 13$
	ld	a, #107
13$:
	sub	a, #19
	ld	d, #0xFF
14$:
	inc	d			; D = ブロック << 1 | 1(F ナンバーの bit8)
	inc	d
	sub	a, #12
	jr	nc, 14$
	add	a, #12
	ld	hl, #fm_ft12
	add	a, l
	ld	l, a
	adc	a, h
	sub	a, l
	ld	h, a
	ld	e, (hl)			; E = F ナンバーの下位、D = 上位とブロック
	ld	a, FMC_DET (ix)		; 音程のずらし(符号つき)を F ナンバーに足す
	ld	l, a
	add	a, a
	sbc	a, a			; 上位(0 か 0xFF)
	ld	h, a
	ld	a, e
	add	a, l
	ld	e, a
	ld	a, d
	adc	a, h
	ld	d, a
	ld	FMC_HI (ix), d
	ld	a, c
	add	a, #0x10
	ld	l, e
	call	chw
	ld	a, c
	add	a, #0x20
	ld	l, d
	call	chw		; キーオフしてから
	ld	a, c
	add	a, #0x20
	ld	l, d
	set	4, l
	call	chw		; キーオン(エンベロープを頭から)
	ld	FMC_ON (ix), #1
	ret
20$:
	cp	a, #0x80
	jr	nz, 21$
	ld	a, (hl)			; 休み
	inc	hl
	ld	FMC_WAIT (ix), a
	ld	FMC_P (ix), l
	ld	FMC_P + 1 (ix), h
	jr	key_off
21$:
	cp	a, #0x81
	jr	nz, 22$
	ld	a, c			; 音色・音量
	add	a, #0x30
	push	hl
	ld	l, (hl)
	call	chw
	pop	hl
	inc	hl
	jp	11$
22$:
	cp	a, #0x82
	jr	nz, 23$
	ld	a, (hl)
	inc	hl
	ld	FMC_GATE (ix), a
	jp	11$
23$:
	cp	a, #0x83
	jr	nz, 24$
	ld	FMC_LOOP (ix), l
	ld	FMC_LOOP + 1 (ix), h
	jp	11$
24$:
	cp	a, #0x85
	jr	nz, 27$
	ld	a, (hl)			; 今の音を伸ばす
	inc	hl
	ld	FMC_WAIT (ix), a
	ld	FMC_P (ix), l
	ld	FMC_P + 1 (ix), h
	ret
27$:
	cp	a, #0x86
	jr	nz, 28$
	call	fm_voice		; オリジナル音色を書き換える(曲の途中で替える)
	jp	11$
28$:
	cp	a, #0x8A
	jr	nz, 29$
	ld	a, (hl)			; 音程のずらし
	inc	hl
	ld	FMC_DET (ix), a
	jp	11$
29$:
	call	rep_op			; 0x87 / 0x89: くり返し
	jp	nc, 11$
25$:
	cp	a, #0xFF
	jr	nz, 26$
	ld	FMC_RSP (ix), #0
	ld	l, FMC_LOOP (ix)
	ld	h, FMC_LOOP + 1 (ix)
	ld	a, h
	or	a, l
	jp	nz, 11$
	ld	l, FMC_TOP (ix)
	ld	h, FMC_TOP + 1 (ix)
	jp	11$
26$:
	xor	a, a			; 0xFE: 終わり
	ld	FMC_P (ix), a
	ld	FMC_P + 1 (ix), a
key_off:
	ld	a, FMC_ON (ix)
	or	a, a
	ret	z
	ld	FMC_ON (ix), #0
	ld	a, c
	add	a, #0x20
	ld	l, FMC_HI (ix)
	jp	chw

; chw: 曲のメロディの書き込み(A = レジスタ、L = 値、C = チャンネル)。FM の効果音が ch5 を使っている間は ch5 に書かない。
;   ch5 の音色・音量(0x35)は覚えておき、効果音が終わったら戻す
chw:
	push	af
	cp	a, #0x35
	jr	nz, 1$
	ld	a, l
	ld	(fm_v5), a
1$:
	ld	a, c
	cp	a, #5
	jr	nz, 2$
	ld	a, (fse_on)
	or	a, a
	jr	nz, 3$
2$:
	pop	af
	jp	_fm_write
3$:
	pop	af
	ret

; rhy_tick: IX = リズムのチャンネル
rhy_tick:
	dec	FMC_WAIT (ix)
	ret	nz
	ld	l, FMC_P (ix)
	ld	h, FMC_P + 1 (ix)
31$:
	ld	a, (hl)
	inc	hl
	bit	7, a
	jr	nz, 40$
	; 打つ(いったん全部離してから)
	push	af
	push	hl
	ld	a, #0x0E
	ld	l, #0x20
	call	_fm_write
	pop	hl
	pop	af
	ld	(_g_drm), a		; 打った音(スネア・シンバル・ハイハットに PSG のノイズを重ねる。sndtick.c)
	or	a, #0x20
	push	hl
	ld	l, a
	ld	a, #0x0E
	call	_fm_write
	pop	hl
	ld	a, (hl)
	inc	hl
	ld	FMC_WAIT (ix), a
	jr	39$
40$:
	cp	a, #0x80
	jr	nz, 41$
	ld	a, (hl)			; 休み
	inc	hl
	ld	FMC_WAIT (ix), a
	push	hl
	ld	a, #0x0E
	ld	l, #0x20
	call	_fm_write
	pop	hl
	jr	39$
41$:
	cp	a, #0x84
	jr	nz, 42$
	ld	a, #0x36		; 音量
	call	rv
	ld	a, (hl)			; ハイハットとスネアの音量(上位 4 ビットがハイハットの減衰)。PSG のノイズの強さに使う(sndtick.c)
	ld	(_g_hha), a
	ld	a, #0x37
	call	rv
	ld	a, #0x38
	call	rv
	jr	31$
42$:
	cp	a, #0x83
	jr	nz, 43$
	ld	FMC_LOOP (ix), l
	ld	FMC_LOOP + 1 (ix), h
	jr	31$
43$:
	call	rep_op			; 0x87 / 0x89: くり返し
	jr	nc, 31$
	cp	a, #0xFF
	jr	nz, 44$
	ld	FMC_RSP (ix), #0
	ld	l, FMC_LOOP (ix)
	ld	h, FMC_LOOP + 1 (ix)
	ld	a, h
	or	a, l
	jr	nz, 31$
	ld	l, FMC_TOP (ix)
	ld	h, FMC_TOP + 1 (ix)
	jr	31$
44$:
	xor	a, a			; 0xFE: 終わり
	ld	FMC_P (ix), a
	ld	FMC_P + 1 (ix), a
	ld	a, #0x0E
	ld	l, #0x20
	jp	_fm_write
39$:
	ld	FMC_P (ix), l
	ld	FMC_P + 1 (ix), h
	ret

; rep_op: A = 命令、HL = 次のバイト、IX = チャンネル。0x87 n(くり返しの始め、n 回)と 0x89(くり返しの終わり)なら処理して
;   キャリー 0 で戻る(HL = 次に読む所)。それ以外はキャリー 1 で戻る(A・HL はそのまま)。BC は保つ
rep_op:
	cp	a, #0x87
	jr	z, 1$
	cp	a, #0x89
	jr	z, 3$
	scf
	ret
1$:
	ld	e, (hl)			; 回数
	inc	hl
	ld	a, FMC_RSP (ix)
	inc	FMC_RSP (ix)
	or	a, a
	jr	nz, 2$
	ld	FMC_R0P (ix), l
	ld	FMC_R0P + 1 (ix), h
	ld	FMC_R0N (ix), e
	or	a, a			; キャリー 0
	ret
2$:
	ld	FMC_R1P (ix), l
	ld	FMC_R1P + 1 (ix), h
	ld	FMC_R1N (ix), e
	or	a, a
	ret
3$:
	ld	a, FMC_RSP (ix)
	cp	a, #2
	jr	z, 5$
	dec	FMC_R0N (ix)		; 1 段目
	jr	z, 4$
	ld	l, FMC_R0P (ix)
	ld	h, FMC_R0P + 1 (ix)
	or	a, a
	ret
5$:
	dec	FMC_R1N (ix)		; 2 段目
	jr	z, 4$
	ld	l, FMC_R1P (ix)
	ld	h, FMC_R1P + 1 (ix)
	or	a, a
	ret
4$:
	dec	FMC_RSP (ix)		; 回し終わった: 段を戻して先へ
	or	a, a
	ret

; fm_voice: HL = オリジナル音色の 8 バイト → レジスタ 0x00〜0x07 に書き、HL を次へ。BC は保つ
fm_voice:
	push	bc
	ld	c, #0
1$:
	ld	a, c
	push	hl
	ld	l, (hl)
	call	_fm_write
	pop	hl
	inc	hl
	inc	c
	ld	a, c
	cp	a, #8
	jr	c, 1$
	pop	bc
	ret

; rv: A = レジスタ、HL = 値の番地 → 書いて HL を次へ
rv:
	push	hl
	ld	l, (hl)
	call	_fm_write
	pop	hl
	inc	hl
	ret

; ---------------------------------------------------------------------------
; FM の効果音(落下の「ヒューッ」、着地の「ドシャーン」など。sndtick.c の se_fm_)。FM の ch5 を鳴っている間だけ借りる
;   (曲の ch5 は止まり、終わったら音色・音量を戻す。ch5 を使わない曲もある)。リズムの音も打てる(PSG のドラムも重なる)。
;   段(8 バイト): ティック数, 音色 << 4 | 音量, 0, F ナンバー >> 1(0 = ch5 は鳴らさない), ブロック, k, 音量のマスク, リズム
;   (bit4 バスドラム、bit3 スネア、bit2 タム、bit1 シンバル、bit0 ハイハット)。毎ティック F ナンバーを 1/2^k 下げる(k = 0 は下げない)。
;   音量は残りティック & マスク が 0 のティックに 1 下げる(マスク 0 は下げない)。
;   ティック数 0 = 終わり。ほかの FM の効果音が来たら、そこで切れて入れ替わる
; void fm_se_start(const u8 *p)   HL = 段の並び
; void fm_se_tick(void)
; ---------------------------------------------------------------------------
_fm_se_start::
	ld	(fse_p), hl
	ld	a, #1
	ld	(fse_t), a
	ld	(fse_on), a
	ret

_fm_se_tick::
	ld	a, (fse_on)
	or	a, a
	ret	z
	ld	hl, #fse_t
	dec	(hl)
	jr	nz, 5$
	ld	hl, (fse_p)		; 次の段
	ld	a, (hl)
	or	a, a
	jr	nz, 1$
9$:
	xor	a, a			; 終わり: ch5 を曲に返す
	ld	(fse_on), a
	ld	l, a
	ld	a, #0x25
	call	_fm_write
	ld	a, (fm_v5)
	ld	l, a
	ld	a, #0x35
	jp	_fm_write
1$:
	ld	de, #fse_t
	ld	bc, #7
	ldir
	ld	c, (hl)			; リズム
	inc	hl
	ld	(fse_p), hl
	ld	a, #0x25		; ch5 を離す(段ごとに音の頭から)
	ld	l, b
	call	_fm_write
	ld	a, c
	or	a, a
	jr	z, 6$
	ld	hl, #_g_drm		; PSG のドラムも重ねる(sndtick.c)
	or	a, (hl)
	ld	(hl), a
	ld	a, (fm_rhy)		; リズムモードの曲だけ FM のリズムを打つ
	or	a, a
	jr	z, 6$
	ld	l, #0x20
	call	rhy_w
	ld	a, c
	or	a, #0x20
	ld	l, a
	call	rhy_w
	jr	6$
5$:
	ld	a, (fse_vm)		; 音量を下げる(HL = fse_t)
	or	a, a
	jr	z, 4$
	and	a, (hl)
	jr	nz, 4$
	inc	hl			; fse_iv
	ld	a, (hl)
	cpl
	and	a, #0x0F
	jr	z, 4$
	inc	(hl)
4$:
	ld	a, (fse_k)		; 音程を下げる: F -= F >> k
	or	a, a
	jr	z, 6$
	ld	b, a
	ld	hl, (fse_f)
	ld	d, h
	ld	e, l
3$:
	srl	d
	rr	e
	djnz	3$
	or	a, a
	sbc	hl, de
	bit	7, h
	jr	nz, 7$
	add	hl, hl			; 下のブロックへ
	ex	de, hl
	ld	hl, #fse_blk
	dec	(hl)
	ex	de, hl
7$:
	ld	(fse_f), hl
6$:
	ld	a, (fse_iv)
	ld	l, a
	ld	a, #0x35
	call	_fm_write
	ld	hl, (fse_f)
	ld	a, h
	or	a, a
	ret	z			; 鳴らさない段
	add	hl, hl			; H = F ナンバーの下位 8 ビット
	ld	l, h
	ld	a, #0x15
	call	_fm_write
	ld	a, (fse_blk)
	add	a, a
	or	a, #0x11		; キーオン、F ナンバーの bit8
	ld	l, a
	ld	a, #0x25
	jp	_fm_write

rhy_w:
	ld	a, #0x0E
	jp	_fm_write
