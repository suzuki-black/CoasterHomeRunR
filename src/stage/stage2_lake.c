/* stage2_lake.c — ステージ 2: 地底湖(仕様書 5 章 #2〜#4 を置き換え)
 *
 * 流れ: 坑道を抜けると大きな地底湖(A LAKE?!)→ レールが湖で途切れ、なぜかトロッコごと筏に乗って渡る(ON A RAFT?!)
 *       → 岩棚の弓兵(前)と、水に浮いた木箱の金貨袋 → 後ろからコウモリ人 → 湖の主(後ろから追ってくる大ガエル人)
 *       → 弓兵・コウモリ人 → 向こう岸でまたレールに戻り(LAND HO!)、出口へ
 * すべて位置で進める(EV_AT。約 16m ごとに敵が来るように並べる)。
 * ステージデータは定数だけで書き、読み込み先(0xB400)に合わせて単独でリンクする(stage1_cave.c と同じ)。
 */
#include "game.h"
#include "env.h"
#include "sound.h"

#define S(sec) ((u16)((sec) * 60))

/* (長さ[m], ヨー角の変化(65536 = 1 周、正 = 右), 1m あたりの高さ変化(1/16m), 速度(m/秒 × 4))。湖の上は筏の進む道筋 */
static const RailSeg lake_rail[] = {
    {  28,       0,   0, 16 },   /*   0〜 28m 坑道 */
    {  24,       0,   0, 16 },   /*  28m〜 地底湖(筏) */
    {  20,  0x1400,   0, 18 },
    {  28,       0,   0, 18 },
    {  20, -0x1800,   0, 18 },
    {  30,       0,   0, 18 },
    {  24,  0x1000,   0, 18 },
    {  40,       0,   0, 16 },   /* 湖の主と戦う */
    {  20, -0x1400,   0, 18 },
    {  36,       0,   0, 18 },
    {  30,       0,   0, 18 },   /* 〜300m */
    {  32,       0,   0, 14 },   /* 300〜332m 向こう岸の坑道 → 出口 */
    {   0,       0,   0,  0 },
};

/* 地形: (このリングまで, 種類)。リング = 4m */
static const u8 lake_zones[] = {
    10,                          /* レールの色(暗い岩) */
    7, K_EXIT, 75, K_LAKE, 0xFF, K_EXIT,
};

/* 9: 木 10: 暗い岩・レール 11: 岩 12: 鍾乳石 13: 水 14: 明るい水 15: (未使用) */
static const u8 lake_pal[14] = {
    0x41, 2,  0x12, 1,  0x32, 2,  0x44, 4,  0x14, 2,  0x25, 4,  0x55, 5,
};

enum { T_NONE, T_LAKE, T_RAFT, T_WARNING, T_LAND, T_EXIT };
static const Caption lake_captions[] = {
    { "A LAKE?!",       C_WHITE,  2 },
    { "ON A RAFT?!",    C_YELLOW, 1 },
    { "WARNING!",       C_RED,    2 },
    { "LAND HO!",       C_YELLOW, 2 },
    { "SUNLIGHT!",      C_WHITE,  1 },
};

/* BGM「地底湖」: 180BPM のホ短調(Em → C → D → B)。8 分音符 = 10 ティック。疾走感 */
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

