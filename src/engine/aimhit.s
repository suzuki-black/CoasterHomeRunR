; aimhit.s — 照準ブロックに重なる当たり判定の矩形のうち、最も手前のものを探す(game.c の aim_target。ゲームモジュールに入れる)
;   照準の色(毎フレーム)と射撃で使う。C で書くと変数を IX から読み書きして、敵が多い場面で 1 フレーム 0.5ms かかっていた
;   hits は HitRect {x0, y0, x1, y1, z(16bit、符号つき), id} の 7 バイトずつ(game.h)
	.module	aimhit
	.globl	_hits, _n_hits
	.area	_CODE

; u8 aim_scan(u8 bx, u8 by)   A = bx、L = by → A = 最も手前の矩形の hits の番号(無ければ 0xFF)
_aim_scan::
	ld	c, a			; C = bx
	ld	b, l			; B = by
	ld	a, (_n_hits)
	or	a, a
	jr	z, 9$
	ld	d, a			; D = 残り
	ld	e, #0			; E = 番号
	push	ix
	ld	ix, #_hits
	ld	hl, #0x7FFF		; HL = いちばん手前の z
	ld	a, #0xFF
	ex	af, af'			; A' = いちばん手前の番号
1$:
	ld	a, c
	cp	a, 0 (ix)
	jr	c, 3$			; bx < x0
	cp	a, 2 (ix)
	jr	nc, 3$			; bx >= x1
	ld	a, b
	cp	a, 1 (ix)
	jr	c, 3$			; by < y0
	cp	a, 3 (ix)
	jr	nc, 3$			; by >= y1
	ld	a, 4 (ix)		; z < いちばん手前(符号つき)?
	sub	a, l
	ld	a, 5 (ix)
	sbc	a, h
	jp	po, 2$
	xor	a, #0x80
2$:
	jp	p, 3$
	ld	l, 4 (ix)
	ld	h, 5 (ix)
	ld	a, e
	ex	af, af'
3$:
	push	bc
	ld	bc, #7
	add	ix, bc
	pop	bc
	inc	e
	dec	d
	jr	nz, 1$
	pop	ix
	ex	af, af'
	ret
9$:
	ld	a, #0xFF
	ret
