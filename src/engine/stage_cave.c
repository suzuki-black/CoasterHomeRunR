/* stage_cave.c — ステージ 1: オープニング(宝を盗む)と洞窟(仕様書 5 章)
 *
 * 流れ: オープニング → 雑魚(前・後ろ) → 中ボス(大猿人の親方) → 雑魚 → 大ボス(洞窟の主) → 仮エンディング
 * 【試作版】ステージデータは C の定数としてエンジンに同梱している。
 * 仕様書 7.3 の「ステージ切替時に常駐ローダーが ROM バンクから読む」方式は、
 * ステージが増えてエンジンの 32KB に収まらなくなった時点で移す。
 */
#include "game.h"

#define S(sec) ((u16)((sec) * 60))

static const RailSeg cave_rail[] = {
    {  18,       0,  0, 10 },   /* 宝の間 → 発進(2.5m/s) */
    {  30,       0,  0, 12 },
    {  36,  0x2000,  0, 14 },   /* 右カーブ 45 度 */
    {  24,       0,  1, 16 },   /* ゆるい上り */
    {  40, -0x3000,  0, 16 },   /* 左カーブ */
    {  30,       0, -1, 18 },
    {  40,  0x1800,  0, 20 },   /* 5m/s */
    {  30,       0,  0, 20 },
    {  40, -0x1000,  0, 16 },
    { 110,       0,  0, 12 },   /* 洞窟の出口へ */
    {   0,       0,  0,  0 },
};

/* 9: 木の枠 10/11: 天井 12/13: 壁 14/15: 床。暗い茶・灰・黒(敵の茶 4 と見分けられる色) */
static const u8 cave_pal[14] = {
    0x30, 2,  0x22, 2,  0x11, 1,  0x23, 2,  0x12, 1,  0x31, 2,  0x21, 1,
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

const Stage stage_cave = {
    cave_rail, cave_pal, cave_events, cave_captions,
    8,          /* 宝の間の 8m 地点から */
    1,          /* 後ろ(宝の間)を向いて始まる */
    S(6),       /* A でオープニングを飛ばす先 */
};
