; modhead_scene.s — 場面のモジュールの先頭(ページ2 = マッパーのセグメント 10、0x8000)
;   scene.c を 0x8000 に合わせてリンクし、ROM のバンク 40〜41 に置く。起動時に 1 回セグメント 10 へ読み込む。
;   常駐部の vdp_init / ending_show / gameover_show / config_show(sys.s)がページ2を切り替えてここを呼ぶ。
	.module	modhead_scene
	.globl	_scm_vdp_init, _scm_ending, _scm_gameover, _scm_config

	.area	_HEADER (ABS)
	.org	0x8000
	jp	_scm_vdp_init		; void vdp_init(void)
	jp	_scm_ending		; void ending_show(void)
	jp	_scm_gameover		; void gameover_show(void)
	jp	_scm_config		; void config_show(void)

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
