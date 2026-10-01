/* fillbench.c — 「詰めたバッファ(1 バイト = 横 2 ブロック)へ塗ると速いか」を確かめるベンチ(make fillbench の ROM だけ)
 *
 * 実際のゲームで記録した塗り(raster.s の fill)の呼び出しを、いまの fill と、詰めたバッファへ塗る pfill の両方で
 * 再生して時間を比べる。呼び出しの列は計測スクリプトが 0x9000 から書き込み、fb_state = 1 で始める。
 *   1 件 = r0, 行数 n, 色, そのあと n 行分の (span_a, span_b)
 * 結果(ターボR のシステムタイマのカウント、3.911µs):
 *   fb_res[0] スパンを置くだけ(差し引く分)  [1] + fill  [2] + pfill
 *   fb_mis    両方を塗り終えた絵が一致しないバイトの数(0 であること)
 *
 * 詰めたバッファは VRAM と同じ並び(Z80 が OUTI で頭から送れる形): 8 行の帯 × 32 列ペア × 8 行。
 *   番地 = PBUF + (行 >> 3) * 256 + 列ペア * 8 + (行 & 7)。上位 4bit が左のブロック
 *   横に隣り合うバイトは 8 バイト離れるので、1 バイト書くごとに add hl,bc(BC = 8)で進む
 */
#include "engine.h"

#ifdef FILLBENCH

#define FB_LIST   ((const u8 *)0x9000)
#define PBUF      ((u8 *)0x8000)
#define PBLK      0x8600            /* pfill の展開ブロック(写して使う。256 境界の中) */

volatile u8 fb_state;               /* 0 待ち 1 開始 2 終わり */
u16 fb_n;                           /* 呼び出しの件数 */
u16 fb_res[3];
u16 fb_mis;

/* pfill の作業用 */
u8 pf_end, pf_lo, pf_hi, pf_w;

static void call_fill(u8 r0, u8 n) __naked
{
    (void)r0; (void)n;
    __asm
    ld  c, a
    ld  b, l
    jp  fill
    __endasm;
}

static void call_none(u8 r0, u8 n) __naked
{
    (void)r0; (void)n;
    __asm
    ret
    __endasm;
}

/* 詰めたバッファへ塗る。C = r0、B = 行数、色は g_color。行ごとの左右端は fill と同じく span_a / span_b
 *   fill と同じ組み立て(1 行ごとに左右を決め、展開した書き込みの途中へ飛ぶ)にし、違いは
 *   「奇数の端の半バイト」と「1 バイトごとに +8」だけにしてある */
static void pfill(u8 r0, u8 n) __naked
{
    (void)r0; (void)n;
    __asm
    ld  c, a
    ld  b, l
    ld  a, c
    add a, b
    ld  (_pf_end), a
    ld  a, (_g_color)
    and a, #0x0F
    ld  (_pf_lo), a
    ld  b, a
    add a, a
    add a, a
    add a, a
    add a, a
    ld  (_pf_hi), a
    or  a, b
    ld  (PBLK + 1), a                     ; 両方のブロックが同じ色のバイト
    ld  d, #0xDE                          ; span_a
    ld  e, c
pf_row:
    inc d
    ld  a, (de)                           ; span_b
    ld  c, a
    dec d
    ld  a, (de)                           ; span_a
    sub a, c
    jr  z, pf_next
    jr  c, pf_lt
    ld  b, a                              ; 幅 = A、左端 = C
    jr  pf_go
pf_lt:
    neg
    ld  b, a
    ld  a, c
    sub a, b
    ld  c, a
pf_go:
    ; H = PBUF の上位 + (行 >> 3)、L = (左端 >> 1) * 8 + (行 & 7)
    ld  a, e
    rrca
    rrca
    rrca
    and a, #0x1F
    add a, #0x80
    ld  h, a
    ld  a, c
    and a, #0xFE
    add a, a
    add a, a
    ld  l, a
    ld  a, e
    and a, #7
    or  a, l
    ld  l, a
    bit 0, c
    jr  z, pf_even
    ; 左端が奇数: そのバイトの右半分だけ
    ld  a, (hl)
    and a, #0xF0
    ld  c, a
    ld  a, (_pf_lo)
    or  a, c
    ld  (hl), a
    ld  a, l
    add a, #8
    ld  l, a
    dec b
    jr  z, pf_next
pf_even:
    ld  a, b
    ld  (_pf_w), a
    srl a                                 ; 丸ごと塗るバイトの数
    jr  z, pf_tail
    add a, a
    ld  b, a
    ld  a, #<(PBLK + 5 + 64)
    sub a, b
    ld  (PBLK + 3), a
    ld  bc, #8
    jp  PBLK
_pf_ret::
pf_tail:
    ld  a, (_pf_w)
    rrca
    jr  nc, pf_next
    ; 右端が奇数: そのバイトの左半分だけ
    ld  a, (hl)
    and a, #0x0F
    ld  c, a
    ld  a, (_pf_hi)
    or  a, c
    ld  (hl), a
pf_next:
    inc e
    ld  a, (_pf_end)
    cp  a, e
    jp  nz, pf_row
    ret

    ; 展開ブロックの元(PBLK へ写す)
_pblk_tpl::
    ld  a, #0
    jp  PBLK
    .rept 32
    ld  (hl), a
    add hl, bc
    .endm
    jp  _pf_ret
_pblk_end::
    __endasm;
}

extern const u8 pblk_tpl[], pblk_end[];

typedef void (*FillFn)(u8 r0, u8 n);

static u16 replay(FillFn fn)
{
    const u8 *p = FB_LIST;
    u16 i, t0;
    timer_reset();
    t0 = timer_read();
    for (i = 0; i < fb_n; i++) {
        u8 r0 = p[0], n = p[1], k;
        u8 *sa = (u8 *)0xDE00 + r0, *sb = (u8 *)0xDF00 + r0;
        g_color = p[2];
        p += 3;
        for (k = 0; k < n; k++) { sa[k] = p[0]; sb[k] = p[1]; p += 2; }
        fn(r0, n);
    }
    return timer_read() - t0;
}

static void clear(u8 *d, u16 n) { while (n--) *d++ = 0; }

void fill_bench(void)
{
    u16 i;
    u8 r, c;
    for (i = 0; i < (u16)(pblk_end - pblk_tpl); i++) ((u8 *)PBLK)[i] = pblk_tpl[i];
    g_buf_hi = BUF_A_HI;
    for (;;) {
        while (fb_state != 1) ;
        __asm__("di");
        clear(BUF, 64 * 48);
        clear(PBUF, 1536);
        fb_res[0] = replay(call_none);
        fb_res[1] = replay(call_fill);
        fb_res[2] = replay((FillFn)pfill);
        __asm__("ei");
        fb_mis = 0;
        for (r = 0; r < GAME_ROWS; r++)
            for (c = 0; c < 32; c++) {
                const u8 *s = BUF + (u16)r * 64 + c * 2;
                if (PBUF[(u16)(r >> 3) * 256 + c * 8 + (r & 7)] != (u8)((s[0] << 4) | (s[1] & 0x0F))) fb_mis++;
            }
        fb_state = 2;
    }
}

#endif
