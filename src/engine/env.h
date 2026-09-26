/* env.h — 環境ポリゴン */
#ifndef ENV_H
#define ENV_H
#include "engine.h"

/* s = カメラのレール位置(1/64m)、look = 視線の相対ヨー(0 前 / 128 後ろ) */
void env_draw(u16 s, u8 look);
/* ステージ開始時(レール表を作った後)に、洞窟の断面・色・飾りを乱数 seed で作る */
void env_build(u16 seed);

#endif
