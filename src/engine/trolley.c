/* trolley.c — 画面下部のコースターの車両(自機・手は描かない)
 *
 * 乗り物はジェットコースターの一人乗りの先頭車両(飛び出したときに後ろの車両との連結が外れた。仕様書 1 章)。
 * 絵は tools/gen_data.py の coaster_front / coaster_back(64×13、0 = 透過)。前を向いているときは前の絵、
 * 後ろを向いているときは後ろの絵。ライフ(車両の耐久)が減るほど壊れていく: 壊れ具合の点(coaster_dmg_*。
 * (x, y, (現れる耐久 << 4) | 色)、0xFF で終わり。色 0 は穴)のうち、耐久が「現れる耐久」より小さいものを重ねる。
 */
#include "trolley.h"

extern const u8 coaster_front[TROLLEY_W * TROLLEY_H];
extern const u8 coaster_back[TROLLEY_W * TROLLEY_H];
extern const u8 coaster_dmg_front[];
extern const u8 coaster_dmg_back[];
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
static u8 built_back;
static i8 last_x;
static u8 last_y;

void trolley_set(u8 life, u8 back)
{
    u16 k;
    const u8 *src, *d;
    if (life == built_life && back == built_back) return;
    built_life = life;
    built_back = back;
    src = back ? coaster_back : coaster_front;
    for (k = 0; k < sizeof(timg); k++) timg[k] = src[k];
    /* 壊れ具合: 耐久が「現れる耐久」より小さい点を重ねる(色 0 は穴) */
    for (d = back ? coaster_dmg_back : coaster_dmg_front; d[0] != 0xFF; d += 3) {
        if (life < (d[2] >> 4)) timg[(u16)d[1] * TROLLEY_W + d[0]] = d[2] & 0x0F;
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
