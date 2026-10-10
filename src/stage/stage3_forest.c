/* stage3_forest.c — ステージ 3: 森(仕様書 5 章 #5)
 *
 * 流れ: 洞窟を出て森の小道へ → キリン人(前)と魔法使い → 後ろからコウモリ人・魔法使い
 *       → キリン人・魔法使い・救急箱 → 森の番人(巨大コウモリ人) → ステージ 4(町)
 * ステージデータは定数だけで書き、読み込み先(0xB400)に合わせて単独でリンクする(stage1_cave.c と同じ)。
 */
#include "game.h"
#include "env.h"
#include "sound.h"

#define S(sec) ((u16)((sec) * 60))

/* (長さ[m], ヨー角の変化(65536 = 1 周、正 = 右), 1m あたりの高さ変化(1/16m), 速度(m/秒 × 4)) */
static const RailSeg forest_rail[] = {
    {  16,       0,   0, 16 },   /* 洞窟を出たところ */
    {  24,       0,   0, 18 },
    {  20,  0x2000,   1, 16 },   /* 上りながら右へ */
    {  24,       0,   0, 18 },
    {  20, -0x2800,  -1, 18 },   /* 下りながら左へ */
    {  24,       0,   0, 18 },
    {  20,  0x1800,   0, 16 },   /* S 字 */
    {  20, -0x1800,   1, 16 },
    {  28,       0,  -1, 18 },
    {  20,  0x2000,   0, 18 },
    {  24,       0,   0, 16 },
    {  60,       0,   0, 14 },   /* 森の番人と戦う */
    {  16,       0,   0, 14 },
    {   0,       0,   0,  0 },
};

static const u8 forest_zones[] = {
    10,                          /* 屋外のレールの色(暗い色) */
    0xFF, K_FOREST,
};

/* 9: 幹・落ち葉 10: 影・レール 11: 土の道 12: 下草 13: 濃い茂み 14: 明るい葉 15: 空 */
static const u8 forest_pal[14] = {
    0x31, 2,  0x11, 1,  0x42, 3,  0x21, 5,  0x01, 3,  0x31, 6,  0x26, 4,
};

enum { T_NONE, T_FOREST, T_WARNING, T_CLEAR };
static const Caption forest_captions[] = {
    { "INTO THE WOODS", C_WHITE, 1 },
    { "WARNING!",      C_RED,    2 },
    { "FOREST CLEAR!", C_YELLOW, 1 },
};

/* BGM「森の小道」: 140BPM のト長調(G → C → D → G)。8 分音符 = 13 ティック。軽快 */
#define E8 13
#define Q  26
#define DQ 39
#define H  52
static const u8 bgm_melody[] = {
    67,E8, 71,E8, 74,Q,  71,E8, 74,E8, 79,Q,             /* G */
    76,Q,  72,E8, 76,E8, 79,Q,  76,Q,                    /* C */
    74,E8, 76,E8, 78,E8, 74,E8, 81,Q,  78,Q,             /* D */
    79,DQ, 78,E8, 79,H,                                  /* G */
    83,E8, 81,E8, 79,E8, 81,E8, 83,Q,  79,Q,             /* G */
    84,E8, 83,E8, 81,E8, 79,E8, 76,Q,  72,Q,             /* C */
    74,E8, 78,E8, 81,E8, 78,E8, 74,Q,  76,E8, 78,E8,     /* D */
    79,Q,  74,Q,  79,Q,  0,Q,                            /* G */
    0xFF, 0,
};
static const u8 bgm_bass[] = {                           /* 4 分のズンチャッ */
    43,Q, 50,Q, 43,Q, 50,Q,
    36,Q, 43,Q, 36,Q, 43,Q,
    38,Q, 45,Q, 38,Q, 45,Q,
    43,Q, 50,Q, 47,Q, 50,Q,
    0xFF, 0,
};

