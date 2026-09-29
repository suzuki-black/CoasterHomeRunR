/* fx.h — 画面の演出(常駐部の fx.c) */
#ifndef FX_H
#define FX_H
#include "rail.h"

/* 数字の表示: 得点(白、敵の位置)/ 宝が増えた(黄、宝の山の上)/ 宝が減った(赤、宝の山の上) */
enum { POP_SCORE, POP_GAIN, POP_LOSS };
extern u8 g_pop_hx, g_pop_hy;     /* 得点を出す位置(撃った敵の画面上の位置) */
void popup(u8 kind, u16 v);
void popups_draw(void);
/* 被弾で宝の山から金貨が飛び散る */
void coins_start(void);
void coins_draw(void);
/* 分岐点から先の、選ばれていない側の線路(game.c が rail_sample で g_alt に入れる) */
#define ALT_N 12
extern RailPt g_alt[ALT_N];
extern u8 g_alt_n;
void alt_draw(void);
void fx_reset(void);              /* ステージの始めに */

#endif
