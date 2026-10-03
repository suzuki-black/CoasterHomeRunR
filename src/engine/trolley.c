/* trolley.c — 画面下部のトロッコの縁(自機・手は描かない)と、縁の上に積んだ宝の山
 *
 * 絵は tools/gen_data.py の trolley_img(64×13、0 = 透過。上の 4 行は空き)。ライフ = 積んだ宝の量で、
 * trolley_pile の点のうち「見えるのに要るライフ」が今のライフ以下のものだけを重ねる。
 * 被弾するたびに宝の山が外側・上から減っていく(金貨が飛び散る演出は game.c)。
 */
#include "trolley.h"

extern const u8 trolley_img[TROLLEY_W * TROLLEY_H];
extern const u8 trolley_pile[TROLLEY_W * PILE_H];
extern const u8 *g_bt_src;
extern u8 g_bt_x, g_bt_y;
void blit_trans(u8 count);
void runs_draw(const u8 *tbl, u16 y0);

static u8 timg[TROLLEY_W * TROLLEY_H];
/* まっすぐ前(横ずれ 0)のときに使う、詰めた形(gfx.s の描画バッファの並び)の表。5 バイトずつ、行 = 0xFF で終わり:
 *   (行, 列ペア, n, 元の下位, 元の上位)   n >= 1: 丸ごと不透明なバイト n 個(値は pimg)を写す
 *   (行, 列ペア, 0, 残すビット, 値)      片側だけ不透明なバイト */
#define MAX_RUNS 120
static u8 runs[MAX_RUNS * 5 + 1];
static u8 pimg[TROLLEY_H * 32];     /* 丸ごと不透明なバイトの詰めた値 */
static u8 built_life = 0xFF;
static i8 last_x;
static u8 last_y;

void trolley_set_life(u8 life)
{
    u16 k;
    u8 lv = life << 4;
    if (life == built_life) return;
    built_life = life;
    for (k = 0; k < sizeof(timg); k++) timg[k] = trolley_img[k];
    for (k = 0; k < TROLLEY_W * PILE_H; k++) {
        u8 c = trolley_pile[k];
        if (c && (c & 0xF0) <= lv) timg[k] = c & 0x0F;
    }
    /* 詰めた形の表を作る */
    {
        u8 y, c, n = 0, *r = runs;
        for (y = 0; y < TROLLEY_H; y++) {
            const u8 *row = &timg[y * TROLLEY_W];
            u8 *pv = &pimg[y * 32];
            for (c = 0; c < 32 && n < MAX_RUNS; ) {
                u8 l = row[c * 2], rr = row[c * 2 + 1];
                if (l && rr) {
                    u8 c0 = c;
                    while (c < 32 && row[c * 2] && row[c * 2 + 1]) {
                        pv[c] = (u8)((row[c * 2] << 4) | row[c * 2 + 1]);
                        c++;
                    }
                    r[0] = y; r[1] = c0; r[2] = c - c0;
                    *(u8 **)&r[3] = &pv[c0];
                } else if (l || rr) {
                    r[0] = y; r[1] = c; r[2] = 0;
                    r[3] = l ? 0x0F : 0xF0;                  /* 残すビット */
                    r[4] = l ? (u8)(l << 4) : rr;            /* 値 */
                    c++;
                } else {
                    c++;
                    continue;
                }
                r += 5; n++;
            }
        }
        *r = 0xFF;
    }
}

void trolley_draw(i8 xoff, u8 shake)
{
    u8 y, rows;
    i8 x0, x1;
    last_x = xoff;
    last_y = TROLLEY_Y + shake;
    if (xoff <= -64 || xoff >= 64) return;
    if (xoff == 0) {
        g_rh = TROLLEY_H - shake;          /* 揺れで下にはみ出す行は描かない */
        runs_draw(runs, last_y);
        return;
    }
    x0 = xoff < 0 ? -xoff : 0;             /* 絵の中で写し始める列 */
    x1 = xoff > 0 ? 64 - xoff : 64;
    rows = TROLLEY_H - shake;
    for (y = 0; y < rows; y++) {
        g_bt_src = &timg[y * TROLLEY_W + x0];
        g_bt_x = (u8)(x0 + xoff);
        g_bt_y = last_y + y;
        blit_trans((u8)(x1 - x0));
    }
}

u8 trolley_covers(u8 bx, u8 by)
{
    i8 x = (i8)bx - last_x;
    if (by < last_y || by >= GAME_ROWS || x < 0 || x >= TROLLEY_W) return 0;
    return timg[(by - last_y) * TROLLEY_W + (u8)x] != 0;
}
