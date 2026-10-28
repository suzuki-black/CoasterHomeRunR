/* stage3_forest.c — ステージ 3: 森(仕様書 5 章 #5)
 *
 * 流れ: 洞窟を出て森の小道へ → 木漏れ日の空き地(敵は出さない)→ 周回 A(キリン人(前)と魔法使い、後ろからコウモリ人)
 *       → 小川の丸太橋(敵は出さない)→ 周回 B(雑魚)→ 倒木のトンネル(敵は出さない)→ 周回 C(森の番人(巨大コウモリ人))
 *       → 祠と鳥居をくぐる → ステージ 4(鉄橋)
 * 周回区間では、敵(ボスも)が残っている間は同じコースを回り続ける(見せ場に敵を持ち込まないように)。周回区間の外は位置で(EV_AT)、中は撃破で(WAVE と EV_WAIT)進める。
 * ステージデータは定数だけで書き、読み込み先(0xB400)に合わせて単独でリンクする(stage1_cave.c と同じ)。
 */
#include "game.h"
#include "env.h"
#include "sound.h"
#include "pcm.h"

#define S(sec) ((u16)((sec) * 60))

/* (長さ[m], ヨー角の変化(65536 = 1 周、正 = 右), 1m あたりの高さ変化(1/16m), 速度(m/秒 × 4)) */
static const RailSeg forest_rail[] = {
    {  16,       0,   0, 16 },   /*   0〜 16m 洞窟を出たところ */
    {  20,       0,   0, 16 },   /*  16〜 36m 見せ場: 木漏れ日の空き地 */
    {  24,       0,   0, 16 },   /*  36〜 60m 周回 A の手前 */
    LOOP_TURN(16),               /*  60〜172m 周回 A: 雑魚 */
    {  24,       0,   0, 18 },   /* 172〜196m 周回を抜ける */
    {  12,       0,   0, 16 },   /* 196〜208m 見せ場: 小川の丸太橋 */
    {  24,       0,   0, 16 },   /* 208〜232m 周回 B の手前 */
    LOOP_TURN(16),               /* 232〜344m 周回 B: 雑魚 */
    {  24,       0,   0, 18 },   /* 344〜368m 周回を抜ける */
    {  12,       0,   0, 16 },   /* 368〜380m 見せ場: 倒木のトンネル */
    {  24,       0,   0, 16 },   /* 380〜404m 周回 C の手前 */
    LOOP_TURN(14),               /* 404〜516m 周回 C: 森の番人 */
    {  24,       0,   0, 20 },   /* 516〜540m 周回を抜ける */
    {  32,       0,   0, 16 },   /* 540〜572m 祠と鳥居 → 森の出口 */
    {   0,       0,   0,  0 },
};
/* 周回区間: (始まりのリング, 長さのリング数)。リング = 4m */
static const u8 forest_loops[] = { 60 / 4, 112 / 4,  232 / 4, 112 / 4,  404 / 4, 112 / 4,  0 };

/* 地形: (このリングまで, 種類)。リング = 4m。森の小道の途中に、木漏れ日の空き地(16〜36m)、小川の丸太橋(196〜208m)、
 * 倒木のトンネル(368〜380m)、苔むした祠と鳥居(544m。森の番人を倒した後にくぐる)。
 * 敵の出ない区間は約 10 秒まで: 見せ場は周回の間に 1 つずつ置く(文字の出ない景色を長く見せないように。2026-10-25 ユーザーの指摘) */
