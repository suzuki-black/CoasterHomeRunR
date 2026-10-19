/* stage5_town.c — ステージ 5: 町(仕様書 5 章 #6)
 *
 * 流れ: 森を抜けると町の中(なぜか道にレールが敷いてある)→ 前から E01〜E05 の混成 → 後ろからコウモリ人・魔法使い・追ってくる猿人
 *       → 祭りの山車 B03(獣人たちが引いて後ろから追ってくる。上の猿人が花火を撃ち、ときどきぶつけてくる)→ 前から混成 → 自宅前へ
 * ステージデータは定数だけで書き、読み込み先(0xB400)に合わせて単独でリンクする(stage1_cave.c と同じ)。
 */
#include "game.h"
#include "sound.h"
#include "pcm.h"
#include "env.h"

#define S(sec) ((u16)((sec) * 60))

/* (長さ[m], ヨー角の変化(65536 = 1 周、正 = 右), 1m あたりの高さ変化(1/16m), 速度(m/秒 × 4))。町の角は急に曲がる */
static const RailSeg town_rail[] = {
    {  20,       0,   0, 18 },
    {  40,       0,   0, 20 },
    {  12,  0x3000,   0, 16 },   /* 右の角 */
    {  40,       0,   0, 20 },
    {  12, -0x3000,   0, 16 },   /* 左の角 */
    {  36,       0,   0, 20 },
    {  40,       0,   0, 14 },   /* 祭りの山車と戦う長い通り */
    {  12,  0x2000,   0, 16 },
    {  40,       0,   0, 22 },
    {  64,       0,   0, 22 },
    {   0,       0,   0,  0 },
};

/* 地形: (このリングまで, 種類)。リング = 4m。祭りの町: 屋台と提灯の通り(24〜60m)、運河の橋(76〜88m)、噴水の広場(128〜148m)、
 * 町の門(184〜192m)。遊ぶと 124m で後ろから祭りの山車が来て 152m 前後で倒し、199m ほどで終わる(2026-10-18 自動プレイで計測)。
 * 山車とは広場で戦い、最後に町の門をくぐって村へ向かう */
static const u8 town_zones[] = {
    10,                          /* レールの色(暗い色) */
    6, K_TOWN, 15, K_FAIR, 19, K_TOWN, 22, K_CANAL, 32, K_TOWN, 37, K_PLAZA, 46, K_TOWN, 48, K_GATE,
    0xFF, K_TOWN,
};

/* 9: 戸 10: 窓・レール 11: 道 12: 壁(クリーム) 13: 屋根(暗い赤) 14: 壁(青灰) 15: 空 */
static const u8 town_pal[14] = {
    0x31, 2,  0x12, 1,  0x33, 3,  0x64, 6,  0x41, 1,  0x45, 5,  0x26, 4,
};

enum { T_NONE, T_WHY, T_FLOAT, T_NEAR };
static const Caption town_captions[] = {
    { "WHY RAILS HERE?", C_YELLOW, 1 },
    { "FESTIVAL FLOAT!", C_RED,    1 },
    { "VILLAGE IS NEAR", C_YELLOW, 1 },
};


static const Event town_events[] = {
    { S(0),    EV_DIR,   DIR_NONE,    0,   0 },
    { S(0),    EV_BGM,   SONG_STAGE5, 0,   0 },
    { S(2),    EV_TEXT,  T_WHY,       0,   0 },
    /* ---- 前から混成(弓兵は屋根の上) ---- */
    { S(2.5),   EV_DIR,   DIR_FRONT,   0,   0 },
    { S(2.5),   EV_SPAWN, ET_E01,      22,  10 },
    { S(3),     EV_SPAWN, ET_E01,      24, -10 },
    { S(5),    EV_DIR,   DIR_FRONT,   0,   0 },
    { S(5),    EV_SPAWN, ET_E01,      22,  10 },
    { S(5),    EV_SPAWN, ET_E03,      24, -10 },
    { S(7),    EV_SPAWN, ET_E04,      24,  22 },
    { S(7),    EV_SPAWN, ET_E05,      20,  -8 },
    { S(9),    EV_SPAWN, ET_E01,      22, -10 },
    { S(9),    EV_SPAWN, ET_E01,      24,  10 },
    { S(11),   EV_SPAWN, ET_E03,      22,  10 },
    { S(11),   EV_SPAWN, ET_MEDKIT,   18, -10 },
    { S(13),   EV_SPAWN, ET_E04,      24, -22 },
    { S(13),   EV_SPAWN, ET_E04,      26,  22 },
    /* ---- 後ろから: コウモリ人・魔法使い・追ってくる猿人 ---- */
    { S(14.5),  EV_SPAWN, ET_E03,      22,  10 },
    { S(14.5), EV_SPAWN, ET_MISBOX,   18, -10 },   /* 誘導弾の箱(山車の前。山車は後ろから来るので、振り向く前に) */
    { S(16),   EV_DIR,   DIR_BACK,    0,   0 },
    { S(16.5), EV_SPAWN, ET_E02,     -14,   6 },
    { S(16.5), EV_SPAWN, ET_E05,     -12,  -6 },
    { S(18),   EV_SPAWN, ET_E01,     -10,   8 },
    { S(19),    EV_SPAWN, ET_E05,     -12,   6 },
    { S(20),   EV_SPAWN, ET_E02,     -14,  -6 },
    { S(22),   EV_SPAWN, ET_E02,     -16,   6 },
    /* ---- 祭りの山車(後ろから。撃破までスクリプトは止まる) ---- */
    { S(23.5),  EV_SPAWN, ET_E01,     -10,  -8 },
    { S(25),   EV_DIR,   DIR_BACK,    0,   0 },
    { S(25),   EV_TEXT,  T_FLOAT,     0,   0 },
    { S(25),   EV_VOICE, VOICE_WARNING, 0, 0 },
    { S(25),   EV_SPEED, 14,          0,   0 },
    { S(26),   EV_BOSS,  ET_B03,     -20,  0 },
    { S(26.1), EV_SPEED, 0,           0,   0 },
    /* ---- 前から混成 ---- */
    { S(27),   EV_DIR,   DIR_FRONT,   0,   0 },
    { S(27),   EV_SPAWN, ET_E03,      22, -10 },
    { S(28),   EV_SPAWN, ET_E05,      20,   8 },
    { S(29),   EV_SPAWN, ET_MEDKIT,   18,  10 },
    { S(30),   EV_SPAWN, ET_E01,      22,  10 },
    { S(30),   EV_SPAWN, ET_E04,      26, -22 },
    { S(32),   EV_SPAWN, ET_E03,      22,  10 },
    { S(32),   EV_SPAWN, ET_E01,      24, -10 },
    { S(35),   EV_DIR,   DIR_NONE,    0,   0 },
    { S(35),   EV_TEXT,  T_NEAR,      0,   0 },
    { S(38),   EV_END,   0,           0,   0 },
};

const Stage stage_data = {
    town_rail, town_pal, town_events, town_captions,
    2,
    0,
    0,          /* 演出は飛ばさない(走りながら始まる) */
    SONG_STAGE5,
    town_zones, 0x3C71,
    "STAGE 5", "THE TOWN",
};
