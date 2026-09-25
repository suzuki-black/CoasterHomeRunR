/* trolley.c — 画面下部のトロッコの縁(自機・手は描かない)
 *
 * 絵は tools/gen_data.py の trolley_img(64×9、0 = 透過)。ライフが 1 減るごとに
 * trolley_dmg の 1 段(穴 = 透過にする / ひび = 黒)を累積で当て、トロッコが徐々に壊れていく。
 * 穴からは奥の景色が見える。
 */
#include "trolley.h"

extern const u8 trolley_img[TROLLEY_W * TROLLEY_H];
extern const u8 trolley_dmg[];
extern u8 *g_bt_src, *g_bt_dst;
void blit_trans(u8 count);

static u8 timg[TROLLEY_W * TROLLEY_H];
static u8 built_life = 0xFF;
static i8 last_x;
static u8 last_y;

void trolley_set_life(u8 life)
{
    u8 level, i;
    const u8 *d = trolley_dmg;
    u16 k;
    if (life == built_life) return;
    built_life = life;
    for (k = 0; k < sizeof(timg); k++) timg[k] = trolley_img[k];
    level = (life >= 8) ? 0 : 8 - life;
    for (i = 0; i < level; i++) {
        for (; *d != 0xFF; d += 5) {
            u8 x, y;
            for (y = d[1]; y < d[1] + d[3] && y < TROLLEY_H; y++) {
                for (x = d[0]; x < d[0] + d[2] && x < TROLLEY_W; x++) {
                    u8 *p = &timg[y * TROLLEY_W + x];
                    if (d[4] == 0) *p = 0;
                    else if (*p) *p = d[4];
                }
            }
        }
        d++;                               /* 段の区切り */
    }
}

void trolley_draw(i8 xoff, u8 shake)
{
    u8 y, rows;
    i8 x0, x1;
    last_x = xoff;
    last_y = TROLLEY_Y + shake;
    if (xoff <= -64 || xoff >= 64) return;
    x0 = xoff < 0 ? -xoff : 0;             /* 絵の中で写し始める列 */
    x1 = xoff > 0 ? 64 - xoff : 64;
    rows = TROLLEY_H - shake;
    for (y = 0; y < rows; y++) {
        g_bt_src = &timg[y * TROLLEY_W + x0];
        g_bt_dst = BUF + (u16)(last_y + y) * 64 + (u8)(x0 + xoff);
        blit_trans((u8)(x1 - x0));
    }
}

u8 trolley_covers(u8 bx, u8 by)
{
    i8 x = (i8)bx - last_x;
    if (by < last_y || by >= GAME_ROWS || x < 0 || x >= TROLLEY_W) return 0;
    return timg[(by - last_y) * TROLLEY_W + (u8)x] != 0;
}
