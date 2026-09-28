/* env.h — 環境ポリゴン
 *
 * 地形の処理(env.c)は、マッパーのセグメント 5 に置く「地形モジュール」(起動時に ROM から 1 回読み込む)。
 * 呼ぶときだけページ2をセグメント 5 に切り替える(sys.s の env_build / env_draw が切り替えて呼び、戻す)。
 * モジュールの中からはゲームモジュール(ページ2のセグメント 1)の定数は読めないので、地形の区間と乱数の種は
 * 常駐部の g_env_zones / g_env_seed に写してから env_build を呼ぶ。
 */
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

#define ENV_NR        11          /* リングの数 - 1(前 5 + 後ろ 5 + 仮リング)。env.c の NR と同じ */
#define ENV_ZONES_MAX 32

/* s = カメラのレール位置(1/64m)、look = 視線の相対ヨー(0 前 / 128 後ろ。下位 8 ビット)。
 * 引数はどちらも 16 ビットにしてレジスタ(HL, DE)で渡す(スタックで渡すと、切り替えを挟む sys.s の中継でずれる) */
void env_draw(u16 s, u16 look);
/* ステージ開始時(レール表を作った後)に、断面・色・飾りを乱数の種 g_env_seed と地形の区間 g_env_zones で作る */
void env_build(void);
extern u8  g_env_zones[ENV_ZONES_MAX];   /* 先頭 = レールの色、以降 (このリングまで, 種類) の組。0xFF まで */
extern u16 g_env_seed;
extern u8  g_seg2;

/* 地形モジュールの中の本体(ページ2 = セグメント 5 のときだけ呼べる。常駐部の env_build / env_draw 経由で呼ぶ) */
void envm_build(void);
void envm_draw(u16 s, u16 look);

#endif
