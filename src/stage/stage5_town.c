/* stage5_town.c — ステージ 5: 町(仕様書 5 章 #6)
 *
 * 流れ: 鉄橋を渡ると町の中(なぜか道にレールが敷いてある)→ 屋台と提灯の通り(敵は出さない)
 *       → 周回 A(前から E01〜E05 の混成、後ろからコウモリ人・魔法使い・追ってくる猿人)→ 運河の橋(敵は出さない)
 *       → 周回 B(混成)→ 噴水の広場で分岐(左 = 救急箱が多い / 右 = 敵ばかり)→ 合流して
 *       周回 C(祭りの山車 B03。獣人たちが引いて後ろから追ってくる。上の猿人が花火を撃ち、ときどきぶつけてくる)
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
    {  24,       0,   0, 18 },   /* 380〜404m 見せ場: 噴水の広場 → 分岐点(L・R の看板だけ) */
    {   0,       0,   0,  0 },
};
static const RailSeg town_left[] = {       /* 左: 34 度左へ出て、67 度右へ戻し、34 度左で元の向き(ステージ 1 と同じひし形) */
    {  16, -0x1800,  0, 18 },
    {  40,       0,  0, 18 },
    {  16,  0x3000,  0, 18 },
    {  40,       0,  0, 18 },
    {  16, -0x1800,  0, 18 },
    {   0,       0,  0,  0 },
};
static const RailSeg town_right[] = {      /* 右: 左の鏡写し */
    {  16,  0x1800,  0, 18 },
    {  40,       0,  0, 18 },
    {  16, -0x3000,  0, 18 },
    {  40,       0,  0, 18 },
    {  16,  0x1800,  0, 18 },
    {   0,       0,  0,  0 },
};
static const RailSeg town_tail[] = {       /* 532m〜 合流後 */
    {  24,       0,   0, 16 },   /* 532〜556m 周回 C の手前 */
    LOOP_TURN(14),               /* 556〜668m 周回 C: 祭りの山車 */
    {  24,       0,   0, 20 },   /* 668〜692m 周回を抜ける */
    {   8,       0,   0, 20 },   /* 692〜700m 町の門 */
    {  24,       0,   0, 20 },   /* 700〜724m 村へ */
    {   0,       0,   0,  0 },
};
/* 周回区間: (始まりのリング, 長さのリング数)。リング = 4m */
static const u8 town_loops[] = { 72 / 4, 112 / 4,  244 / 4, 112 / 4,  556 / 4, 112 / 4,  0 };

/* 地形: (このリングまで, 種類)。リング = 4m。祭りの町: 屋台と提灯の通り(8〜36m)、運河の橋(208〜220m)、噴水の広場(380〜400m)、
 * 町の門(692〜700m。山車を倒した後にくぐって村へ向かう)。広場は分岐点の先まで(380〜424m。分かれる線路が家に重ならないように)。
 * 敵の出ない区間は約 10 秒まで: 見せ場は周回の間に 1 つずつ置く(2026-10-25 ユーザーの指摘) */
static const u8 town_zones[] = {
    10,                          /* レールの色(暗い色) */
    2, K_TOWN, 9, K_FAIR, 52, K_TOWN, 55, K_CANAL, 95, K_TOWN, 106, K_PLAZA, 173, K_TOWN, 175, K_GATE,
    0xFF, K_TOWN,
};

/* 9: 戸 10: 窓・レール 11: 道 12: 壁(クリーム) 13: 屋根(暗い赤) 14: 壁(青灰) 15: 空 */
static const u8 town_pal[14] = {
    0x31, 2,  0x12, 1,  0x33, 3,  0x64, 6,  0x41, 1,  0x45, 5,  0x26, 4,
};

