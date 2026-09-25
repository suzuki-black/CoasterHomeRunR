/* trolley.h — 画面下部のトロッコの縁と、ライフに応じた壊れ方 */
#ifndef TROLLEY_H_INC
#define TROLLEY_H_INC
#include "engine.h"

#define TROLLEY_W   64
#define TROLLEY_H   9
#define TROLLEY_Y   (GAME_ROWS - TROLLEY_H)   /* 37 行目から */

/* ライフ(0-8)に合わせて絵を組み立て直す(変わったときだけ実際に作る) */
void trolley_set_life(u8 life);
/* xoff: 横のずれ(振り向きの演出)、shake: 1 なら 1 行下へ揺らす */
void trolley_draw(i8 xoff, u8 shake);
/* ブロック座標 (bx, by) がトロッコの絵で隠れているか(直前の描画位置で判定) */
u8 trolley_covers(u8 bx, u8 by);

#endif
