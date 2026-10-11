/* fx.h — 画面の演出(常駐部の fx.c) */
#ifndef FX_H
#define FX_H
#include "rail.h"

/* 数字の表示: 得点(白、敵の位置)/ 耐久が増えた(黄、車両の上)/ 耐久が減った(赤、車両の上) */
enum { POP_SCORE, POP_GAIN, POP_LOSS };
extern u8 g_pop_hx, g_pop_hy;     /* 得点を出す位置(撃った敵の画面上の位置) */
void popup(u8 kind, u16 v);
void popups_draw(void);
/* 被弾で車両から車体の破片が飛び散る */
void debris_start(void);
void debris_draw(void);
/* 分岐点から先の、選ばれていない側の線路(game.c が rail_sample で g_alt に入れる) */
#define ALT_N 12
/* 実際に描く点の数(2m おき)。分岐点から 16m まで(16m より先は画面で 1〜2 ブロックにしかならず、省いても絵の違いは
 * 1 枚あたり最大 2 ブロック。分岐を過ぎた直後で 1 フレーム約 4.6ms 軽くなる。2026-10-11 ユーザーと相談して決めた) */
#define ALT_DRAW_N 9
extern RailPt g_alt[ALT_N];
extern u8 g_alt_n;
void alt_draw(void);              /* 描く処理はゲームモジュール(game.c) */
void fx_reset(void);              /* ステージの始めに */

#endif