enum { T_NONE, T_WHY, T_FLOAT, T_NEAR, T_CLEAR, T_PICK, T_LROAD, T_RROAD };
static const Caption town_captions[] = {
    { "WHY RAILS HERE?", C_YELLOW, 1 },
    { "FESTIVAL FLOAT!", C_RED,    1 },
    { "VILLAGE IS NEAR", C_YELLOW, 1 },
    { "STAGE CLEAR!",   C_YELLOW, 1 },
    { "SHOOT L OR R!",  C_WHITE,  1 },
    { "FIRST AID ROAD", C_YELLOW, 1 },
    { "BEAST ROAD!",    C_RED,    1 },
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
    /* ---- 分岐: 広場の先で線路が Y 字に分かれる手前、左の線路の脇に L、右の脇に R(攻撃しない)。先に倒した側へ。倒さなければ右(規定) ---- */
    { AT(368, EV_TEXT),              T_PICK, 0, 0 },
    { AT(374, EV_SPAWN),             ET_SIGNL, 24, -20 },
    { AT(374, EV_SPAWN),             ET_SIGNR, 24, 20 },
    { AT(378, EV_DIR),               DIR_FRONT, 0, 0 },
    { AT(378, EV_SPAWN),             ET_E01, 22, 0 },       /* 広場に猿人(周回 B を抜けてから約 10 秒で敵が来るように) */
    /* ---- 左: 救急箱が多い(敵は少なめ) / 右: 敵ばかり(404〜532m) ---- */
    { AT(406, EV_TEXT | EV_ONLY_L),  T_LROAD, 0, 0 },
    { AT(406, EV_TEXT | EV_ONLY_R),  T_RROAD, 0, 0 },
    { AT(408, EV_SPAWN | EV_ONLY_R), ET_E01, 22, 10 },
    { AT(408, EV_SPAWN | EV_ONLY_R), ET_E01, 24, -10 },
    { AT(410, EV_SPAWN | EV_ONLY_L), ET_E01, 22, 10 },
    { AT(416, EV_SPAWN | EV_ONLY_L), ET_MEDKIT, 18, 10 },
    { AT(422, EV_SPAWN | EV_ONLY_R), ET_E03, 22, -10 },
    { AT(422, EV_SPAWN | EV_ONLY_R), ET_E04, 26, 22 },
    { AT(430, EV_SPAWN | EV_ONLY_L), ET_E03, 22, -10 },
    { AT(436, EV_SPAWN | EV_ONLY_R), ET_E02, -14, 6 },
    { AT(436, EV_SPAWN | EV_ONLY_R), ET_E05, -12, -6 },
    { AT(440, EV_SPAWN | EV_ONLY_L), ET_MEDKIT, 18, -10 },
    { AT(452, EV_SPAWN | EV_ONLY_R), ET_E01, 22, 10 },
    { AT(452, EV_SPAWN | EV_ONLY_R), ET_E03, 24, -10 },
    { AT(456, EV_SPAWN | EV_ONLY_L), ET_E05, -12, 6 },
    { AT(468, EV_SPAWN | EV_ONLY_R), ET_E02, -14, -6 },
    { AT(468, EV_SPAWN | EV_ONLY_R), ET_E01, -10, 8 },
    { AT(470, EV_SPAWN | EV_ONLY_L), ET_E04, 26, -22 },
    { AT(476, EV_SPAWN | EV_ONLY_L), ET_MEDKIT, 18, 10 },
    { AT(484, EV_SPAWN | EV_ONLY_R), ET_E04, 26, -22 },
    { AT(484, EV_SPAWN | EV_ONLY_R), ET_E05, 20, 8 },
    { AT(496, EV_SPAWN | EV_ONLY_L), ET_E02, -14, 6 },
    { AT(500, EV_SPAWN | EV_ONLY_R), ET_E01, 22, -10 },
    { AT(500, EV_SPAWN | EV_ONLY_R), ET_E03, 24, 10 },
    /* ---- 合流: 周回 C(556〜668m)の手前。雑魚 → 祭りの山車(後ろから。撃破までスクリプトは止まる) ---- */
    { AT(534, EV_SPAWN), ET_E01, 22, 10 },
    { AT(534, EV_SPAWN), ET_E04, 26, -22 },
    { AT(542, EV_SPAWN), ET_E03, 22, 10 },
    { AT(542, EV_SPAWN), ET_E01, 24, -10 },
    /* 誘導弾の箱(山車の前。山車は後ろから来るので、振り向く前に) */
    { AT(548, EV_SPAWN), ET_MISBOX, 18, -10 },
    { AT(554, EV_DIR), DIR_BACK, 0, 0 },
    { AT(556, EV_WAIT),      15, 0, 0 },
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
    { AT(670, EV_DIR), DIR_NONE, 0, 0 },
    { AT(670, EV_TEXT), T_NEAR, 0, 0 },
    { AT(714, EV_END), 0, 0, 0 },
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
    { town_left, town_right }, town_tail,
    1,          /* 撃たなければ右(敵ばかり) */
    0,          /* やさしい進行はしない */
    0, 0,
    town_loops,
};
