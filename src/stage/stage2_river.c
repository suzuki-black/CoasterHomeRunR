/* stage2_river.c — ステージ 2: 崖と川と滝(仕様書 5 章 #2〜#4)
 *
 * 流れ: 洞窟の出口 → 崖の尾根 → 崖から落下 → 川に着水(なぜかトロッコはレールごと川面を走り続ける)
 *       → 高台の弓兵(前) → 後ろからコウモリ人 → 川の主(後ろから追ってくる) → 弓兵・木箱の救急箱 → 滝を落ちて次のステージへ
 * 崖・着水・滝は位置で起こす(EV_AT)。敵は時刻で出す(ボス戦の間は時刻が止まる)。
 * ステージデータは定数だけで書き、読み込み先(0xB400)に合わせて単独でリンクする(stage1_cave.c と同じ)。
 */
#include "game.h"
#include "env.h"
#include "sound.h"

#define S(sec) ((u16)((sec) * 60))

/* (長さ[m], ヨー角の変化(65536 = 1 周、正 = 右), 1m あたりの高さ変化(1/16m), 速度(m/秒 × 4)) */
static const RailSeg river_rail[] = {
    {  16,       0,   0, 16 },   /*   0〜 16m 洞窟の出口 */
    {  16,       0,   0, 14 },   /*  16〜 32m 崖の尾根(レールは崖の先で途切れる) */
    {   4,       0,  -4, 12 },   /*  32〜 52m 落下(だんだん急に) */
    {   4,       0, -12, 12 },
    {   6,       0, -28, 12 },
    {   6,       0, -44, 12 },
    {   8,       0,  -1, 20 },   /*  52m〜 着水、川(ずっと緩い下り、速い) */
    {  24,       0,  -1, 24 },
    {  20,  0x1800,  -1, 24 },   /* 右へ */
    {  24,       0,  -1, 26 },
    {  20, -0x2000,  -1, 24 },   /* 左へ */
    {  28,       0,  -2, 28 },   /* 急流 */
    {  20,  0x1400,  -1, 24 },
    {  24,       0,  -1, 24 },
    {  40,       0,  -1, 20 },   /* 川の主と戦う広い流れ */
    {  20, -0x1800,  -1, 24 },
    {  24,       0,  -2, 26 },
    {  28,  0x1000,  -1, 24 },
    {  16,       0,   0, 20 },   /* 〜348m */
    {   6,       0,  -8, 20 },   /* 348〜368m 滝 */
    {   6,       0, -24, 20 },
    {   8,       0, -40, 20 },
    {   0,       0,   0,  0 },
};

/* 地形: (このリングまで, 種類)。リング = 4m */
static const u8 river_zones[] = {
    10,                          /* 屋外のレールの色(暗い灰) */
    4, K_EXIT, 8, K_CLIFF, 13, K_FALL, 87, K_RIVER, 0xFF, K_FALLS,
};

/* 9: 木 10: 暗い岩・遠くの山・レール 11: 土・岩 12: 草 13: 水 14: 明るい水 15: 空 */
static const u8 river_pal[14] = {
    0x41, 2,  0x23, 2,  0x42, 3,  0x21, 5,  0x14, 2,  0x15, 3,  0x26, 4,
};

enum { T_NONE, T_OUT, T_WHOA, T_SPLASH, T_STILL, T_WARNING, T_FALLS, T_AAH };
static const Caption river_captions[] = {
    { "OUTSIDE!",        C_WHITE,  1 },
    { "WHOA!",           C_YELLOW, 2 },
    { "SPLASH!",         C_WHITE,  2 },
    { "STILL RUNNING?!", C_YELLOW, 1 },
    { "WARNING!",        C_RED,    2 },
    { "WATERFALL!",      C_RED,    1 },
    { "AAAAH!",          C_YELLOW, 2 },
};

/* BGM「川下り」: 180BPM のホ短調(Em → C → D → B)。8 分音符 = 10 ティック。疾走感 */
#define E8 10
#define Q  20
#define H  40
static const u8 bgm_melody[] = {
    76,E8, 79,E8, 83,Q,  81,E8, 79,E8, 78,E8, 79,E8,     /* Em */
    76,Q,  72,E8, 74,E8, 76,Q,  79,Q,                    /* C  */
    78,E8, 76,E8, 74,E8, 76,E8, 78,Q,  81,Q,             /* D  */
    83,H,  78,Q,  75,Q,                                  /* B  */
    88,E8, 86,E8, 83,E8, 86,E8, 88,Q,  83,Q,             /* Em */
    84,E8, 83,E8, 81,E8, 79,E8, 76,Q,  79,Q,             /* C  */
    81,E8, 83,E8, 81,E8, 79,E8, 78,Q,  74,Q,             /* D  */
    75,Q,  78,Q,  83,Q,  71,Q,                           /* B  */
    0xFF, 0,
};
static const u8 bgm_bass[] = {                           /* 8 分のオクターブ刻み(駆け足) */
    40,E8, 52,E8, 40,E8, 52,E8, 40,E8, 52,E8, 40,E8, 52,E8,
    36,E8, 48,E8, 36,E8, 48,E8, 36,E8, 48,E8, 36,E8, 48,E8,
    38,E8, 50,E8, 38,E8, 50,E8, 38,E8, 50,E8, 38,E8, 50,E8,
    35,E8, 47,E8, 35,E8, 47,E8, 35,E8, 47,E8, 39,E8, 42,E8,
    0xFF, 0,
};

