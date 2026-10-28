/* stage5_town.c — ステージ 5: 町(仕様書 5 章 #6)
 *
 * 流れ: 鉄橋を渡ると町の中(なぜか道にレールが敷いてある)→ 屋台と提灯の通り(敵は出さない)
 *       → 周回 A(前から E01〜E05 の混成、後ろからコウモリ人・魔法使い・追ってくる猿人)→ 運河の橋(敵は出さない)
 *       → 周回 B(混成)→ 噴水の広場(敵は出さない)
 *       → 周回 C(祭りの山車 B03。獣人たちが引いて後ろから追ってくる。上の猿人が花火を撃ち、ときどきぶつけてくる)
 *       → 町の門をくぐって村へ
 * 周回区間では、敵(ボスも)が残っている間は同じコースを回り続ける(見せ場に敵を持ち込まないように)。周回区間の外は位置で(EV_AT)、中は撃破で(WAVE と EV_WAIT)進める。
 * ステージデータは定数だけで書き、読み込み先(0xB400)に合わせて単独でリンクする(stage1_cave.c と同じ)。
 */
#include "game.h"
#include "sound.h"
#include "pcm.h"
#include "env.h"

#define S(sec) ((u16)((sec) * 60))

/* (長さ[m], ヨー角の変化(65536 = 1 周、正 = 右), 1m あたりの高さ変化(1/16m), 速度(m/秒 × 4))。町の角は急に曲がる */
static const RailSeg town_rail[] = {
    {   8,       0,   0, 18 },   /*   0〜  8m */
    {  28,       0,   0, 18 },   /*   8〜 36m 見せ場: 屋台と提灯の通り */
    {  12,  0x3000,   0, 16 },   /* 右の角 */
    {  24,       0,   0, 18 },   /*  48〜 72m 周回 A の手前 */
    LOOP_TURN(18),               /*  72〜184m 周回 A: 雑魚 */
    {  24,       0,   0, 20 },   /* 184〜208m 周回を抜ける */
    {  12,       0,   0, 18 },   /* 208〜220m 見せ場: 運河の橋 */
    {  24,       0,   0, 18 },   /* 220〜244m 周回 B の手前 */
    LOOP_TURN(16),               /* 244〜356m 周回 B: 雑魚 */
    {  24,       0,   0, 20 },   /* 356〜380m 周回を抜ける */
    {  20,       0,   0, 18 },   /* 380〜400m 見せ場: 噴水の広場 */
    {  24,       0,   0, 16 },   /* 400〜424m 周回 C の手前 */
    LOOP_TURN(14),               /* 424〜536m 周回 C: 祭りの山車 */
    {  24,       0,   0, 20 },   /* 536〜560m 周回を抜ける */
    {   8,       0,   0, 20 },   /* 560〜568m 町の門 */
    {  24,       0,   0, 20 },   /* 568〜592m 村へ */
    {   0,       0,   0,  0 },
};
/* 周回区間: (始まりのリング, 長さのリング数)。リング = 4m */
static const u8 town_loops[] = { 72 / 4, 112 / 4,  244 / 4, 112 / 4,  424 / 4, 112 / 4,  0 };

/* 地形: (このリングまで, 種類)。リング = 4m。祭りの町: 屋台と提灯の通り(8〜36m)、運河の橋(208〜220m)、噴水の広場(380〜400m)、
 * 町の門(560〜568m。山車を倒した後にくぐって村へ向かう)。
 * 敵の出ない区間は約 10 秒まで: 見せ場は周回の間に 1 つずつ置く(2026-10-25 ユーザーの指摘) */
static const u8 town_zones[] = {
    10,                          /* レールの色(暗い色) */
    2, K_TOWN, 9, K_FAIR, 52, K_TOWN, 55, K_CANAL, 95, K_TOWN, 100, K_PLAZA, 140, K_TOWN, 142, K_GATE,
    0xFF, K_TOWN,
};

/* 9: 戸 10: 窓・レール 11: 道 12: 壁(クリーム) 13: 屋根(暗い赤) 14: 壁(青灰) 15: 空 */
static const u8 town_pal[14] = {
    0x31, 2,  0x12, 1,  0x33, 3,  0x64, 6,  0x41, 1,  0x45, 5,  0x26, 4,
};

enum { T_NONE, T_WHY, T_FLOAT, T_NEAR, T_CLEAR };
static const Caption town_captions[] = {
    { "WHY RAILS HERE?", C_YELLOW, 1 },
    { "FESTIVAL FLOAT!", C_RED,    1 },
    { "VILLAGE IS NEAR", C_YELLOW, 1 },
    { "STAGE CLEAR!",   C_YELLOW, 1 },
};


#define AT(m, cmd) (m), (cmd) | EV_AT

