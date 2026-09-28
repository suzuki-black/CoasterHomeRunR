/* stage_cave.c — ステージ 1: オープニング(宝を盗む)と洞窟(仕様書 5 章)
 *
 * 流れ: オープニング → 雑魚(前・後ろ) → 中ボス(大猿人の親方) → 分岐(左 = 金貨袋ばかり / 右 = 敵ばかり、約 1 分)
 *       → 合流して雑魚 → 大ボス(洞窟の主) → ステージ 2(地底湖)
 * ステージデータは定数だけで書き、読み込み先(0xB400)に合わせて単独でリンクして ROM のバンクに置く。
 * エンジンは常駐ローダーで RAM へ写してから使う(仕様書 7.3)。変数(初期値つき含む)は置けない。
 */
#include "game.h"
#include "env.h"

#define S(sec) ((u16)((sec) * 60))

/* 坑道の線形: 左右に折れ曲がり(S 字を含む)、急な上り下り(3/16 ≒ 19%)もある。
 * (長さ[m](偶数), ヨー角の変化(65536 = 1 周、正 = 右), 1m あたりの高さ変化(1/16m), 速度(m/秒 × 4))
 * 196m に分岐点: 左(金貨袋ばかり)と右(敵ばかり)。どちらも同じ長さ 228m のひし形で、424m で元の一本道に戻る */
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
static const u8 cave_pal[14] = {
    0x41, 2,  0x12, 1,  0x22, 2,  0x33, 3,  0x20, 1,  0x31, 2,  0x55, 5,
};