static const u8 forest_zones[] = {
    10,                          /* 屋外のレールの色(暗い色) */
    4, K_FOREST, 9, K_GLADE, 49, K_FOREST, 52, K_STREAM, 92, K_FOREST, 95, K_HOLLOW,
    136, K_FOREST, 137, K_SHRINE, 0xFF, K_FOREST,
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


#define AT(m, cmd) (m), (cmd) | EV_AT

static const Event forest_events[] = {
    /* ---- 森へ。空き地までは景色を見せる ---- */
    { S(0),    EV_DIR,   DIR_NONE,    0,   0 },
    { S(0),    EV_BGM,   SONG_STAGE3, 0,   0 },
    { AT(4, EV_TEXT),    T_FOREST,    0,   0 },
    /* ---- 周回 A(60〜172m。敵が残っていれば何周でも): 前からキリン人(槍のリーチが長い)と魔法使い ---- */
    { AT(38, EV_DIR),    DIR_FRONT,   0,   0 },
    { AT(38, EV_SPAWN),  ET_E03,      22, -10 },
    { AT(48, EV_SPAWN),  ET_E03,      22,  10 },
    { AT(58, EV_SPAWN),  ET_E03,      22, -10 },
    { AT(60, EV_WAIT),       15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E03,      24,  10 },
    { WAVE(EV_SPAWN),        ET_E05,      20,   8 },
    { WAVE(EV_SPAWN),        ET_E03,      22, -10 },
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_MEDKIT,   18,  10 },
    { WAVE(EV_SPAWN),        ET_E03,      22,  10 },
    { WAVE(EV_SPAWN),        ET_E03,      25, -10 },
    /* 後ろからコウモリ人と魔法使い */
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E05,      20,  -8 },
    { WAVE(EV_SPAWN),        ET_E05,     -12,   6 },
    { WAVE(EV_SPAWN),        ET_E02,     -14,  -6 },
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E05,     -14,  -6 },
    { WAVE(EV_SPAWN),        ET_E02,     -14,   6 },
    { WAVE(EV_SPAWN),        ET_E02,     -16,  -6 },
    /* 前から: キリン人・魔法使い・救急箱 */
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E03,      22,  10 },
    { WAVE(EV_SPAWN),        ET_E05,      20,  -8 },
    { WAVE(EV_SPAWN),        ET_MEDKIT,   18, -10 },
    /* ---- 周回を抜けた: 小川の丸太橋(敵は出さない) ---- */
    { AT(174, EV_DIR), DIR_NONE, 0, 0 },
    /* ---- 周回 B(232〜344m): 前からキリン人と魔法使い、後ろからコウモリ人 ---- */
    { AT(210, EV_DIR), DIR_FRONT, 0, 0 },
    { AT(210, EV_SPAWN), ET_E03, 22, -10 },
    { AT(218, EV_SPAWN), ET_E05, 20, 8 },
    { AT(226, EV_SPAWN), ET_E03, 22, 10 },
    { AT(226, EV_SPAWN), ET_E03, 25, -10 },
    { AT(232, EV_WAIT),      15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E03, 22, -10 },
    { WAVE(EV_SPAWN),        ET_MEDKIT, 18, 10 },
    { WAVE(EV_SPAWN),        ET_E05, 20, -8 },
    { WAVE(EV_SPAWN),        ET_E02, -14, 6 },
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E05, -12, -6 },
    { WAVE(EV_SPAWN),        ET_E02, -14, -6 },
    { WAVE(EV_SPAWN),        ET_E02, -16, 6 },
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E03, 22, 10 },
    { WAVE(EV_SPAWN),        ET_E05, 20, 8 },
    { WAVE(EV_SPAWN),        ET_E03, 22, -10 },
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E03, 24, 10 },
    /* ---- 周回を抜けた: 倒木のトンネル(敵は出さない) ---- */
    { AT(346, EV_DIR), DIR_NONE, 0, 0 },
    /* ---- 周回 C(404〜516m): 雑魚 → 森の番人(撃破までスクリプトは止まる) ---- */
    { AT(382, EV_DIR), DIR_FRONT, 0, 0 },
    { AT(382, EV_SPAWN), ET_E03, 22, -10 },
    { AT(390, EV_SPAWN), ET_E05, 20, 8 },
    { AT(396, EV_SPAWN), ET_MISBOX, 18, 10 },
    { AT(400, EV_SPAWN), ET_E03, 22, 10 },
    { AT(404, EV_WAIT),      15, 0, 0 },
    { WAVE(EV_TEXT),         T_WARNING, 0, 0 },
    { WAVE(EV_VOICE),        VOICE_WARNING, 0, 0 },
    { WAVE(EV_SPEED),        6, 0, 0 },
    { WAVE(EV_WAIT),         50, 0, 0 },
    { WAVE(EV_BOSS),         ET_B02, 20, 0 },
    { WAVE(EV_SPEED),        0, 0, 0 },
    { WAVE(EV_TEXT),         T_CLEAR, 0, 0 },   /* 最後のボスを倒した: 面のクリア */
    /* ---- 周回を抜けた: クリア。祠と鳥居をくぐって森の出口へ ---- */
    { AT(518, EV_DIR), DIR_NONE, 0, 0 },
    { AT(518, EV_SPEED), 20, 0, 0 },
    { AT(562, EV_END), 0, 0, 0 },
};

const Stage stage_data = {
    forest_rail, forest_pal, forest_events, forest_captions,
    2,
    0,
    0,          /* 演出は飛ばさない(走りながら始まる) */
    SONG_STAGE3,
    forest_zones, 0x7A3C,
    "STAGE 3", "THE FOREST",
    0, 0,
    { 0, 0 }, 0, 0,
    0,          /* やさしい進行はしない */
    0, 0,
    forest_loops,
};
