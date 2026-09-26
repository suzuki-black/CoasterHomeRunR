/* game.h — ゲーム進行(仕様書 2〜5 章) */
#ifndef GAME_H
#define GAME_H
#include "rail.h"

/* 敵モデル = カメラ正対の矩形の集まり(仕様書 7.4)。単位 1/16m、y は足元から上 */
typedef struct { i8 x0, y0, x1, y1; u8 color; u8 part; } MRect;   /* part: 0=胴 1=頭 2=脚 */
typedef struct { const MRect *r; u8 n; } Model;

enum { ET_NONE, ET_E01, ET_E02, ET_E03, ET_E04, ET_E05, ET_B01, ET_TREASURE, ET_MEDKIT,
       ET_BOSS1,     /* 洞窟の主(ステージ 1 の大ボス) */
       ET_ROCK,      /* 投げた岩(敵弾。撃ち落とせる) */
       ET_COUNT };

/* ステージスクリプト(仕様書 7.7)。時刻は 60Hz ティック */
enum {
    EV_SPAWN,   /* a = 敵、b = 距離[m]、c = 横位置(1/8m) */
    EV_DIR,     /* a = 襲撃方向(警告表示) */
    EV_GO,      /* トロッコ発進 */
    EV_BOSS,    /* a = ボス、b = 距離[m]。撃破までスクリプトは止まる */
    EV_END,     /* ステージクリア */
    EV_TEXT,    /* a = 字幕の番号(0 = 消す)。約 2 秒で消える */
    EV_SPEED,   /* a = トロッコの速度(m/秒 × 4)、0 = レールの指定に戻す */
    EV_CTRL,    /* a = 1 で操作できる、0 で操作できない(演出中) */
    EV_TURN,    /* a = 0 で前、1 で後ろへ自動で振り向く */
    EV_GRAB,    /* 宝箱を取る(宝箱がカメラへ飛んできて消える) */
};
enum { DIR_NONE, DIR_FRONT, DIR_BACK };
typedef struct {
    u16 tick;
    u8  cmd;
    u8  a;       /* SPAWN/BOSS: 敵 ID、DIR: 方向 */
    i8  b;       /* SPAWN/BOSS: カメラからのレール距離[m](負 = 後方) */
    i8  c;       /* SPAWN: 横位置(1/8m、正 = 右) */
} Event;

typedef struct { const char *text; u8 color; u8 scale; } Caption;

typedef struct {
    const RailSeg *track;
    const u8      *palette;   /* 9-15 の 7 色(RB, G)× 7 */
    const Event   *events;
    const Caption *captions;  /* 字幕(番号 1 から) */
    u8  start_m;              /* 開始位置[m] */
    u8  start_back;           /* 1 = 後ろを向いて始まる(オープニング) */
    u16 skip_tick;            /* 演出中に A を押したらこの時刻まで飛ばす(0 = 飛ばせない) */
} Stage;

extern const Stage stage_cave;

/* 仮エンディング(main.c) */
void ending_run(void);

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
