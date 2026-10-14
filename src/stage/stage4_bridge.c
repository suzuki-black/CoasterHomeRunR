/* stage4_bridge.c — ステージ 4: 海の鉄橋(森と町の間)
 *
 * 流れ: 森を抜けた岬のトンネルから飛び出すと、なぜか周りは海で、長い鉄橋(トラス)が続いている
 *       → 前の海から半魚人が跳び上がってくる → 後ろからも半魚人と追ってくる猿人 → 前から半魚人とコウモリ人
 *       → 大ダコ人(後ろから追ってきて墨を吐き、前の海から足を伸ばして橋をつかむ)→ 向こう岸の港町(ステージ 5)へ
 * 半魚人・大ダコ人の動きと絵は、GAME_SEA を付けてコンパイルした game.c・models.c にだけ入っている(Makefile の game4.ihx)。
 * ステージデータは定数だけで書き、読み込み先(0xB400)に合わせて単独でリンクする(stage1_cave.c と同じ)。
 */
#include "game.h"
#include "env.h"
#include "sound.h"
#include "pcm.h"

#define S(sec) ((u16)((sec) * 60))

/* (長さ[m], ヨー角の変化(65536 = 1 周、正 = 右), 1m あたりの高さ変化(1/16m), 速度(m/秒 × 4))。鉄橋はほぼまっすぐ */
static const RailSeg bridge_rail[] = {
    {  12,       0,   0, 16 },   /* 岬のトンネル */
    {  40,       0,   0, 18 },   /* 鉄橋 */
    {  30,  0x0C00,   0, 18 },   /* ゆるく右へ */
    {  50,       0,   0, 18 },
    {  30, -0x0C00,   0, 18 },   /* ゆるく左へ */
    {  50,       0,   0, 18 },
    {  24,  0x0800,   0, 16 },
    {  70,       0,   0, 14 },   /* 大ダコ人と戦う */
    {  24,       0,   0, 16 },
    {   0,       0,   0,  0 },
};

static const u8 bridge_zones[] = {
    10,                          /* レールの色(暗い色) */
    3,    K_EXIT,                /* リング 0〜2(12m): 岬のトンネル */
    0xFF, K_BRIDGE,
};

/* 嵐の海: 9: 枕木 10: レール・暗い岩 11: 橋の床・岩 12: 海 13: 鉄骨(赤茶) 14: (使わない) 15: 曇り空 */
static const u8 bridge_pal[14] = {
    0x21, 1,  0x11, 1,  0x22, 2,  0x12, 2,  0x41, 2,  0x66, 6,  0x55, 5,
};

enum { T_NONE, T_SEA, T_FISH, T_OCTO, T_LAND };
static const Caption bridge_captions[] = {
    { "THE SEA?!",      C_YELLOW, 1 },
    { "FISH MEN!",      C_RED,    1 },
    { "GIANT OCTOPUS!", C_RED,    1 },
    { "LAND AHEAD!",    C_YELLOW, 1 },
};

static const Event bridge_events[] = {
    { S(0),    EV_DIR,   DIR_NONE,    0,   0 },
    { S(0),    EV_BGM,   SONG_STAGE4, 0,   0 },
    { 13,      EV_AT | EV_TEXT, T_SEA, 0,  0 },   /* トンネルを出たら(位置で) */
    /* ---- 前の海から半魚人が跳び上がってくる ---- */
    { S(5),    EV_DIR,   DIR_FRONT,   0,   0 },
    { S(5),    EV_TEXT,  T_FISH,      0,   0 },
    { S(5),    EV_SPAWN, ET_E06,      20,  26 },
    { S(6.5),  EV_SPAWN, ET_E06,      20, -26 },
    { S(8),    EV_SPAWN, ET_E06,      20,  26 },
    { S(8.5),  EV_SPAWN, ET_E06,      22, -26 },
    { S(10),   EV_SPAWN, ET_MEDKIT,   18,   6 },
    { S(10.5), EV_SPAWN, ET_E02,      22,  -8 },
    { S(12),   EV_SPAWN, ET_E06,      20, -26 },
    { S(12),   EV_SPAWN, ET_E06,      22,  26 },
    /* ---- 後ろから: 半魚人と、走って追ってくる猿人 ---- */
    { S(14),   EV_SPAWN, ET_E02,      22,   8 },
    { S(15.5), EV_DIR,   DIR_BACK,    0,   0 },
    { S(16),   EV_SPAWN, ET_E06,     -10,  26 },
    { S(17.5), EV_SPAWN, ET_E01,     -10,  -8 },
    { S(19),   EV_SPAWN, ET_E06,     -10, -26 },
    { S(20.5), EV_SPAWN, ET_E02,     -14,   6 },
    { S(22),   EV_SPAWN, ET_E06,     -10,  26 },
    { S(23),   EV_SPAWN, ET_E01,     -12,   8 },
    /* ---- 前から: 半魚人とコウモリ人、救急箱 ---- */
    { S(24),   EV_DIR,   DIR_FRONT,   0,   0 },
    { S(24),   EV_SPAWN, ET_E06,      20,  26 },
    { S(25),   EV_SPAWN, ET_E02,      22,  -8 },
    { S(26),   EV_SPAWN, ET_E06,      20, -26 },
    { S(27),   EV_SPAWN, ET_MEDKIT,   18,  -6 },
    { S(28),   EV_SPAWN, ET_E06,      20,  26 },
    { S(28.5), EV_SPAWN, ET_E06,      22, -26 },
    { S(30),   EV_SPAWN, ET_E02,      22,   8 },
    { S(31),   EV_SPAWN, ET_MISBOX,   18,   6 },   /* 誘導弾の箱(大ダコ人の前。大ダコ人は後ろから来るので、振り向く前に) */
    { S(31.5), EV_SPAWN, ET_E06,      20, -26 },
    /* ---- 大ダコ人(後ろから。撃破までスクリプトは止まる。前に足を伸ばしてくる) ---- */
    { S(33),   EV_DIR,   DIR_BACK,    0,   0 },
    { S(33),   EV_TEXT,  T_OCTO,      0,   0 },
    { S(33),   EV_VOICE, VOICE_WARNING, 0, 0 },
    { S(33),   EV_SPEED, 14,          0,   0 },
    { S(34),   EV_BOSS,  ET_B07,     -12,   0 },
    /* ---- 向こう岸へ ---- */
    { S(34.1), EV_DIR,   DIR_NONE,    0,   0 },
    { S(34.1), EV_SPEED, 18,          0,   0 },
    { S(34.1), EV_TEXT,  T_LAND,      0,   0 },
    { S(38),   EV_END,   0,           0,   0 },
};

const Stage stage_data = {
    bridge_rail, bridge_pal, bridge_events, bridge_captions,
    2,
    0,
    0,          /* 演出は飛ばさない(走りながら始まる) */
    SONG_STAGE4,
    bridge_zones, 0x51E7,
    "STAGE 4", "THE BRIDGE",
};