static const Event forest_events[] = {
    /* ---- 森へ ---- */
    { S(0),    EV_DIR,   DIR_NONE,    0,   0 },
    { S(1),    EV_TEXT,  T_FOREST,    0,   0 },
    { S(0),    EV_BGM,   1,           0,   0 },
    /* ---- 前からキリン人(槍のリーチが長い)と魔法使い ---- */
    { S(2.5),   EV_DIR,   DIR_FRONT,   0,   0 },
    { S(2.5),   EV_SPAWN, ET_E03,      22, -10 },
    { S(5),    EV_DIR,   DIR_FRONT,   0,   0 },
    { S(5),    EV_SPAWN, ET_E03,      22,  10 },
    { S(6.5),  EV_SPAWN, ET_E03,      22, -10 },
    { S(8),    EV_SPAWN, ET_E03,      24,  10 },
    { S(9),    EV_SPAWN, ET_E05,      20,   8 },
    { S(11),   EV_SPAWN, ET_E03,      22, -10 },
    { S(12),   EV_SPAWN, ET_MEDKIT,   18,  10 },
    { S(13),   EV_SPAWN, ET_E03,      22,  10 },
    { S(13),   EV_SPAWN, ET_E03,      25, -10 },
    /* ---- 後ろからコウモリ人と魔法使い ---- */
    { S(14.5),  EV_SPAWN, ET_E05,      20,  -8 },
    { S(16),   EV_DIR,   DIR_BACK,    0,   0 },
    { S(16.5), EV_SPAWN, ET_E05,     -12,   6 },
    { S(18),   EV_SPAWN, ET_E02,     -14,  -6 },
    { S(19.5), EV_SPAWN, ET_E05,     -14,  -6 },
    { S(21),   EV_SPAWN, ET_E02,     -14,   6 },
    { S(23),   EV_SPAWN, ET_E02,     -16,  -6 },
    /* ---- 前から: キリン人・魔法使い・救急箱 ---- */
    { S(24.5),  EV_SPAWN, ET_E05,     -12,   6 },
    { S(26),   EV_DIR,   DIR_FRONT,   0,   0 },
    { S(26),   EV_SPAWN, ET_E03,      22,  10 },
    { S(27),   EV_SPAWN, ET_E05,      20,  -8 },
    { S(28.5), EV_SPAWN, ET_E03,      22, -10 },
    { S(30),   EV_SPAWN, ET_E03,      24,  10 },
    { S(31),   EV_SPAWN, ET_MEDKIT,   18, -10 },
    { S(32),   EV_SPAWN, ET_E05,      20,   8 },
    { S(34),   EV_SPAWN, ET_E03,      22, -10 },
    { S(34),   EV_SPAWN, ET_E03,      25,  10 },
    /* ---- 森の番人(撃破までスクリプトは止まる) ---- */
    { S(35.5),  EV_SPAWN, ET_E03,      22, -10 },
    { S(36),   EV_SPAWN, ET_MISBOX,   18,  10 },   /* 誘導弾の箱(森の番人の前) */
    { S(36.5),  EV_SPAWN, ET_E05,      20,   8 },
    { S(38),   EV_TEXT,  T_WARNING,   0,   0 },
    { S(38),   EV_SPEED, 6,           0,   0 },
    { S(39),   EV_BOSS,  ET_B02,      20,  0 },
    /* ---- クリア ---- */
    { S(39.1), EV_DIR,   DIR_NONE,    0,   0 },
    { S(39.1), EV_SPEED, 16,          0,   0 },
    { S(39.1), EV_TEXT,  T_CLEAR,     0,   0 },
    { S(43),   EV_END,   0,           0,   0 },
};

const Stage stage_data = {
    forest_rail, forest_pal, forest_events, forest_captions,
    2,
    0,
    0,          /* 演出は飛ばさない(走りながら始まる) */
    bgm_melody, bgm_bass,
    forest_zones, 0x7A3C,
    "STAGE 3", "THE FOREST",
};
