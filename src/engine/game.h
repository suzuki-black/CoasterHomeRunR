/* game.h — ゲーム進行(仕様書 2〜5 章) */
#ifndef GAME_H
#define GAME_H
#include "rail.h"

/* 敵モデル = カメラ正対の矩形の集まり(仕様書 7.4)。単位 1/16m、y は足元から上 */
typedef struct { i8 x0, y0, x1, y1; u8 color; u8 part; } MRect;   /* part: 0=胴 1=頭 2=脚 */
typedef struct { const MRect *r; u8 n; } Model;

enum { ET_NONE, ET_E01, ET_E02, ET_E03, ET_E04, ET_E05, ET_B01, ET_TREASURE, ET_MEDKIT, ET_COUNT };

/* ステージスクリプト(仕様書 7.7)。時刻は 60Hz ティック */
enum { EV_SPAWN, EV_DIR, EV_GO, EV_BOSS, EV_END };
enum { DIR_NONE, DIR_FRONT, DIR_BACK };
typedef struct {
    u16 tick;
    u8  cmd;
    u8  a;       /* SPAWN/BOSS: 敵 ID、DIR: 方向 */
    i8  b;       /* SPAWN/BOSS: カメラからのレール距離[m](負 = 後方) */
    i8  c;       /* SPAWN: 横位置(1/8m、正 = 右) */
} Event;

typedef struct {
    const RailSeg *track;
    const u8      *palette;   /* 9-15 の 7 色(RB, G)× 7 */
    const Event   *events;
} Stage;

extern const Stage stage_cave;

/* 1 回遊ぶ。戻り値 0 = ゲームオーバー、1 = クリア */
u8 game_run(const Stage *st);
extern u16 g_score;          /* スコア / 10 */

/* 表示(main.c) */
extern u8 g_spr_attr[17];
extern const u8 pal_fixed[];  /* 0-8 */
void present(void);
void set_stage_palette(const u8 *pal);
void flash_palette(void);

/* 計測用(openMSX のテストが読む) */
extern u8  g_perf_ticks;     /* 直前のフレームにかかったティック数 */
extern u16 g_perf_render;
extern u16 g_perf_xfer;      /* vdp_xfer の時間 */
extern u16 g_perf_env;       /* env_draw までの時間 */    /* 直前のフレームの描画時間(タイマ値 ≒ 3.9µs) */
extern u8  g_scene;          /* 0=タイトル 1=ゲーム 2=ゲームオーバー 3=クリア */

#endif
