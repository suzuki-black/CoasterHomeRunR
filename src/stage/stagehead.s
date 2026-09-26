; stagehead.s — ステージデータの先頭(読み込み先 0xB400)に Stage 構造体へのポインタを置く
;
; ステージは C の定数だけで書き、読み込み先の番地(0xB400)に合わせて単独でリンクする。
; tools/mkrom.py が ROM のバンクに置き、エンジンは rom_load で RAM へ写してから、
; 先頭 2 バイトのポインタで Stage を引く(main.c の stage_load)。
	.module	stagehead
	.globl	_stage_data

	.area	_HEADER (ABS)
	.org	0xB400
	.dw	_stage_data

	.area	_CODE
