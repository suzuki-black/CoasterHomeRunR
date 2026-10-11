/* hud.c — 描画バッファへの文字・ゲージの上書き */
#include "hud.h"

void glyph1(const u8 *g);    /* gfx.s: 倍率 1 の字形を g_rx, g_ry に描く */
extern const u8 *g_cb_g;     /* gfx.s の char_big: 字形・左上・倍率(色は g_color) */
extern i8 g_cb_x, g_cb_y;
extern u8 g_cb_s;
void char_big(void);

static void char_draw(i8 x, i8 y, char ch, u8 color, u8 scale)
{
    u8 n;
    const u8 *g;
    if (ch < 0x20 || ch > 0x7F) return;
    n = font35_map[ch - 0x20];
    if (n == 0xFF) return;
    g = &font35[n * 5];
    if (scale == 1 && x >= 0 && x <= 61 && y >= 0 && y <= 43) {
        g_rx = (u8)x; g_ry = (u8)y; g_color = color;
        glyph1(g);
        return;
    }
    /* 拡大した字は、列ごとに縦につながった点を 1 つの長方形で塗る(点ごとに塗ると「WARNING!」で 1 フレーム十数 ms かかっていた)。
     * 塗るのは gfx.s の char_big(C で書くと「WARNING!」で 1 フレーム約 3ms かかっていた) */
    g_cb_g = g; g_cb_x = x; g_cb_y = y; g_cb_s = scale; g_color = color;
    char_big();
}

void text_draw(i8 x, i8 y, const char *s, u8 color, u8 scale)
{
    if (scale == 1 && y >= 0 && y <= 43) {
        /* 倍率 1 で画面に収まる字は、char_draw を通さずに glyph1 を直接呼ぶ(「ENEMY AHEAD!」などで 1 文字ずつの呼び出しが重かった) */
        for (; *s; s++, x += 4) {
            u8 ch = (u8)*s, n;
            if (x < 0 || x > 61) {
                char_draw(x, y, (char)ch, color, 1);
                continue;
            }
            if (ch < 0x20 || ch > 0x7F) continue;
            n = font35_map[ch - 0x20];
            if (n == 0xFF) continue;
            g_rx = (u8)x; g_ry = (u8)y; g_color = color;
            glyph1(&font35[n * 5]);
        }
        return;
    }
    for (; *s; s++) {
        char_draw(x, y, *s, color, scale);
        x += 4 * scale;
    }
}

void text_center(i8 y, const char *s, u8 color, u8 scale)
{
    u8 n = 0;
    const char *p = s;
    while (*p++) n++;
    text_draw((i8)((64 - (i8)(n * 4 * scale - scale)) / 2), y, s, color, scale);
}

/* 10 進の各桁は、割り算(R800 には割り算の命令が無く、ライブラリは 1 回数百サイクル)でなく、
 * 10000・1000・100・10 を引いて求める */
static const u16 pow10[4] = { 10000, 1000, 100, 10 };

void num_draw(i8 x, i8 y, u16 n, u8 digits, u8 color)
{
    char buf[6];
    u8 i, k;
    for (k = 0; k < 4; k++) {
        char c = '0';
        while (n >= pow10[k]) { n -= pow10[k]; c++; }
        buf[k] = c;
    }
    buf[4] = (char)('0' + n);
    buf[5] = 0;
    for (i = 0; i < 4 && buf[i] == '0'; i++) ;   /* 先頭の 0 は出さない(0 は 1 桁の「0」) */
    x += (i8)((digits - (5 - i)) * 4);
    text_draw(x, y, &buf[i], color, 1);
}

void hud_life(u8 life)
{
    u8 i;
    g_rh = 2;
    buf_fill_rows(C_BLACK, 46);
    for (i = 0; i < 8; i++) {
        g_rx = 4 + i * 7; g_ry = 46; g_rw = 6; g_rh = 2;
        g_color = (i < life) ? C_YELLOW : 9;     /* 耐久メーター(車両の耐久) */
        rect_fill();
    }
}
