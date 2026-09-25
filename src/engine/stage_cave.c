/* stage_cave.c — ステージ 0〜1: 宝物発見・洞窟(仕様書 5 章)
 *
 * 【試作版】ステージデータは C の定数としてエンジンに同梱している。
 * 仕様書 7.3 の「ステージ切替時に常駐ローダーが ROM バンクから読む」方式は、
 * ステージが増えてエンジンの 32KB に収まらなくなった時点で移す。
 */
#include "game.h"

#define S(sec) ((u16)((sec) * 60))

static const RailSeg cave_rail[] = {
    {  10,       0,  0, 10 },   /* 発進(2.5m/s) */
    {  30,       0,  0, 12 },
    {  36,  0x2000,  0, 14 },   /* 右カーブ 45 度 */
    {  24,       0,  1, 16 },   /* ゆるい上り */
    {  40, -0x3000,  0, 16 },   /* 左カーブ */
    {  30,       0, -1, 18 },
    {  40,  0x1800,  0, 20 },   /* 5m/s */
    {  30,       0,  0, 20 },
    {  40, -0x1000,  0, 16 },
    { 110,       0,  0,  8 },   /* ボス区間(2m/s) */
    {   0,       0,  0,  0 },
};

/* 9: 木の枠 10/11: 天井 12/13: 壁 14/15: 床。暗い茶・灰・黒(敵の茶 4 と見分けられる色) */
static const u8 cave_pal[14] = {
    0x30, 2,  0x22, 2,  0x11, 1,  0x23, 2,  0x12, 1,  0x31, 2,  0x21, 1,
};

static const Event cave_events[] = {
    { S(0),    EV_DIR,   DIR_NONE,  0,   0 },
    { S(0),    EV_SPAWN, ET_TREASURE, 3, 10 },
    { S(1.5),  EV_SPAWN, ET_E01,   12, -10 },
    { S(2.5),  EV_DIR,   DIR_FRONT, 0,   0 },
    { S(4),    EV_GO,    0,         0,   0 },
    /* 前から: 猿人 */
    { S(5),    EV_SPAWN, ET_E01,   22,  10 },
    { S(6.5),  EV_SPAWN, ET_E01,   22, -10 },
    { S(8),    EV_SPAWN, ET_E01,   22,  10 },
    { S(8),    EV_SPAWN, ET_E01,   26, -10 },
    { S(10),   EV_SPAWN, ET_E01,   22, -10 },
    { S(11.5), EV_SPAWN, ET_E01,   24,  10 },
    { S(12.5), EV_SPAWN, ET_E01,   22, -10 },
    { S(14),   EV_SPAWN, ET_E01,   22,  10 },
    { S(14),   EV_SPAWN, ET_E01,   25, -10 },
    { S(16),   EV_SPAWN, ET_E01,   22, -10 },
    { S(17),   EV_SPAWN, ET_E01,   22,  10 },
    /* 後ろから: コウモリ人の追跡 */
    { S(19),   EV_DIR,   DIR_BACK,  0,   0 },
    { S(20),   EV_SPAWN, ET_E02,  -14,   6 },
    { S(22.5), EV_SPAWN, ET_E02,  -14,  -6 },
    { S(25),   EV_SPAWN, ET_E02,  -16,   6 },
    { S(26),   EV_SPAWN, ET_E02,  -18,  -6 },
    { S(29),   EV_SPAWN, ET_E02,  -14,   6 },
    { S(30),   EV_SPAWN, ET_E02,  -16,  -6 },
    /* また前から */
    { S(34),   EV_DIR,   DIR_FRONT, 0,   0 },
    { S(35),   EV_SPAWN, ET_E01,   22,  10 },
    { S(36),   EV_SPAWN, ET_E01,   22, -10 },
    { S(37.5), EV_SPAWN, ET_E01,   22,  10 },
    { S(37.5), EV_SPAWN, ET_E01,   25, -10 },
    { S(40),   EV_SPAWN, ET_E01,   22, -10 },
    { S(41),   EV_SPAWN, ET_E01,   22,  10 },
    { S(43),   EV_SPAWN, ET_E01,   22, -10 },
    { S(44),   EV_SPAWN, ET_E01,   24,  10 },
    /* 小ボス: 大猿人の親方(撃破までスクリプトは止まる) */
    { S(47),   EV_BOSS,  ET_B01,   16,   0 },
    { S(47.1), EV_DIR,   DIR_NONE,  0,   0 },
    { S(50),   EV_END,   0,         0,   0 },
};

const Stage stage_cave = { cave_rail, cave_pal, cave_events };
