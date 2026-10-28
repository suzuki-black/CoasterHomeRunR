/* stage2_lake.c — ステージ 2: 地底湖(仕様書 5 章 #2〜#4 を置き換え)
 *
 * 流れ: 坑道を抜けると大きな地底湖(A LAKE?!)→ レールが湖で途切れ、なぜかコースターごと筏に乗って渡る(ON A RAFT?!)
 *       → 滝(敵は出さない)→ 周回 A(岩棚の弓兵・コウモリ人・水に浮いた木箱の救急箱)
 *       → 沈んだトロッコの残骸(敵は出さない)→ 周回 B(中ボスの湖の主)→ 光る結晶の洞(敵は出さない)
 *       → 周回 C(大ナマズ人。前の水面に出る大ボス)→ 岩の門 → 向こう岸でまたレールに戻り(LAND HO!)、出口へ
 * 周回区間では、敵(ボスも)が残っている間は同じコースを回り続ける(見せ場に敵を持ち込まないように)。
 * 周回区間の外は位置で(EV_AT)、中は撃破で(WAVE と EV_WAIT)進める。
 * ステージデータは定数だけで書き、読み込み先(0xB400)に合わせて単独でリンクする(stage1_cave.c と同じ)。
 */
#include "game.h"
#include "env.h"
#include "sound.h"
#include "pcm.h"

#define S(sec) ((u16)((sec) * 60))

/* (長さ[m], ヨー角の変化(65536 = 1 周、正 = 右), 1m あたりの高さ変化(1/16m), 速度(m/秒 × 4))。湖の上は筏の進む道筋 */
static const RailSeg lake_rail[] = {
    {  28,       0,   0, 16 },   /*   0〜 28m 坑道 */
    {   8,       0,   0, 18 },   /*  28〜 36m 地底湖(筏) */
    {  16,       0,   0, 14 },   /*  36〜 52m 見せ場: 滝(ゆっくり) */
    {  24,       0,   0, 16 },   /*  52〜 76m 周回 A の手前 */
    LOOP_TURN(16),               /*  76〜188m 周回 A: 雑魚 */
    {  24,       0,   0, 18 },   /* 188〜212m 周回を抜ける */
    {   8,       0,   0, 16 },   /* 212〜220m 見せ場: 沈んだトロッコの残骸 */
    {  24,       0,   0, 16 },   /* 220〜244m 周回 B の手前 */
    LOOP_TURN(14),               /* 244〜356m 周回 B: 中ボス(湖の主) */
    {  24,       0,   0, 18 },   /* 356〜380m 周回を抜ける */
    {  16,       0,   0, 16 },   /* 380〜396m 見せ場: 光る結晶の洞 */
    {  24,       0,   0, 16 },   /* 396〜420m 周回 C の手前 */
    LOOP_TURN(14),               /* 420〜532m 周回 C: 大ナマズ人 */
    {  24,       0,   0, 20 },   /* 532〜556m 周回を抜ける */
    {  12,       0,   0, 18 },   /* 556〜568m 岩の門 */
    {   8,       0,   0, 18 },   /* 568〜576m 向こう岸 */
    {  32,       0,   0, 14 },   /* 576〜608m 向こう岸の坑道 → 出口 */
    {   0,       0,   0,  0 },
};
/* 周回区間: (始まりのリング, 長さのリング数)。リング = 4m */
static const u8 lake_loops[] = { 76 / 4, 112 / 4,  244 / 4, 112 / 4,  420 / 4, 112 / 4,  0 };

/* 地形: (このリングまで, 種類)。リング = 4m。湖の途中に、滝(36〜52m)、沈んだトロッコの残骸(212〜220m)、光る結晶の洞(380〜396m)、
 * 岩の門(556〜568m)。周回区間はただの湖面。
 * 敵の出ない区間は約 10 秒まで: 見せ場は周回の間に 1 つずつ置き、大ナマズ人を倒した後は岩の門をくぐって短く終える
 * (文字の出ない景色を 20 秒以上見せていたら、待たされる・敵が出なくなったと感じられた。2026-10-25 ユーザーの指摘) */
