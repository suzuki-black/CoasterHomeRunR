/* stage1_cave.c — ステージ 1: 遊園地からの脱出(オープニング)と坑道(仕様書 5 章)
 *
 * 流れ: オープニング(獣人の窃盗団が営む遊園地で村の宝を取り返し、コースターで逃げる。出入り口は囲まれているので、
 *       レールの切れ目からジャンプして柵の外の丘のレールに着地)→ 獣人が気づいて後ろから追ってくる → 丘を下って坑道へ
 *       → 雑魚(前・後ろ) → 中ボス(大猿人の親方) → 分岐(左 = 金貨袋ばかり / 右 = 敵ばかり、約 1 分)
 *       → 合流して雑魚 → 大ボス(洞窟の主) → ステージ 2(地底湖)
 * オープニングは位置で進める(EV_AT)。A で飛ばすと着地した先(skip_m)へ進む。
 * 外(遊園地・丘)は park_pal、坑道に入ったら cave_pal へ切り替える(EV_PAL)。
 * ステージデータは定数だけで書き、ゲームモジュールとして 0x8000 にリンクする(仕様書 7.3)。初期値つきの変数は置けない。
 */
#include "game.h"
#include "env.h"
#include "sound.h"

#define S(sec) ((u16)((sec) * 60))

/* 線形: 遊園地のコースター → 巻き上げ → 落下 → レールの切れ目からジャンプ → 丘に着地して下る → 坑道。
 * 坑道は左右に折れ曲がり(S 字を含む)、急な上り下り(3/16 ≒ 19%)もある。
 * (長さ[m](偶数), ヨー角の変化(65536 = 1 周、正 = 右), 1m あたりの高さ変化(1/16m), 速度(m/秒 × 4))
 * 326m に分岐点: 左(金貨袋ばかり)と右(敵ばかり)。どちらも同じ長さ 228m のひし形で、554m で元の一本道に戻る */
static const RailSeg cave_rail[] = {
    {  20,       0,  0, 22 },   /*   0〜 46m 遊園地の中の高架 */
    {  12,  0x2000,  0, 20 },
    {   8,       0, 10, 12 },   /* 巻き上げ(遅い) */
    {   6,       0,  0, 14 },
    {  10,       0,-16, 26 },   /*  46〜 56m 落下 */
    {   6,       0,  6, 26 },   /*  56〜 78m 切れたレールから飛び出す(宙) */
    {   8,       0, -4, 26 },
    {   8,       0,-14, 24 },
    {  36,       0, -3, 20 },   /*  78〜130m 柵の外の丘に着地して下る */
    {  16,  0x1400, -2, 18 },
    {  18,       0,  0, 16 },   /* 130m〜 坑道の入口 */
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
    {  20,       0,  0, 16 },   /* 〜196m 分岐点 */
    {   0,       0,  0,  0 },
};
static const RailSeg cave_left[] = {       /* 左: 34 度左へ出て、67 度右へ戻し、34 度左で元の向き */
    {  16, -0x1800,  0, 16 },
    {  90,       0,  0, 16 },
    {  16,  0x3000,  0, 16 },
    {  90,       0,  0, 16 },
    {  16, -0x1800,  0, 16 },
    {   0,       0,  0,  0 },
};
static const RailSeg cave_right[] = {      /* 右: 左の鏡写し */
    {  16,  0x1800,  0, 16 },
    {  90,       0,  0, 16 },
    {  16, -0x3000,  0, 16 },
    {  90,       0,  0, 16 },
    {  16,  0x1800,  0, 16 },
    {   0,       0,  0,  0 },
};
static const RailSeg cave_tail[] = {       /* 424m〜 合流後 */
    {  16,  0x1800,  2, 16 },   /* 上りながら右へ */
    {  20, -0x1800,  0, 16 },   /* S 字 */
    {  30,       0, -2, 18 },   /* 下り */
    {  80,       0,  0, 12 },   /* 大ボスの広い坑道 → 出口(〜570m) */
    {   0,       0,  0,  0 },
};