static const Event town_events[] = {
    { S(0),    EV_DIR,   DIR_NONE,    0,   0 },
    { S(0),    EV_BGM,   SONG_STAGE5, 0,   0 },
    { AT(2, EV_TEXT),    T_WHY,       0,   0 },
    /* ---- 周回 A(72〜184m。敵が残っていれば何周でも): 前から混成(弓兵は屋根の上) ---- */
    { AT(38, EV_DIR),    DIR_FRONT,   0,   0 },
    { AT(38, EV_SPAWN),  ET_E01,      22,  10 },
    { AT(38, EV_SPAWN),  ET_E01,      24, -10 },
    { AT(48, EV_SPAWN),  ET_E01,      22,  10 },
    { AT(48, EV_SPAWN),  ET_E03,      24, -10 },
    { AT(58, EV_SPAWN),  ET_E04,      24,  22 },
    { AT(58, EV_SPAWN),  ET_E05,      20,  -8 },
    { AT(68, EV_SPAWN),  ET_E01,      22, -10 },
    { AT(68, EV_SPAWN),  ET_E01,      24,  10 },
    { AT(72, EV_WAIT),       15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E03,      22,  10 },
    { WAVE(EV_SPAWN),        ET_MEDKIT,   18, -10 },
    { WAVE(EV_SPAWN),        ET_E04,      24, -22 },
    { WAVE(EV_SPAWN),        ET_E04,      26,  22 },
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E03,      22,  10 },
    /* 後ろから: コウモリ人・魔法使い・追ってくる猿人 */
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E02,     -14,   6 },
    { WAVE(EV_SPAWN),        ET_E05,     -12,  -6 },
    { WAVE(EV_SPAWN),        ET_E01,     -10,   8 },
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E05,     -12,   6 },
    { WAVE(EV_SPAWN),        ET_E02,     -14,  -6 },
    { WAVE(EV_SPAWN),        ET_E02,     -16,   6 },
    /* 前から混成 */
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E03,      22, -10 },
    { WAVE(EV_SPAWN),        ET_E05,      20,   8 },
    { WAVE(EV_SPAWN),        ET_MEDKIT,   18,  10 },
    /* ---- 周回を抜けた: 運河の橋(敵は出さない) ---- */
    { AT(186, EV_DIR), DIR_NONE, 0, 0 },
    /* ---- 周回 B(244〜356m): 前から混成、後ろからコウモリ人・魔法使い・追ってくる猿人 ---- */
    { AT(222, EV_DIR), DIR_FRONT, 0, 0 },
    { AT(222, EV_SPAWN), ET_E01, 22, 10 },
    { AT(222, EV_SPAWN), ET_E01, 24, -10 },
    { AT(230, EV_SPAWN), ET_E03, 22, 10 },
    { AT(230, EV_SPAWN), ET_E04, 26, -22 },
    { AT(240, EV_SPAWN), ET_E05, 20, 8 },
    { AT(244, EV_WAIT),      15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E01, 22, -10 },
    { WAVE(EV_SPAWN),        ET_E01, 24, 10 },
    { WAVE(EV_SPAWN),        ET_MEDKIT, 18, -10 },
    { WAVE(EV_SPAWN),        ET_E04, 24, 22 },
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E02, -14, 6 },
    { WAVE(EV_SPAWN),        ET_E05, -12, -6 },
    { WAVE(EV_SPAWN),        ET_E01, -10, 8 },
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E02, -14, -6 },
    { WAVE(EV_SPAWN),        ET_E03, 22, -10 },
    { WAVE(EV_SPAWN),        ET_E05, 20, 8 },
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E01, 22, 10 },
    { WAVE(EV_SPAWN),        ET_E04, 26, -22 },
    /* ---- 周回を抜けた: 噴水の広場(敵は出さない) ---- */
    { AT(358, EV_DIR), DIR_NONE, 0, 0 },
    /* ---- 周回 C(424〜536m): 雑魚 → 祭りの山車(後ろから。撃破までスクリプトは止まる) ---- */
    { AT(402, EV_DIR), DIR_FRONT, 0, 0 },
    { AT(402, EV_SPAWN), ET_E01, 22, 10 },
    { AT(402, EV_SPAWN), ET_E04, 26, -22 },
    { AT(410, EV_SPAWN), ET_E03, 22, 10 },
    { AT(410, EV_SPAWN), ET_E01, 24, -10 },
    /* 誘導弾の箱(山車の前。山車は後ろから来るので、振り向く前に) */
    { AT(416, EV_SPAWN), ET_MISBOX, 18, -10 },
    { AT(422, EV_DIR), DIR_BACK, 0, 0 },
    { AT(424, EV_WAIT),      15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E01, -10, -8 },
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_TEXT),         T_FLOAT, 0, 0 },
    { WAVE(EV_VOICE),        VOICE_WARNING, 0, 0 },
    { WAVE(EV_SPEED),        14, 0, 0 },
    { WAVE(EV_WAIT),         50, 0, 0 },
    { WAVE(EV_BOSS),         ET_B03, -20, 0 },
    { WAVE(EV_SPEED),        0, 0, 0 },
    { WAVE(EV_TEXT),         T_CLEAR, 0, 0 },   /* 最後のボスを倒した: 面のクリア */
    /* ---- 周回を抜けた: 町の門をくぐって村へ ---- */
    { AT(538, EV_DIR), DIR_NONE, 0, 0 },
    { AT(538, EV_TEXT), T_NEAR, 0, 0 },
    { AT(582, EV_END), 0, 0, 0 },
};

const Stage stage_data = {
    town_rail, town_pal, town_events, town_captions,
    2,
    0,
    0,          /* 演出は飛ばさない(走りながら始まる) */
    SONG_STAGE5,
    town_zones, 0x3C71,
    "STAGE 5", "THE TOWN",
    0, 0,
    { 0, 0 }, 0, 0,
    0,          /* やさしい進行はしない */
    0, 0,
    town_loops,
};
