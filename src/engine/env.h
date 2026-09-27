/* env.h — 環境ポリゴン */
#ifndef ENV_H
#define ENV_H
#include "engine.h"

/* 地形の種類(ステージの zones で区間ごとに指定する) */
enum { K_CAVE,     /* 炭鉱の坑道(ステージ 1 のパレット) */
       K_EXIT,     /* 洞窟の出口(屋外のパレットで描く坑道) */
       K_LAKE,     /* 地底湖(大きな空洞の水面。トロッコごと筏に乗って渡る) */
       K_FOREST,   /* 森 */
       K_TOWN,     /* 町(両側に家並み) */
       K_HOME,     /* 自宅前(町の道の突き当たりに自宅) */
       K_COUNT };

/* s = カメラのレール位置(1/64m)、look = 視線の相対ヨー(0 前 / 128 後ろ) */
void env_draw(u16 s, u8 look);
/* ステージ開始時(レール表を作った後)に、断面・色・飾りを乱数 seed と地形の区間 zones で作る */
void env_build(u16 seed, const u8 *zones);

#endif
