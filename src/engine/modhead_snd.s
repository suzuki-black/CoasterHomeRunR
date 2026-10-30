; modhead_snd.s — 音のモジュールの先頭(ページ2 = マッパーのセグメント 8、0x8000)
;   sndtick.c・sndfm.s を 0x8000 に合わせてリンクし、曲のデータ(tools/gen_music.py)を 0x8A40 から後ろに置く(tools/mkrom.py)。
;   起動時に 1 回セグメント 8 へ読み込む。割り込み(sys.s)はページ2をセグメント 8 にしてから 0x8000 を呼び、
;   常駐部の bgm_play / bgm_stop(sys.s)はページ2を切り替えて 0x8003 / 0x8006 を呼ぶ。
	.module	modhead_snd
	.globl	_sndm_tick, _sndm_play, _sndm_stop

	.area	_HEADER (ABS)
	.org	0x8000
	jp	_sndm_tick		; void sndm_tick(void)
	jp	_sndm_play		; void sndm_play(u8 song)   A = 曲の番号
	jp	_sndm_stop		; void sndm_stop(void)

	; 区画の並び(crt0.s と同じ): コードと定数を先に、変数(0xBC00-)を後に置く
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
