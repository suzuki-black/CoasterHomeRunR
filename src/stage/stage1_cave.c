/* stage_cave.c — ステージ 1: オープニング(宝を盗む)と洞窟(仕様書 5 章)
 *
 * 流れ: オープニング → 雑魚(前・後ろ) → 中ボス(大猿人の親方) → 雑魚 → 大ボス(洞窟の主) → ステージ 2(崖・川・滝)
 * ステージデータは定数だけで書き、読み込み先(0xB400)に合わせて単独でリンクして ROM のバンクに置く。
 * エンジンは常駐ローダーで RAM へ写してから使う(仕様書 7.3)。変数(初期値つき含む)は置けない。
 */
#include "game.h"
#include "env.h"

#define S(sec) ((u16)((sec) * 60))

/* 坑道の線形: 左右に折れ曲がり(S 字を含む)、急な上り下り(3/16 ≒ 19%)もある。
 * (長さ[m], ヨー角の変化(65536 = 1 周、正 = 右), 1m あたりの高さ変化(1/16m), 速度(m/秒 × 4)) */
static const RailSeg cave_rail[] = {
    {  18,       0,  0, 10 },   /* 宝の間 → 発進(2.5m/s) */
    {  20,       0,  0, 12 },
    {  16,  0x2000,  0, 14 },   /* 右へ 45 度 */
    {  12,       0,  0, 14 },
    {  16, -0x2800,  0, 14 },   /* 左へ 56 度 */
    {  20,       0, -3, 18 },   /* 急な下り */
    {  12,       0,  0, 18 },
    {  20,       0,  3, 13 },   /* 急な上り(遅くなる) */
    {  16,  0x3000,  0, 14 },   /* 右へ 67 度 */
    {  10,       0,  0, 16 },
    {  16, -0x3000, -2, 16 },   /* 下りながら左へ */
    {  20,       0,  0, 20 },
    {  16,  0x1800,  2, 16 },   /* 上りながら右へ */
    {  20, -0x1800,  0, 16 },   /* S 字 */
    {  30,       0, -2, 18 },   /* 下り */
    {  80,       0,  0, 12 },   /* 大ボスの広い坑道 → 出口へ */
    {   0,       0,  0,  0 },
};

/* 9: 坑木・枕木 10〜12: 石炭・岩(黒→灰。敵の茶 4 と見分けられる) 13: 天井の板・暗い土 14: 土 15: レール */
static const u8 cave_pal[14] = {
    0x41, 2,  0x12, 1,  0x22, 2,  0x33, 3,  0x20, 1,  0x31, 2,  0x55, 5,
};

enum { T_NONE, T_TREASURE, T_GOTIT, T_GRRR, T_ESCAPE, T_WARNING, T_CLEAR };
static const Caption cave_captions[] = {
    { "TREASURE!", C_YELLOW, 1 },
    { "GOT IT!",   C_YELLOW, 2 },
    { "GRRR!",     C_RED,    2 },
    { "ESCAPE!",   C_WHITE,  2 },
    { "WARNING!",  C_RED,    2 },
    { "CAVE CLEAR!", C_YELLOW, 1 },
};

/* BGM「逃走」: 150BPM の短調(Dm → B♭ → C → A)。8 分音符 = 12 ティック。勇ましく、急き立てる感じ */
#define E8 12
#define Q  24
#define DQ 36
#define H  48
static const u8 bgm_melody[] = {
    74,E8, 74,E8, 69,E8, 74,E8, 77,Q,  76,E8, 74,E8,     /* Dm */
    72,DQ, 70,E8, 69,Q,  67,Q,                           /* B♭ */
    67,E8, 69,E8, 70,E8, 72,E8, 76,Q,  72,Q,             /* C  */
    69,H,  73,Q,  76,Q,                                  /* A  */
    74,E8, 74,E8, 69,E8, 74,E8, 77,E8, 79,E8, 81,Q,      /* Dm */
    82,Q,  81,E8, 79,E8, 77,Q,  74,Q,                    /* B♭ */
    76,E8, 77,E8, 79,E8, 76,E8, 72,Q,  76,Q,             /* C  */
    81,Q,  79,E8, 77,E8, 76,Q,  73,Q,                    /* A  */
    0xFF, 0,
};
static const u8 bgm_bass[] = {                           /* 8 分のオクターブ刻み */
    38,E8, 50,E8, 38,E8, 50,E8, 38,E8, 50,E8, 38,E8, 50,E8,
    34,E8, 46,E8, 34,E8, 46,E8, 34,E8, 46,E8, 34,E8, 46,E8,
    36,E8, 48,E8, 36,E8, 48,E8, 36,E8, 48,E8, 36,E8, 48,E8,
    33,E8, 45,E8, 33,E8, 45,E8, 33,E8, 45,E8, 40,E8, 43,E8,
    0xFF, 0,
};