#define AT(m, cmd) (m), (cmd) | EV_AT

static const Event river_events[] = {
    /* ---- 洞窟の出口 → 崖 → 落下 → 着水 ---- */
    { S(0),    EV_DIR,   DIR_NONE,    0,   0 },
    { S(0),    EV_BGM,   1,           0,   0 },
    { AT(14, EV_TEXT),   T_OUT,       0,   0 },
    { AT(29, EV_TEXT),   T_WHOA,      0,   0 },
    { AT(29, EV_FX),     SE_ALARM,    0,   0 },
    { AT(51, EV_FX),     SE_TRAIN,    8,   3 },
    { AT(51, EV_TEXT),   T_SPLASH,    0,   0 },
    { AT(60, EV_TEXT),   T_STILL,     0,   0 },
    /* ---- 高台の弓兵(前)と、水に浮いた木箱の救急箱 ---- */
    { S(18),   EV_DIR,   DIR_FRONT,   0,   0 },
    { S(18),   EV_SPAWN, ET_E04,      24,  26 },
    { S(19.5), EV_SPAWN, ET_E04,      24, -26 },
    { S(21),   EV_SPAWN, ET_E04,      26,  26 },
    { S(21),   EV_SPAWN, ET_CRATE,    22, -10 },
    { S(23),   EV_SPAWN, ET_E04,      24, -26 },
    { S(24.5), EV_SPAWN, ET_E04,      24,  26 },
    { S(24.5), EV_SPAWN, ET_E04,      28, -26 },
    /* ---- 後ろからコウモリ人 ---- */
    { S(27),   EV_DIR,   DIR_BACK,    0,   0 },
    { S(27.5), EV_SPAWN, ET_E02,     -14,   6 },
    { S(29),   EV_SPAWN, ET_E02,     -14,  -6 },
    { S(30.5), EV_SPAWN, ET_E02,     -16,   6 },
    { S(32),   EV_SPAWN, ET_E02,     -14,  -6 },
    /* ---- 川の主: 後ろから追ってくる(撃破までスクリプトは止まる) ---- */
    { S(35),   EV_TEXT,  T_WARNING,   0,   0 },
    { S(35),   EV_SPEED, 12,          0,   0 },
    { S(36),   EV_BOSS,  ET_BOSS2,   -18,  0 },
    { S(36.1), EV_SPEED, 0,           0,   0 },
    /* ---- 弓兵・救急箱・コウモリ人 ---- */
    { S(37),   EV_DIR,   DIR_FRONT,   0,   0 },
    { S(37),   EV_SPAWN, ET_CRATE,    22,  10 },
    { S(38),   EV_SPAWN, ET_E04,      24, -26 },
    { S(39),   EV_SPAWN, ET_E04,      24,  26 },
    { S(40),   EV_SPAWN, ET_E04,      27, -26 },
    { S(41.5), EV_DIR,   DIR_BACK,    0,   0 },
    { S(42),   EV_SPAWN, ET_E02,     -14,   6 },
    { S(43),   EV_SPAWN, ET_E02,     -16,  -6 },
    { S(45.5), EV_DIR,   DIR_FRONT,   0,   0 },
    { S(45.5), EV_SPAWN, ET_E04,      24,  26 },
    { S(46.5), EV_SPAWN, ET_E04,      26, -26 },
    /* ---- 滝: 落ちて森へ ---- */
    { AT(334, EV_DIR),   DIR_NONE,    0,   0 },
    { AT(334, EV_TEXT),  T_FALLS,     0,   0 },
    { AT(334, EV_FX),    SE_ALARM,    0,   0 },
    { AT(350, EV_TEXT),  T_AAH,       0,   0 },
    { AT(365, EV_END),   0,           0,   0 },
};

const Stage stage_data = {
    river_rail, river_pal, river_events, river_captions,
    2,          /* 洞窟の出口の手前から */
    0,
    0,          /* 演出は飛ばさない(走りながら始まる) */
    bgm_melody, bgm_bass,
    river_zones, 0x2B1D,
    "STAGE 2", "THE RIVER",
};
