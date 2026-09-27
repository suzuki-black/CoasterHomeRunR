/* trolley.h — 画面下部のトロッコの縁と、縁の上に積んだ宝の山(ライフ) */
#ifndef TROLLEY_H_INC
#define TROLLEY_H_INC
#include "engine.h"

#define TROLLEY_W   64
#define PILE_H      4                          /* 宝の山の行数(縁の上) */
#define TROLLEY_H   13
#define TROLLEY_Y   (GAME_ROWS - TROLLEY_H)   /* 33 行目から(縁は 37 行目から) */

/* ライフ(0-8)に合わせて宝の山を組み立て直す(変わったときだけ実際に作る) */
void trolley_set_life(u8 life);
/* xoff: 横のずれ(振り向きの演出)、shake: 1 なら 1 行下へ揺らす */
void trolley_draw(i8 xoff, u8 shake);
/* ブロック座標 (bx, by) がトロッコの絵で隠れているか(直前の描画位置で判定) */
u8 trolley_covers(u8 bx, u8 by);

#endif
