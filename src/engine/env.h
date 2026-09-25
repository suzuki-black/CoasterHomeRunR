/* env.h — 環境ポリゴン */
#ifndef ENV_H
#define ENV_H
#include "engine.h"

/* s = カメラのレール位置(1/64m)、look = 視線の相対ヨー(0 前 / 128 後ろ) */
void env_draw(u16 s, u8 look);

#endif
