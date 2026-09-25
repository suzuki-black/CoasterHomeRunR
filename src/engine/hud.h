/* hud.h — ブロックフォント・ライフゲージ・警告・スコア(仕様書 3.1 / 3.5 / 5.3) */
#ifndef HUD_H
#define HUD_H
#include "engine.h"

void text_draw(i8 x, i8 y, const char *s, u8 color, u8 scale);
void text_center(i8 y, const char *s, u8 color, u8 scale);
void num_draw(i8 x, i8 y, u16 n, u8 digits, u8 color);   /* 右詰め、先頭 0 埋めなし */
void hud_life(u8 life);

#endif