static const u8 lake_zones[] = {
    10,                          /* レールの色(暗い岩) */
    7, K_EXIT, 9, K_LAKE, 13, K_FALLS, 53, K_LAKE, 55, K_WRECK, 95, K_LAKE, 99, K_CRYSTAL, 139, K_LAKE, 142, K_ARCH,
    144, K_LAKE, 0xFF, K_EXIT,
};

/* 9: 木 10: 暗い岩・レール 11: 岩 12: 鍾乳石 13: 水 14: 明るい水 15: (未使用) */
static const u8 lake_pal[14] = {
    0x41, 2,  0x12, 1,  0x32, 2,  0x44, 4,  0x14, 2,  0x25, 4,  0x55, 5,
};

enum { T_NONE, T_LAKE, T_RAFT, T_WARNING, T_LAND, T_EXIT, T_CATFISH, T_CLEAR };
static const Caption lake_captions[] = {
    { "A LAKE?!",       C_WHITE,  2 },
    { "ON A RAFT?!",    C_YELLOW, 1 },
    { "WARNING!",       C_RED,    2 },
    { "LAND HO!",       C_YELLOW, 2 },
    { "SUNLIGHT!",      C_WHITE,  1 },
    { "GIANT CATFISH!", C_RED,    1 },
    { "STAGE CLEAR!",   C_YELLOW, 1 },
};


#define AT(m, cmd) (m), (cmd) | EV_AT