static const Event cave_events[] = {
    /* ---- オープニング: 宝の間でトロッコは止まったまま、後ろ(宝の間)を向いている ---- */
    { S(0),    EV_CTRL,  0,           0,   0 },
    { S(0),    EV_DIR,   DIR_NONE,    0,   0 },
    { S(0),    EV_SPAWN, ET_TREASURE, -3,  0 },
    { S(0.3),  EV_TEXT,  T_TREASURE,  0,   0 },
    { S(2),    EV_GRAB,  0,           0,   0 },
    { S(3),    EV_TEXT,  T_GOTIT,     0,   0 },
    { S(3.8),  EV_SPAWN, ET_E01,     -11,  -8 },
    { S(3.8),  EV_SPAWN, ET_E01,     -12,   8 },
    { S(4),    EV_SPAWN, ET_E01,     -14,   0 },
    { S(4.2),  EV_TEXT,  T_GRRR,      0,   0 },
    /* 発進してカメラが前へ向き、操作できるようになる(ここまで A で飛ばせる) */
    { S(6),    EV_TEXT,  T_ESCAPE,    0,   0 },
    { S(6),    EV_GO,    0,           0,   0 },
    { S(6),    EV_TURN,  0,           0,   0 },
    { S(6),    EV_CTRL,  1,           0,   0 },
    { S(6),    EV_DIR,   DIR_BACK,    0,   0 },
    { S(6),    EV_BGM,   1,           0,   0 },
    /* ---- 雑魚: 前から猿人 ---- */
    { S(10),   EV_DIR,   DIR_FRONT,   0,   0 },
    { S(10),   EV_SPAWN, ET_E01,      22,  10 },
    { S(11.5), EV_SPAWN, ET_E01,      22, -10 },
    { S(13),   EV_SPAWN, ET_E01,      22,  10 },
    { S(13),   EV_SPAWN, ET_E01,      26, -10 },
    { S(15),   EV_SPAWN, ET_E01,      22, -10 },
    { S(16.5), EV_SPAWN, ET_E01,      24,  10 },
    { S(17.5), EV_SPAWN, ET_E01,      22, -10 },
    { S(19),   EV_SPAWN, ET_E01,      22,  10 },
    { S(19),   EV_SPAWN, ET_E01,      25, -10 },
    /* ---- 雑魚: 後ろからコウモリ人 ---- */
    { S(22),   EV_DIR,   DIR_BACK,    0,   0 },
    { S(23),   EV_SPAWN, ET_E02,     -14,   6 },
    { S(25),   EV_SPAWN, ET_E02,     -14,  -6 },
    { S(27),   EV_SPAWN, ET_E02,     -16,   6 },
    { S(28),   EV_SPAWN, ET_E02,     -18,  -6 },
    { S(31),   EV_SPAWN, ET_E02,     -14,   6 },
    /* ---- 中ボス: 大猿人の親方(撃破までスクリプトは止まる) ---- */
    { S(35),   EV_DIR,   DIR_FRONT,   0,   0 },
    { S(35),   EV_TEXT,  T_WARNING,   0,   0 },
    { S(35),   EV_SPEED, 6,           0,   0 },
    { S(36),   EV_BOSS,  ET_B01,      16,  0 },
    { S(36.1), EV_SPEED, 0,           0,   0 },
    /* ---- 雑魚: 前後から。救急箱で立て直す ---- */
    { S(38),   EV_SPAWN, ET_MEDKIT,   18,  10 },
    { S(39),   EV_SPAWN, ET_E01,      22, -10 },
    { S(40),   EV_SPAWN, ET_E01,      22,  10 },
    { S(41),   EV_SPAWN, ET_E01,      25, -10 },
    { S(42),   EV_DIR,   DIR_BACK,    0,   0 },
    { S(43),   EV_SPAWN, ET_E02,     -14,   6 },
    { S(44),   EV_SPAWN, ET_E02,     -16,  -6 },
    { S(47),   EV_DIR,   DIR_FRONT,   0,   0 },
    { S(47),   EV_SPAWN, ET_E01,      22,  10 },
    { S(48),   EV_SPAWN, ET_E01,      22, -10 },
    { S(49),   EV_SPAWN, ET_E01,      24,  10 },
    { S(50),   EV_SPAWN, ET_MEDKIT,   18, -10 },
    /* ---- 大ボス: 洞窟の主 ---- */
    { S(53),   EV_TEXT,  T_WARNING,   0,   0 },
    { S(53),   EV_SPEED, 6,           0,   0 },
    { S(54),   EV_BOSS,  ET_BOSS1,    20,  0 },
    /* ---- クリア: 洞窟の出口へ ---- */
    { S(54.1), EV_DIR,   DIR_NONE,    0,   0 },
    { S(54.1), EV_SPEED, 16,          0,   0 },
    { S(54.1), EV_TEXT,  T_CLEAR,     0,   0 },
    { S(58),   EV_END,   0,           0,   0 },
};

/* 地形: 全部が坑道。先頭はレールの色 */
static const u8 cave_zones[] = { 15, 0xFF, K_CAVE };

const Stage stage_data = {
    cave_rail, cave_pal, cave_events, cave_captions,
    8,          /* 宝の間の 8m 地点から */
    1,          /* 後ろ(宝の間)を向いて始まる */
    S(6),       /* A でオープニングを飛ばす先 */
    bgm_melody, bgm_bass,
    cave_zones, 0x5EED,
    "STAGE 1", "THE CAVE",
};
