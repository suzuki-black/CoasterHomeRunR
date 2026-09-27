/* env.h — 環境ポリゴン */
#ifndef ENV_H
#define ENV_H
#include "engine.h"

/* 地形の種類(ステージの zones で区間ごとに指定する) */
enum { K_CAVE,     /* 炭鉱の坑道(ステージ 1 のパレット) */
       K_EXIT,     /* 洞窟の出口(屋外のパレットで描く坑道) */
       K_CLIFF,    /* 空に突き出た崖の尾根 */
       K_FALL,     /* 落下中(何も描かない) */
       K_RIVER,    /* 川 */
       K_FALLS,    /* 滝(急な下りの白い水) */
       K_FOREST,   /* 森 */
       K_COUNT };

/* s = カメラのレール位置(1/64m)、look = 視線の相対ヨー(0 前 / 128 後ろ) */
void env_draw(u16 s, u8 look);
/* ステージ開始時(レール表を作った後)に、断面・色・飾りを乱数 seed と地形の区間 zones で作る */
void env_build(u16 seed, const u8 *zones);

#endif
