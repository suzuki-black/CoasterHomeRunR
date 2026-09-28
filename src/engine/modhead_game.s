; modhead_game.s — ゲームモジュールの先頭(ページ2 = マッパーのセグメント 1、0x8000)
;   ステージごとに game.c・models.c・そのステージのデータを 0x8000 に合わせてリンクし、ROM の 2 バンクに置く。
;   常駐部(main.c)はステージの始めにこれを読み込み(mod_load)、0x8003 の Stage へのポインタを見て、0x8000 を呼ぶ。
	.module	modhead_game
	.globl	_game_run, _stage_data

	.area	_HEADER (ABS)
	.org	0x8000
	jp	_game_run		; u8 game_run(const Stage *st, u8 first)
	.dw	_stage_data

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