static const Event lake_events[] = {
    { S(0),    EV_DIR,   DIR_NONE,    0,   0 },
    { S(0),    EV_BGM,   1,           0,   0 },
    /* ---- 坑道: 1 面から追ってきたコウモリ人 ---- */
    { AT(2, EV_DIR),        DIR_BACK, 0, 0 },
    { AT(4, EV_SPAWN),      ET_E02, -14, 6 },
    { AT(4, EV_SPAWN),      ET_E02, -16, -6 },
    /* ---- 地底湖 → 筏 ---- */
    { AT(16, EV_SPAWN),     ET_E02, -14, -6 },
    { AT(20, EV_TEXT),      T_LAKE, 0, 0 },
    { AT(26, EV_SPAWN),     ET_E02, -15, 6 },
    { AT(30, EV_TEXT),      T_RAFT, 0, 0 },
    /* ---- 岩棚の弓兵(前)と、水に浮いた木箱の金貨袋 ---- */
    { AT(40, EV_DIR),       DIR_FRONT, 0, 0 },
    { AT(40, EV_SPAWN),     ET_E04, 24, 36 },
    { AT(40, EV_SPAWN),     ET_E04, 26, -36 },
    { AT(56, EV_SPAWN),     ET_E04, 24, -36 },
    { AT(56, EV_SPAWN),     ET_CRATE, 22, 10 },
    { AT(72, EV_SPAWN),     ET_E04, 24, 36 },
    { AT(72, EV_SPAWN),     ET_E04, 27, -36 },
    /* ---- 後ろからコウモリ人 ---- */
    { AT(88, EV_DIR),       DIR_BACK, 0, 0 },
    { AT(88, EV_SPAWN),     ET_E02, -14, 6 },
    { AT(88, EV_SPAWN),     ET_E02, -16, -6 },
    { AT(104, EV_SPAWN),    ET_E02, -14, -6 },
    { AT(104, EV_SPAWN),    ET_E02, -16, 6 },
    { AT(118, EV_DIR),      DIR_FRONT, 0, 0 },
    { AT(118, EV_SPAWN),    ET_E04, 24, -36 },
    { AT(118, EV_SPAWN),    ET_E04, 26, 36 },
    { AT(118, EV_SPAWN),    ET_CRATE, 20, -10 },
    { AT(134, EV_SPAWN),    ET_E04, 24, 36 },
    { AT(134, EV_SPAWN),    ET_E04, 27, -36 },
    /* ---- 湖の主: 後ろから追ってくる(撃破までスクリプトは止まる) ---- */
    { AT(146, EV_DIR),      DIR_BACK, 0, 0 },     /* 湖の主は後ろから */
    { AT(146, EV_TEXT),     T_WARNING, 0, 0 },
    { AT(146, EV_SPEED),    12, 0, 0 },
    { AT(150, EV_BOSS),     ET_BOSS2, -18, 0 },
    { AT(150, EV_SPEED),    0, 0, 0 },
    { AT(150, EV_DIR),      DIR_FRONT, 0, 0 },
    { AT(176, EV_SPAWN),    ET_E04, 24, -36 },
    { AT(176, EV_SPAWN),    ET_E04, 26, 36 },
    { AT(192, EV_SPAWN),    ET_CRATE, 22, 10 },
    { AT(192, EV_SPAWN),    ET_E04, 24, -36 },
    { AT(206, EV_DIR),      DIR_BACK, 0, 0 },
    { AT(206, EV_SPAWN),    ET_E02, -14, 6 },
    { AT(206, EV_SPAWN),    ET_E02, -16, -6 },
    { AT(222, EV_SPAWN),    ET_E02, -14, -6 },
    { AT(222, EV_SPAWN),    ET_E02, -16, 6 },
    { AT(236, EV_DIR),      DIR_FRONT, 0, 0 },
    { AT(236, EV_SPAWN),    ET_E04, 24, 36 },
    { AT(236, EV_SPAWN),    ET_E04, 26, -36 },
    { AT(252, EV_SPAWN),    ET_E04, 24, -36 },
    { AT(252, EV_SPAWN),    ET_CRATE, 20, -10 },
    { AT(266, EV_DIR),      DIR_BACK, 0, 0 },
    { AT(266, EV_SPAWN),    ET_E02, -14, 6 },
    { AT(266, EV_SPAWN),    ET_E02, -16, -6 },
    { AT(280, EV_DIR),      DIR_FRONT, 0, 0 },
    { AT(280, EV_SPAWN),    ET_E04, 24, 36 },
    { AT(280, EV_SPAWN),    ET_E04, 26, -36 },
    /* ---- 向こう岸 → 出口 ---- */
    { AT(296, EV_DIR),      DIR_NONE, 0, 0 },
    { AT(296, EV_TEXT),     T_LAND, 0, 0 },
    { AT(302, EV_DIR),      DIR_BACK, 0, 0 },
    { AT(302, EV_SPAWN),    ET_E02, -14, 6 },
    { AT(312, EV_SPAWN),    ET_E02, -16, -6 },
    { AT(318, EV_TEXT),     T_EXIT, 0, 0 },
    { AT(328, EV_END),      0, 0, 0 },
};

const Stage stage_data = {
    lake_rail, lake_pal, lake_events, lake_captions,
    2,          /* 坑道の途中から */
    0,
    0,          /* 演出は飛ばさない(走りながら始まる) */
    bgm_melody, bgm_bass,
    lake_zones, 0x2B1D,
    "STAGE 2", "THE LAKE",
    0, 0, 0,
    { 0, 0 }, 0, 0,
    1,          /* やさしい進行 */
};