static const Event lake_events[] = {
    { S(0),    EV_DIR,   DIR_NONE,    0,   0 },
    { S(0),    EV_BGM,   SONG_STAGE2, 0,   0 },
    /* ---- 坑道 → 地底湖 → 筏 → 滝: まずどんなステージかを見せる(敵は出さない)。演出は少し大げさに ---- */
    { AT(18, EV_FX),        SE_TRAIN, 3, 0 },     /* 湖が見えて、ガクンと速度を落とす */
    { AT(18, EV_SPEED),     10, 0, 0 },           /* 2.5m/秒で湖を見せる */
    { AT(20, EV_TEXT),      T_LAKE, 0, 0 },
    { AT(24, EV_SPEED),     0, 0, 0 },
    { AT(28, EV_FX),        SE_TRAIN, 6, 0 },     /* レールが途切れ、筏に乗り移る衝撃 */
    { AT(30, EV_TEXT),      T_RAFT, 0, 0 },
    /* ---- 周回 A(76〜188m。敵が残っていれば何周でも): 前からコウモリ人(1 面から追ってきた群れ)、岩棚の弓兵と木箱の救急箱 ---- */
    { AT(54, EV_DIR), DIR_FRONT, 0, 0 },
    { AT(54, EV_SPAWN), ET_E02, 20, 6 },
    { AT(54, EV_SPAWN), ET_E02, 22, -6 },
    { AT(62, EV_SPAWN), ET_E04, 24, 36 },
    { AT(62, EV_SPAWN), ET_E04, 26, -36 },
    { AT(74, EV_SPAWN), ET_E04, 24, -36 },
    { AT(74, EV_SPAWN), ET_CRATE, 22, 10 },
    { AT(76, EV_WAIT),       15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E04, 24, 36 },
    { WAVE(EV_SPAWN),        ET_E04, 27, -36 },
    /* 後ろからコウモリ人 */
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E02, -14, 6 },
    { WAVE(EV_SPAWN),        ET_E02, -16, -6 },
    { WAVE(EV_SPAWN),        ET_E02, -14, -6 },
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E02, -16, 6 },
    { WAVE(EV_SPAWN),        ET_E04, 24, -36 },
    { WAVE(EV_SPAWN),        ET_E04, 26, 36 },
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_CRATE, 20, -10 },
    { WAVE(EV_SPAWN),        ET_E04, 24, 36 },
    { WAVE(EV_SPAWN),        ET_E04, 27, -36 },
    { WAVE(EV_SPAWN),        ET_E02, -14, 6 },
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E02, -16, -6 },
    { WAVE(EV_SPAWN),        ET_E04, 24, -36 },
    { WAVE(EV_SPAWN),        ET_E04, 26, 36 },
    /* ---- 周回を抜けた: 沈んだトロッコの残骸(敵は出さない) ---- */
    { AT(190, EV_DIR), DIR_NONE, 0, 0 },
    /* ---- 周回 B(244〜356m): 弓兵 → 湖の主(後ろから追ってくる中ボス。撃破までスクリプトは止まる) ---- */
    { AT(222, EV_DIR), DIR_FRONT, 0, 0 },
    { AT(222, EV_SPAWN), ET_E04, 24, 36 },
    { AT(222, EV_SPAWN), ET_E04, 26, -36 },
    { AT(230, EV_SPAWN), ET_CRATE, 20, -10 },
    { AT(234, EV_SPAWN), ET_MISBOX, 20, 10 },
    /* 湖の主: 後ろから追ってくる中ボス。WARNING! の後、少し置いてから出る */
    { AT(238, EV_DIR), DIR_BACK, 0, 0 },
    { AT(242, EV_TEXT), T_WARNING, 0, 0 },
    { AT(242, EV_VOICE), VOICE_WARNING, 0, 0 },
    { AT(242, EV_SPEED), 12, 0, 0 },
    { AT(244, EV_WAIT),      15, 0, 0 },
    { WAVE(EV_BOSS),         ET_BOSS2, -18, 0 },
    { WAVE(EV_SPEED),        0, 0, 0 },
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E04, 24, -36 },
    { WAVE(EV_SPAWN),        ET_E04, 26, 36 },
    { WAVE(EV_SPAWN),        ET_E02, -14, 6 },
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E02, -16, -6 },
    { WAVE(EV_SPAWN),        ET_E04, 24, 36 },
    { WAVE(EV_SPAWN),        ET_E04, 26, -36 },
    /* ---- 周回を抜けた: 光る結晶の洞(敵は出さない) ---- */
    { AT(358, EV_DIR), DIR_NONE, 0, 0 },
    /* ---- 周回 C(420〜532m): コウモリ人と弓兵 → 大ナマズ人(湖の終わり近く、前の水面に出る大ボス) ---- */
    { AT(398, EV_DIR), DIR_BACK, 0, 0 },
    { AT(398, EV_SPAWN), ET_E02, -14, 6 },
    { AT(398, EV_SPAWN), ET_E02, -16, -6 },
    { AT(410, EV_DIR), DIR_FRONT, 0, 0 },
    { AT(410, EV_SPAWN), ET_E04, 24, 36 },
    { AT(410, EV_SPAWN), ET_E04, 26, -36 },
    { AT(418, EV_SPAWN), ET_CRATE, 20, -10 },
    { AT(420, EV_WAIT),      15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E04, 24, -36 },
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_TEXT),         T_WARNING, 0, 0 },
    { WAVE(EV_VOICE),        VOICE_WARNING, 0, 0 },
    { WAVE(EV_SPEED),        12, 0, 0 },
    { WAVE(EV_WAIT),         50, 0, 0 },
    { WAVE(EV_TEXT),         T_CATFISH, 0, 0 },
    { WAVE(EV_BOSS),         ET_B08, 24, 0 },
    { WAVE(EV_SPEED),        0, 0, 0 },
    { WAVE(EV_TEXT),         T_CLEAR, 0, 0 },   /* 最後のボスを倒した: 面のクリア */
    /* ---- 周回を抜けた: 岩の門をくぐって向こう岸 → 出口 ---- */
    { AT(534, EV_DIR), DIR_NONE, 0, 0 },
    { AT(534, EV_SPEED), 22, 0, 0 },
    { AT(564, EV_TEXT), T_LAND, 0, 0 },
    { AT(584, EV_TEXT), T_EXIT, 0, 0 },
    { AT(594, EV_END), 0, 0, 0 },
};

const Stage stage_data = {
    lake_rail, lake_pal, lake_events, lake_captions,
    2,          /* 坑道の途中から */
    0,
    0,          /* 演出は飛ばさない(走りながら始まる) */
    SONG_STAGE2,
    lake_zones, 0x2B1D,
    "STAGE 2", "THE LAKE",
    0, 0,
    { 0, 0 }, 0, 0,
    1,          /* やさしい進行 */
    0, 0,
    lake_loops,
};