enum { T_NONE, T_TREASURE, T_GOTIT, T_GRRR, T_ESCAPE, T_WARNING, T_CLEAR, T_PICK, T_LROAD, T_RROAD, T_BACK };
static const Caption cave_captions[] = {
    { "TREASURE!", C_YELLOW, 1 },
    { "GOT IT!",   C_YELLOW, 2 },
    { "GRRR!",     C_RED,    2 },
    { "ESCAPE!",   C_WHITE,  2 },
    { "WARNING!",  C_RED,    2 },
    { "CAVE CLEAR!", C_YELLOW, 1 },
    { "SHOOT L OR R!", C_WHITE, 1 },
    { "TREASURE ROAD", C_YELLOW, 1 },
    { "BEAST ROAD!",   C_RED,    1 },
    { "BACK ON TRACK", C_WHITE,  1 },
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
    /* ---- 分岐: お腹に L / R と書かれた獣人(攻撃しない)。先に撃った側へ。撃たなければ右(規定) ----
     * ここから先は位置で進める(中ボス戦の長さでずれないように)。EV_ONLY_L / R = その側を通ったときだけ */
    { AT(158, EV_TEXT),                 T_PICK, 0, 0 },
    { AT(166, EV_SPAWN),                ET_SIGNL, 26, -10 },
    { AT(166, EV_SPAWN),                ET_SIGNR, 26, 10 },
    /* ---- 左: 金貨袋ばかり / 右: 敵ばかり ---- */
    { AT(198, EV_TEXT | EV_ONLY_L),     T_LROAD, 0, 0 },
    { AT(198, EV_TEXT | EV_ONLY_R),     T_RROAD, 0, 0 },
    { AT(198, EV_DIR | EV_ONLY_R),      DIR_FRONT, 0, 0 },
    { AT(200, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, 10 },
    { AT(200, EV_SPAWN | EV_ONLY_R),    ET_E01, 24, -10 },
    { AT(204, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, 10 },
    { AT(215, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, -10 },
    { AT(215, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, 10 },
    { AT(215, EV_SPAWN | EV_ONLY_R),    ET_E01, 26, 0 },
    { AT(224, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, -10 },
    { AT(232, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, 10 },
    { AT(232, EV_SPAWN | EV_ONLY_R),    ET_E01, 24, -10 },
    { AT(244, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, 10 },
    { AT(248, EV_DIR | EV_ONLY_R),      DIR_BACK, 0, 0 },
    { AT(250, EV_SPAWN | EV_ONLY_R),    ET_E02, -14, 6 },
    { AT(250, EV_SPAWN | EV_ONLY_R),    ET_E02, -16, -6 },
    { AT(262, EV_SPAWN | EV_ONLY_R),    ET_E01, -10, 8 },
    { AT(262, EV_SPAWN | EV_ONLY_R),    ET_E02, -14, -6 },
    { AT(264, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, -10 },
    { AT(280, EV_SPAWN | EV_ONLY_R),    ET_E02, -14, 6 },
    { AT(280, EV_SPAWN | EV_ONLY_R),    ET_E02, -16, -6 },
    { AT(284, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, 10 },
    { AT(298, EV_DIR | EV_ONLY_R),      DIR_FRONT, 0, 0 },
    { AT(300, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, 10 },
    { AT(300, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, -10 },
    { AT(300, EV_SPAWN | EV_ONLY_R),    ET_E01, 25, 0 },
    { AT(304, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, -10 },
    { AT(318, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, -10 },
    { AT(318, EV_SPAWN | EV_ONLY_R),    ET_E01, 24, 10 },
    { AT(324, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, 10 },
    { AT(335, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, 10 },
    { AT(335, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, -10 },
    { AT(335, EV_SPAWN | EV_ONLY_R),    ET_E01, 26, 0 },
    { AT(344, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, -10 },
    { AT(350, EV_DIR | EV_ONLY_R),      DIR_BACK, 0, 0 },
    { AT(352, EV_SPAWN | EV_ONLY_R),    ET_E02, -14, 6 },
    { AT(352, EV_SPAWN | EV_ONLY_R),    ET_E02, -16, -6 },
    { AT(364, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, 10 },
    { AT(370, EV_SPAWN | EV_ONLY_R),    ET_E01, -10, 8 },
    { AT(370, EV_SPAWN | EV_ONLY_R),    ET_E01, -12, -8 },
    { AT(384, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, -10 },
    { AT(386, EV_DIR | EV_ONLY_R),      DIR_FRONT, 0, 0 },
    { AT(388, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, 10 },
    { AT(388, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, -10 },
    { AT(388, EV_SPAWN | EV_ONLY_R),    ET_E01, 25, 0 },
    { AT(404, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, 10 },
    { AT(405, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, -10 },
    { AT(405, EV_SPAWN | EV_ONLY_R),    ET_E01, 24, 10 },
    /* ---- 合流: 元の一本道 ---- */
    { AT(426, EV_DIR),                  DIR_NONE, 0, 0 },
    { AT(426, EV_TEXT),                 T_BACK, 0, 0 },
    { AT(432, EV_DIR),                  DIR_FRONT, 0, 0 },
    { AT(432, EV_SPAWN),                ET_MEDKIT, 18, 10 },
    { AT(436, EV_SPAWN),                ET_E01, 22, -10 },
    { AT(440, EV_SPAWN),                ET_E01, 22, 10 },
    { AT(444, EV_SPAWN),                ET_E01, 25, -10 },
    { AT(448, EV_DIR),                  DIR_BACK, 0, 0 },
    { AT(452, EV_SPAWN),                ET_E02, -14, 6 },
    { AT(456, EV_SPAWN),                ET_E02, -16, -6 },
    { AT(466, EV_DIR),                  DIR_FRONT, 0, 0 },
    { AT(466, EV_SPAWN),                ET_E01, 22, 10 },
    { AT(470, EV_SPAWN),                ET_E01, 22, -10 },
    { AT(474, EV_SPAWN),                ET_E01, 24, 10 },
    { AT(478, EV_SPAWN),                ET_MEDKIT, 18, -10 },
    /* ---- 大ボス: 洞窟の主。撃破後はクリアして出口へ ---- */
    { AT(486, EV_TEXT),                 T_WARNING, 0, 0 },
    { AT(486, EV_SPEED),                6, 0, 0 },
    { AT(490, EV_BOSS),                 ET_BOSS1, 20, 0 },
    { AT(490, EV_DIR),                  DIR_NONE, 0, 0 },
    { AT(490, EV_SPEED),                16, 0, 0 },
    { AT(490, EV_TEXT),                 T_CLEAR, 0, 0 },
    { AT(560, EV_END),                  0, 0, 0 },
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
    0, 0, 0,
    { cave_left, cave_right }, cave_tail,
    1,          /* 撃たなければ右(敵ばかり) */
};
