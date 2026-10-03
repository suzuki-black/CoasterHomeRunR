/* hud.c — 描画バッファへの文字・ゲージの上書き */
#include "hud.h"

static void block(i8 x, i8 y, u8 color, u8 scale)
{
    u8 i, j;
    for (j = 0; j < scale; j++) {
        i8 yy = y + j;
        if (yy < 0 || yy >= 48) continue;
        for (i = 0; i < scale; i++) {
            i8 xx = x + i;
            if (xx < 0 || xx >= 64) continue;
            g_rx = (u8)xx; g_ry = (u8)yy; g_color = color;
            pset();
        }
    }
}

void glyph1(const u8 *g);    /* gfx.s: 倍率 1 の字形を g_rx, g_ry に描く */

static void char_draw(i8 x, i8 y, char ch, u8 color, u8 scale)
{
    u8 n, r, bits, c;
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
    for (r = 0; r < 5; r++) {
        bits = g[r];
        for (c = 0; c < 3; c++) {
            if (bits & (4 >> c)) block(x + c * scale, y + r * scale, color, scale);
        }
    }
}

void text_draw(i8 x, i8 y, const char *s, u8 color, u8 scale)
{
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

void num_draw(i8 x, i8 y, u16 n, u8 digits, u8 color)
{
    char buf[6];
    u8 i = 5;
    buf[5] = 0;
    do {
        buf[--i] = '0' + (n % 10);
        n /= 10;
    } while (n && i);
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
        g_color = (i < life) ? C_YELLOW : 9;     /* 積んだ宝の量(金) */
        rect_fill();
    }
}
