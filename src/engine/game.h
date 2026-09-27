/* game.h — ゲーム進行(仕様書 2〜5 章) */
#ifndef GAME_H
#define GAME_H
#include "rail.h"

/* 敵モデル = カメラ正対の矩形の集まり(仕様書 7.4)。単位 1/16m、y は足元から上 */
typedef struct { i8 x0, y0, x1, y1; u8 color; u8 part; } MRect;   /* part: 0=胴 1=頭 2=脚 */
typedef struct { const MRect *r; u8 n; } Model;
extern const Model models[];     /* 敵の種類ごと(models.c、ページ0) */
extern const Model model_e02b;   /* コウモリ人の翼を下げた絵 */

enum { ET_NONE, ET_E01, ET_E02, ET_E03, ET_E04, ET_E05, ET_B01, ET_TREASURE, ET_MEDKIT,
       ET_BOSS1,     /* 洞窟の主(ステージ 1 の大ボス) */
       ET_ROCK,      /* 投げた岩(敵弾。撃ち落とせる) */
       ET_ARROW,     /* 矢(E04 の敵弾) */
       ET_MAGIC,     /* 魔法弾・水の玉(E05・ボスの敵弾。大きく遅い) */
       ET_CRATE,     /* 川に浮いた木箱の上の救急箱 */
       ET_BOSS2,     /* 川の主(ステージ 2 のボス。後ろから追ってくる大ガエル人) */
       ET_B02,       /* 森の番人(ステージ 3 のボス。巨大コウモリ人) */
       ET_B03,       /* 暴走汽車(町。並走レールを後ろから来て体当たり。避けられない) */
       ET_B04,       /* 亜人の王(自宅前のラスボス。キリン人系) */
       ET_COUNT };
#define IS_BOSS(t) ((t) == ET_B01 || (t) == ET_BOSS1 || (t) == ET_BOSS2 || (t) == ET_B02 || \
                    (t) == ET_B03 || (t) == ET_B04)
#define IS_SHOT(t) ((t) == ET_ROCK || (t) == ET_ARROW || (t) == ET_MAGIC)

/* ステージスクリプト(仕様書 7.7)。時刻は 60Hz ティック */
enum {
    EV_SPAWN,   /* a = 敵、b = 距離[m]、c = 横位置(1/8m) */
    EV_DIR,     /* a = 襲撃方向(警告表示) */
    EV_GO,      /* a = 0 でトロッコ発進、1 で止める */
    EV_BOSS,    /* a = ボス、b = 距離[m]。撃破までスクリプトは止まる */
    EV_END,     /* ステージクリア */
    EV_TEXT,    /* a = 字幕の番号(0 = 消す)。約 2 秒で消える */
    EV_SPEED,   /* a = トロッコの速度(m/秒 × 4)、0 = レールの指定に戻す */
    EV_CTRL,    /* a = 1 で操作できる、0 で操作できない(演出中) */
    EV_TURN,    /* a = 0 で前、1 で後ろへ自動で振り向く */
    EV_GRAB,    /* 宝箱を取る(宝箱がカメラへ飛んできて消える) */
    EV_BGM,     /* a = 1 でステージの BGM を鳴らす、0 で止める */
    EV_FX,      /* a = 効果音、b = 揺れのフレーム数、c = 1 で画面を光らせる */
};
/* cmd にこの印を付けると、tick は時刻ではなくレール上の位置[m](カメラがそこを過ぎたら実行) */
#define EV_AT 0x80
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
    const u8 *bgm_melody;     /* BGM(sound.h の形式) */
    const u8 *bgm_bass;
    const u8 *zones;          /* 地形の区間(env.h の env_build) */
    u16 seed;                 /* 地形の乱数の種 */
    const char *name;         /* ステージの始めに出す名前(2 行目は title2) */
    const char *title2;
    /* 最後のステージだけ: エンディング(SCREEN 1、BIOS のフォント)の文と BGM */
    const u8 *ending;         /* (行, 桁, 文字コード…, 0) の並び、0xFF で終わり */
    const u8 *end_melody;
    const u8 *end_bass;
} Stage;

/* ステージデータの読み込み先(ROM バンクから写す。先頭 2 バイトが Stage へのポインタ) */
#define STAGE_BUF    0xB400
#define STAGE1_BANK  6
#define STAGE_COUNT  5        /* バンク 6, 8, 10, 12, 14 */

/* ステージを 1 つ遊ぶ。first = 1 ならライフとスコアを初期化する。
 * 戻り値 0 = ゲームオーバー、1 = クリア */
u8 game_run(const Stage *st, u8 first);
extern u16 g_score;          /* スコア / 10 */
extern u8  life;
extern u8  g_stage;

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
