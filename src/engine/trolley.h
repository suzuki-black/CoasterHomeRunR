/* trolley.h — 画面下部のコースターの車両(一人乗りの先頭車両。前・後ろの絵と、耐久で進む壊れ具合) */
#ifndef TROLLEY_H_INC
#define TROLLEY_H_INC
#include "engine.h"

#define TROLLEY_W   64
#define TROLLEY_H   13
#define TROLLEY_Y   (GAME_ROWS - TROLLEY_H)   /* 33 行目から */

/* 絵を組み立て直す: life = 車両の耐久(0-8。減るほど壊れる)、back = 1 なら後ろを向いたときの絵(変わったときだけ実際に作る) */
void trolley_set(u8 life, u8 back);
/* xoff: 横のずれ(振り向きの演出)、shake: 1 なら 1 行下へ揺らす */
void trolley_draw(i8 xoff, u8 shake);
/* ブロック座標 (bx, by) が車両の絵で隠れているか(直前の描画位置で判定。壊れて開いた穴からは撃てる) */
u8 trolley_covers(u8 bx, u8 by);

#endif
