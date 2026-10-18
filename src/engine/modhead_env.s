; modhead_env.s — 地形モジュールの先頭(ページ2 = マッパーのセグメント 5、0x8000)
;   env.c をステージごとに(ENV_S1〜ENV_S6)0x8000 に合わせてリンクし、ROM の 2 バンクずつに置く。ステージの始めにセグメント 5 へ読み込む。
;   常駐部の env_build / env_draw(sys.s)がページ2を切り替えてここを呼ぶ。
	.module	modhead_env
	.globl	_envm_build, _envm_draw

	.area	_HEADER (ABS)
	.org	0x8000
	jp	_envm_build		; void envm_build(void)
	jp	_envm_draw		; void envm_draw(u16 s, u8 look)

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