/* 9: 坑木・枕木 10〜12: 石炭・岩(黒→灰。敵の茶 4 と見分けられる) 13: 天井の板・暗い土 14: 土 15: レール */
/* 外(遊園地・丘)は夕暮れ: 9 = 木・枕木、10 = 暗い骨組み・レール(紫がかった影)、11 = 土の道、12 = 暮れかけの草、
 * 13 = 赤(テント・屋根)、14 = 灯り(明るい黄)、15 = 夕焼けの空(赤紫) */
static const u8 park_pal[14] = {
    0x41, 2,  0x12, 1,  0x42, 2,  0x11, 3,  0x61, 1,  0x71, 6,  0x64, 3,
};
/* 坑道 */
static const u8 cave_pal[14] = {
    0x41, 2,  0x12, 1,  0x22, 2,  0x33, 3,  0x20, 1,  0x31, 2,  0x55, 5,
};

enum { T_NONE, T_GOTBACK, T_GATE, T_GRRR, T_HANG, T_WARNING, T_CLEAR, T_PICK, T_LROAD, T_RROAD, T_BACK, T_JUMP, T_OUT };
static const Caption cave_captions[] = {
    { "GOT IT BACK!",    C_YELLOW, 1 },
    { "GATE IS GUARDED", C_RED,    1 },
    { "GRRR!",     C_RED,    2 },
    { "HANG ON!",  C_WHITE,  2 },
    { "WARNING!",  C_RED,    2 },
    { "CAVE CLEAR!", C_YELLOW, 1 },
    { "SHOOT L OR R!", C_WHITE, 1 },
    { "TREASURE ROAD", C_YELLOW, 1 },
    { "BEAST ROAD!",   C_RED,    1 },
    { "BACK ON TRACK", C_WHITE,  1 },
    { "JUMP!",         C_YELLOW, 2 },
    { "WE ARE OUT!",   C_WHITE,  1 },
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

#define AT(m, cmd) (m), (cmd) | EV_AT

static const Event cave_events[] = {
    /* ---- オープニング: 遊園地の中でコースターに乗って、宝を取り返したところから(操作できない。A で飛ばせる) ---- */
    { S(0),    EV_CTRL,  0,           0,   0 },
    { S(0),    EV_DIR,   DIR_NONE,    0,   0 },
    { S(0),    EV_GO,    0,           0,   0 },
    { S(0.3),  EV_TEXT,  T_GOTBACK,   0,   0 },
    { AT(22, EV_TEXT),   T_GATE,      0,   0 },
    { AT(33, EV_TEXT),   T_HANG,      0,   0 },
    { AT(52, EV_TEXT),   T_JUMP,      0,   0 },
    { AT(56, EV_FX),     SE_TRAIN,    4,   0 },     /* レールを壊して飛び出す */
    { AT(79, EV_FX),     SE_TRAIN,    8,   0 },     /* 着地 */
    { AT(79, EV_TEXT),   T_OUT,       0,   0 },
    /* 獣人が気づいて後ろから追ってくる。ここから操作できる(オープニングを飛ばすとここへ) */
    { AT(86, EV_TEXT),   T_GRRR,      0,   0 },
    { AT(86, EV_SPAWN),  ET_E01,     -11,  -8 },
    { AT(86, EV_SPAWN),  ET_E01,     -12,   8 },
    { AT(86, EV_SPAWN),  ET_E01,     -14,   0 },
    { AT(86, EV_CTRL),   1,           0,   0 },
    { AT(86, EV_DIR),    DIR_BACK,    0,   0 },
    { AT(86, EV_BGM),    1,           0,   0 },
    /* ---- 雑魚: 前から猿人 ---- */
    { S(22.6),   EV_DIR,   DIR_FRONT,   0,   0 },
    { S(22.6),   EV_SPAWN, ET_E01,      22,  10 },
    { S(24.1), EV_SPAWN, ET_E01,      22, -10 },
    { S(25.6),   EV_SPAWN, ET_E01,      22,  10 },
    { S(25.6),   EV_SPAWN, ET_E01,      26, -10 },
    { S(27.6),   EV_SPAWN, ET_E01,      22, -10 },
    { S(29.1), EV_SPAWN, ET_E01,      24,  10 },
    { S(30.1), EV_SPAWN, ET_E01,      22, -10 },
    { S(31.6),   EV_SPAWN, ET_E01,      22,  10 },
    { S(31.6),   EV_SPAWN, ET_E01,      25, -10 },
    /* ---- 雑魚: 後ろからコウモリ人 ---- */
    { S(34.6),   EV_DIR,   DIR_BACK,    0,   0 },
    { AT(150, EV_PAL),   1,           0,   0 },     /* 坑道に入ったら坑道の色(時刻の並びでここ ≒ 150m) */
    { S(35.6),   EV_SPAWN, ET_E02,     -14,   6 },
    { S(37.6),   EV_SPAWN, ET_E02,     -14,  -6 },
    { S(39.6),   EV_SPAWN, ET_E02,     -16,   6 },
    { S(40.6),   EV_SPAWN, ET_E02,     -18,  -6 },
    { S(43.6),   EV_SPAWN, ET_E02,     -14,   6 },
    /* ---- 中ボス: 大猿人の親方(撃破までスクリプトは止まる) ---- */
    { S(47.6),   EV_DIR,   DIR_FRONT,   0,   0 },
    { S(47.6),   EV_TEXT,  T_WARNING,   0,   0 },
    { S(47.6),   EV_SPEED, 6,           0,   0 },
    { S(48.6),   EV_BOSS,  ET_B01,      16,  0 },
    { S(48.7), EV_SPEED, 0,           0,   0 },
    /* ---- 分岐: お腹に L / R と書かれた獣人(攻撃しない)。先に撃った側へ。撃たなければ右(規定) ----
     * ここから先は位置で進める(中ボス戦の長さでずれないように)。EV_ONLY_L / R = その側を通ったときだけ */
    { AT(288, EV_TEXT),                 T_PICK, 0, 0 },
    { AT(296, EV_SPAWN),                ET_SIGNL, 26, -10 },
    { AT(296, EV_SPAWN),                ET_SIGNR, 26, 10 },
    /* ---- 左: 金貨袋ばかり / 右: 敵ばかり ---- */
    { AT(328, EV_TEXT | EV_ONLY_L),     T_LROAD, 0, 0 },
    { AT(328, EV_TEXT | EV_ONLY_R),     T_RROAD, 0, 0 },
    { AT(328, EV_DIR | EV_ONLY_R),      DIR_FRONT, 0, 0 },
    { AT(330, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, 10 },
    { AT(330, EV_SPAWN | EV_ONLY_R),    ET_E01, 24, -10 },
    { AT(334, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, 10 },
    { AT(345, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, -10 },
    { AT(345, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, 10 },
    { AT(345, EV_SPAWN | EV_ONLY_R),    ET_E01, 26, 0 },
    { AT(354, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, -10 },
    { AT(362, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, 10 },
    { AT(362, EV_SPAWN | EV_ONLY_R),    ET_E01, 24, -10 },
    { AT(374, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, 10 },
    { AT(378, EV_DIR | EV_ONLY_R),      DIR_BACK, 0, 0 },
    { AT(380, EV_SPAWN | EV_ONLY_R),    ET_E02, -14, 6 },
    { AT(380, EV_SPAWN | EV_ONLY_R),    ET_E02, -16, -6 },
    { AT(392, EV_SPAWN | EV_ONLY_R),    ET_E01, -10, 8 },
    { AT(392, EV_SPAWN | EV_ONLY_R),    ET_E02, -14, -6 },
    { AT(394, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, -10 },
    { AT(410, EV_SPAWN | EV_ONLY_R),    ET_E02, -14, 6 },
    { AT(410, EV_SPAWN | EV_ONLY_R),    ET_E02, -16, -6 },
    { AT(414, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, 10 },
    { AT(428, EV_DIR | EV_ONLY_R),      DIR_FRONT, 0, 0 },
    { AT(430, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, 10 },
    { AT(430, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, -10 },
    { AT(430, EV_SPAWN | EV_ONLY_R),    ET_E01, 25, 0 },
    { AT(434, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, -10 },
    { AT(448, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, -10 },
    { AT(448, EV_SPAWN | EV_ONLY_R),    ET_E01, 24, 10 },
    { AT(454, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, 10 },
    { AT(465, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, 10 },
    { AT(465, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, -10 },
    { AT(465, EV_SPAWN | EV_ONLY_R),    ET_E01, 26, 0 },
    { AT(474, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, -10 },
    { AT(480, EV_DIR | EV_ONLY_R),      DIR_BACK, 0, 0 },
    { AT(482, EV_SPAWN | EV_ONLY_R),    ET_E02, -14, 6 },
    { AT(482, EV_SPAWN | EV_ONLY_R),    ET_E02, -16, -6 },
    { AT(494, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, 10 },
    { AT(500, EV_SPAWN | EV_ONLY_R),    ET_E01, -10, 8 },
    { AT(500, EV_SPAWN | EV_ONLY_R),    ET_E01, -12, -8 },
    { AT(514, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, -10 },
    { AT(516, EV_DIR | EV_ONLY_R),      DIR_FRONT, 0, 0 },
    { AT(518, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, 10 },
    { AT(518, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, -10 },
    { AT(518, EV_SPAWN | EV_ONLY_R),    ET_E01, 25, 0 },
    { AT(534, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, 10 },
    { AT(535, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, -10 },
    { AT(535, EV_SPAWN | EV_ONLY_R),    ET_E01, 24, 10 },
    /* ---- 合流: 元の一本道 ---- */
    { AT(556, EV_DIR),                  DIR_NONE, 0, 0 },
    { AT(556, EV_TEXT),                 T_BACK, 0, 0 },
    { AT(562, EV_DIR),                  DIR_FRONT, 0, 0 },
    { AT(562, EV_SPAWN),                ET_MEDKIT, 18, 10 },
    { AT(566, EV_SPAWN),                ET_E01, 22, -10 },
    { AT(570, EV_SPAWN),                ET_E01, 22, 10 },
    { AT(574, EV_SPAWN),                ET_E01, 25, -10 },
    { AT(578, EV_DIR),                  DIR_BACK, 0, 0 },
    { AT(582, EV_SPAWN),                ET_E02, -14, 6 },
    { AT(586, EV_SPAWN),                ET_E02, -16, -6 },
    { AT(596, EV_DIR),                  DIR_FRONT, 0, 0 },
    { AT(596, EV_SPAWN),                ET_E01, 22, 10 },
    { AT(600, EV_SPAWN),                ET_E01, 22, -10 },
    { AT(604, EV_SPAWN),                ET_E01, 24, 10 },
    { AT(608, EV_SPAWN),                ET_MEDKIT, 18, -10 },
    /* ---- 大ボス: 洞窟の主。撃破後はクリアして出口へ ---- */
    { AT(616, EV_TEXT),                 T_WARNING, 0, 0 },
    { AT(616, EV_SPEED),                6, 0, 0 },
    { AT(620, EV_BOSS),                 ET_BOSS1, 20, 0 },
    { AT(620, EV_DIR),                  DIR_NONE, 0, 0 },
    { AT(620, EV_SPEED),                16, 0, 0 },
    { AT(620, EV_TEXT),                 T_CLEAR, 0, 0 },
    { AT(690, EV_END),                  0, 0, 0 },
};

/* 地形: (このリングまで, 種類)。リング = 4m。先頭は外のレールの色(坑道のレールは 15 = 灰) */
static const u8 cave_zones[] = {
    10,
    14, K_PARK, 20, K_AIR, 33, K_HILL, 37, K_EXIT, 0xFF, K_CAVE,
};

const Stage stage_data = {
    cave_rail, park_pal, cave_events, cave_captions,
    10,         /* 遊園地の高架の 10m 地点から */
    0,
    S(18.5),    /* A でオープニングを飛ばす先(着地した後) */
    bgm_melody, bgm_bass,
    cave_zones, 0x5EED,
    "STAGE 1", "THE ESCAPE",
    0, 0, 0,
    { cave_left, cave_right }, cave_tail,
    1,          /* 撃たなければ右(敵ばかり) */
    1,          /* やさしい進行 */
    cave_pal,   /* 坑道に入ったら(EV_PAL 1) */
    84,         /* オープニングを飛ばしたらここ[m]へ */
};
