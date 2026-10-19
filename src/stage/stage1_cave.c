/* stage1_cave.c — ステージ 1「THE ESCAPE」: 遊園地からの脱出(仕様書 5 章)
 *
 * 流れ: オープニング(獣人の窃盗団が営む遊園地で村の宝を取り返し、コースターで逃げる。出入り口は囲まれているので、
 *       レールの切れ目からジャンプして柵の外の丘のレールに着地)→ 獣人が気づいて後ろから追ってくる
 *       → 遊園地の柵沿い・丘を走る(雑魚が前後から)→ 中ボス(大猿人の親方)→ 分岐(左 = 救急箱が多い / 右 = 敵ばかり)
 *       → 合流して柵沿いへ → 大ボス(遊園地の用心棒)→ 坑道の入口へ飛び込んでステージ 2(地底湖)
 * 面全体が遊園地とその周り(夕暮れ)。着地の後はすべて位置で進める(EV_AT)。A でオープニングを飛ばすと着地した先(skip_m)へ。
 * ステージデータは定数だけで書き、ゲームモジュールとして 0x8000 にリンクする(仕様書 7.3)。初期値つきの変数は置けない。
 */
#include "game.h"
#include "env.h"
#include "sound.h"
#include "pcm.h"

#define S(sec) ((u16)((sec) * 60))

/* 線形: 遊園地のコースター → 巻き上げ → 落下 → レールの切れ目からジャンプ → 丘に着地 → 柵沿い・丘。
 * 左右に折れ曲がり(S 字を含む)、急な上り下り(3/16 ≒ 19%)もある(上り坂でもなぜか止まらない)。
 * (長さ[m](偶数), ヨー角の変化(65536 = 1 周、正 = 右), 1m あたりの高さ変化(1/16m), 速度(m/秒 × 4))
 * 326m に分岐点: 左(救急箱ばかり)と右(敵ばかり)。どちらも同じ長さ 228m のひし形で、554m で元の一本道に戻る */
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
    {  18,       0,  0, 16 },   /* 130m〜 遊園地の柵沿い */
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
    {  80,       0,  0, 12 },   /* 大ボス(柵沿い)→ 坑道の入口(〜700m) */
    {   0,       0,  0,  0 },
};

/* 9: 坑木・枕木 10〜12: 石炭・岩(黒→灰。敵の茶 4 と見分けられる) 13: 天井の板・暗い土 14: 土 15: レール */
/* 外(遊園地・丘)は夕暮れ: 9 = 木・枕木、10 = 暗い骨組み・レール(紫がかった影)、11 = 土の道、12 = 暮れかけの草、
 * 13 = 赤(テント・屋根)、14 = 灯り(明るい黄)、15 = 夕焼けの空(赤紫) */
static const u8 park_pal[14] = {
    0x41, 2,  0x12, 1,  0x42, 2,  0x11, 3,  0x61, 1,  0x71, 6,  0x64, 3,
};

enum { T_NONE, T_GOTBACK, T_GATE, T_GRRR, T_HANG, T_WARNING, T_CLEAR, T_PICK, T_LROAD, T_RROAD, T_BACK, T_JUMP, T_OUT };
static const Caption cave_captions[] = {
    { "GOT IT BACK!",    C_YELLOW, 1 },
    { "GATE IS GUARDED", C_RED,    1 },
    { "GRRR!",     C_RED,    2 },
    { "HANG ON!",  C_WHITE,  2 },
    { "WARNING!",  C_RED,    2 },
    { "STAGE CLEAR!", C_YELLOW, 1 },
    { "SHOOT L OR R!", C_WHITE, 1 },
    { "FIRST AID ROAD", C_YELLOW, 1 },
    { "BEAST ROAD!",   C_RED,    1 },
    { "BACK ON TRACK", C_WHITE,  1 },
    { "JUMP!",         C_YELLOW, 2 },
    { "WE ARE OUT!",   C_WHITE,  1 },
};


#define AT(m, cmd) (m), (cmd) | EV_AT

static const Event cave_events[] = {
    /* ---- オープニング: 遊園地の中でコースターに乗って、宝を取り返したところから(操作できない。A で飛ばせる) ---- */
    { S(0),    EV_CTRL,  0,           0,   0 },
    { S(0),    EV_DIR,   DIR_NONE,    0,   0 },
    { S(0),    EV_GO,    0,           0,   0 },
    { S(0),    EV_BGM,   SONG_OPENING, 0,  0 },
    { S(0.3),  EV_TEXT,  T_GOTBACK,   0,   0 },
    { AT(22, EV_TEXT),   T_GATE,      0,   0 },
    { AT(33, EV_TEXT),   T_HANG,      0,   0 },
    { AT(52, EV_TEXT),   T_JUMP,      0,   0 },
    { AT(56, EV_FX),     SE_TRAIN,    4,   0 },     /* レールを壊して飛び出す */
    { AT(79, EV_FX),     SE_TRAIN,    8,   0 },     /* 着地 */
    { AT(79, EV_TEXT),   T_OUT,       0,   0 },
    /* ---- 獣人が気づいて後ろから追ってくる。ここから操作できる(オープニングを飛ばすとここへ) ---- */
    { AT(86, EV_TEXT),                  T_GRRR, 0, 0 },
    { AT(86, EV_SPAWN),                 ET_E01, -11, -8 },
    { AT(86, EV_SPAWN),                 ET_E01, -12, 8 },
    { AT(86, EV_SPAWN),                 ET_E01, -14, 0 },
    { AT(86, EV_CTRL),                  1, 0, 0 },
    { AT(86, EV_DIR),                   DIR_BACK, 0, 0 },
    { AT(86, EV_BGM),                   SONG_STAGE1, 0, 0 },
    /* ---- 柵沿い: 前から猿人、柵の上から弓兵 ---- */
    { AT(108, EV_DIR),                  DIR_FRONT, 0, 0 },
    { AT(108, EV_SPAWN),                ET_E01, 22, 10 },
    { AT(108, EV_SPAWN),                ET_E01, 24, -10 },
    { AT(124, EV_SPAWN),                ET_E01, 22, -10 },
    { AT(124, EV_SPAWN),                ET_E01, 22, 10 },
    { AT(140, EV_SPAWN),                ET_E04, 24, -18 },
    { AT(140, EV_SPAWN),                ET_E01, 22, 10 },
    { AT(154, EV_SPAWN),                ET_E01, 22, -10 },
    { AT(154, EV_SPAWN),                ET_E01, 25, 10 },
    { AT(154, EV_SPAWN),                ET_E01, 27, 0 },
    /* ---- 後ろからコウモリ人と、走って追ってくる猿人 ---- */
    { AT(164, EV_SPAWN),                ET_E01, 22, 10 },
    { AT(170, EV_DIR),                  DIR_BACK, 0, 0 },
    { AT(172, EV_SPAWN),                ET_E02, -14, 6 },
    { AT(172, EV_SPAWN),                ET_E02, -16, -6 },
    { AT(186, EV_SPAWN),                ET_E01, -11, 8 },
    { AT(186, EV_SPAWN),                ET_E02, -14, -6 },
    { AT(198, EV_SPAWN),                ET_E02, -14, 6 },
    { AT(198, EV_SPAWN),                ET_E02, -16, -6 },
    { AT(212, EV_DIR),                  DIR_FRONT, 0, 0 },
    { AT(212, EV_SPAWN),                ET_E01, 22, 10 },
    { AT(212, EV_SPAWN),                ET_E04, 24, -18 },
    { AT(222, EV_SPAWN),                ET_MEDKIT, 18, -10 },
    { AT(222, EV_SPAWN),                ET_E01, 24, 10 },
    { AT(232, EV_SPAWN),                ET_E01, 22, -10 },
    { AT(232, EV_SPAWN),                ET_E01, 24, 10 },
    { AT(236, EV_SPAWN),                ET_MISBOX, 18, 10 },   /* 誘導弾の箱(中ボスの前) */
    /* ---- 中ボス: 大猿人の親方(撃破までスクリプトは止まる) ---- */
    { AT(242, EV_TEXT),                 T_WARNING, 0, 0 },
    { AT(242, EV_VOICE),                VOICE_WARNING, 0, 0 },
    { AT(242, EV_SPEED),                6, 0, 0 },
    { AT(246, EV_BOSS),                 ET_B01, 16, 0 },
    { AT(246, EV_SPEED),                0, 0, 0 },
    { AT(246, EV_DIR),                  DIR_BACK, 0, 0 },
    { AT(256, EV_SPAWN),                ET_E02, -14, 6 },
    { AT(256, EV_SPAWN),                ET_E01, -11, -8 },
    { AT(270, EV_DIR),                  DIR_FRONT, 0, 0 },
    { AT(270, EV_SPAWN),                ET_E01, 22, 10 },
    { AT(270, EV_SPAWN),                ET_E01, 24, -10 },
    { AT(280, EV_DIR),                  DIR_BACK, 0, 0 },
    { AT(280, EV_SPAWN),                ET_E02, -14, 6 },
    { AT(280, EV_SPAWN),                ET_E02, -16, -6 },
    /* ---- 分岐: 線路が Y 字に分かれる手前、左の線路の脇に L、右の脇に R(攻撃しない)。先に倒した側へ。倒さなければ右(規定) ---- */
    { AT(288, EV_DIR),                  DIR_NONE, 0, 0 },
    { AT(288, EV_TEXT),                 T_PICK, 0, 0 },
    { AT(290, EV_DIR),                  DIR_FRONT, 0, 0 },
    { AT(290, EV_SPAWN),                ET_E01, 22, 0 },
    { AT(298, EV_SPAWN),                ET_SIGNL, 24, -20 },
    { AT(298, EV_SPAWN),                ET_SIGNR, 24, 20 },
    /* ---- 左: 救急箱が多い(敵は少なめ) / 右: 敵ばかり ---- */
    { AT(302, EV_DIR),                  DIR_FRONT, 0, 0 },
    { AT(302, EV_SPAWN),                ET_E01, 24, 2 },
    { AT(314, EV_SPAWN),                ET_E01, 20, -2 },
    { AT(324, EV_SPAWN),                ET_E01, 20, 0 },
    { AT(328, EV_TEXT | EV_ONLY_L),     T_LROAD, 0, 0 },
    { AT(328, EV_TEXT | EV_ONLY_R),     T_RROAD, 0, 0 },
    { AT(328, EV_DIR | EV_ONLY_R),      DIR_FRONT, 0, 0 },
    { AT(330, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, 10 },
    { AT(330, EV_SPAWN | EV_ONLY_R),    ET_E01, 24, -10 },
    { AT(334, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, 10 },
    { AT(338, EV_DIR | EV_ONLY_L),      DIR_BACK, 0, 0 },
    { AT(338, EV_SPAWN | EV_ONLY_L),    ET_E02, -14, -6 },
    { AT(344, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, -10 },
    { AT(344, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, 10 },
    { AT(344, EV_SPAWN | EV_ONLY_R),    ET_E01, 26, 0 },
    { AT(346, EV_DIR | EV_ONLY_L),      DIR_FRONT, 0, 0 },
    { AT(346, EV_SPAWN | EV_ONLY_L),    ET_E01, 22, -10 },
    { AT(358, EV_DIR | EV_ONLY_L),      DIR_BACK, 0, 0 },
    { AT(358, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, -10 },
    { AT(358, EV_SPAWN | EV_ONLY_L),    ET_E02, -14, 6 },
    { AT(360, EV_SPAWN | EV_ONLY_R),    ET_E04, 24, -18 },
    { AT(360, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, 10 },
    { AT(372, EV_DIR | EV_ONLY_L),      DIR_BACK, 0, 0 },
    { AT(372, EV_SPAWN | EV_ONLY_L),    ET_E02, -14, 6 },
    { AT(374, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, -10 },
    { AT(374, EV_SPAWN | EV_ONLY_R),    ET_E01, 24, 10 },
    { AT(382, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, 10 },
    { AT(386, EV_SPAWN | EV_ONLY_L),    ET_E02, -16, -6 },
    { AT(388, EV_DIR | EV_ONLY_R),      DIR_BACK, 0, 0 },
    { AT(390, EV_SPAWN | EV_ONLY_R),    ET_E02, -14, 6 },
    { AT(390, EV_SPAWN | EV_ONLY_R),    ET_E02, -16, -6 },
    { AT(398, EV_DIR | EV_ONLY_L),      DIR_FRONT, 0, 0 },
    { AT(398, EV_SPAWN | EV_ONLY_L),    ET_E01, 22, 10 },
    { AT(398, EV_SPAWN | EV_ONLY_L),    ET_E01, 24, -10 },
    { AT(404, EV_SPAWN | EV_ONLY_R),    ET_E01, -11, 8 },
    { AT(404, EV_SPAWN | EV_ONLY_R),    ET_E02, -14, -6 },
    { AT(406, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, -10 },
    { AT(414, EV_SPAWN | EV_ONLY_L),    ET_E01, 22, 10 },
    { AT(418, EV_SPAWN | EV_ONLY_R),    ET_E02, -14, 6 },
    { AT(418, EV_SPAWN | EV_ONLY_R),    ET_E02, -16, -6 },
    { AT(430, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, 10 },
    { AT(430, EV_DIR | EV_ONLY_L),      DIR_BACK, 0, 0 },
    { AT(430, EV_SPAWN | EV_ONLY_L),    ET_E02, -14, -6 },
    { AT(432, EV_DIR | EV_ONLY_R),      DIR_FRONT, 0, 0 },
    { AT(432, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, 10 },
    { AT(432, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, -10 },
    { AT(432, EV_SPAWN | EV_ONLY_R),    ET_E01, 25, 0 },
    { AT(442, EV_SPAWN | EV_ONLY_L),    ET_E02, -14, 6 },
    { AT(448, EV_SPAWN | EV_ONLY_R),    ET_E04, 24, -18 },
    { AT(448, EV_SPAWN | EV_ONLY_R),    ET_E01, 24, 10 },
    { AT(454, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, -10 },
    { AT(456, EV_DIR | EV_ONLY_L),      DIR_FRONT, 0, 0 },
    { AT(456, EV_SPAWN | EV_ONLY_L),    ET_E01, 22, -10 },
    { AT(464, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, 10 },
    { AT(464, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, -10 },
    { AT(464, EV_SPAWN | EV_ONLY_R),    ET_E01, 26, 0 },
    { AT(470, EV_SPAWN | EV_ONLY_L),    ET_E01, 22, 10 },
    { AT(478, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, 10 },
    { AT(480, EV_DIR | EV_ONLY_R),      DIR_BACK, 0, 0 },
    { AT(482, EV_SPAWN | EV_ONLY_R),    ET_E02, -14, 6 },
    { AT(482, EV_SPAWN | EV_ONLY_R),    ET_E02, -16, -6 },
    { AT(484, EV_DIR | EV_ONLY_L),      DIR_BACK, 0, 0 },
    { AT(484, EV_SPAWN | EV_ONLY_L),    ET_E02, -14, 6 },
    { AT(484, EV_SPAWN | EV_ONLY_L),    ET_E02, -16, -6 },
    { AT(496, EV_SPAWN | EV_ONLY_R),    ET_E01, -11, 8 },
    { AT(496, EV_SPAWN | EV_ONLY_R),    ET_E01, -13, -8 },
    { AT(500, EV_DIR | EV_ONLY_L),      DIR_FRONT, 0, 0 },
    { AT(500, EV_SPAWN | EV_ONLY_L),    ET_E04, 24, -18 },
    { AT(502, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, -10 },
    { AT(510, EV_DIR | EV_ONLY_R),      DIR_FRONT, 0, 0 },
    { AT(512, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, 10 },
    { AT(512, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, -10 },
    { AT(512, EV_SPAWN | EV_ONLY_R),    ET_E01, 25, 0 },
    { AT(514, EV_DIR | EV_ONLY_L),      DIR_FRONT, 0, 0 },
    { AT(514, EV_SPAWN | EV_ONLY_L),    ET_E01, 22, 10 },
    { AT(514, EV_SPAWN | EV_ONLY_L),    ET_E01, 24, -10 },
    { AT(526, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, 10 },
    { AT(528, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, -10 },
    { AT(528, EV_SPAWN | EV_ONLY_R),    ET_E04, 24, -18 },
    { AT(528, EV_DIR | EV_ONLY_L),      DIR_BACK, 0, 0 },
    { AT(528, EV_SPAWN | EV_ONLY_L),    ET_E02, -14, 6 },
    { AT(542, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, 10 },
    { AT(542, EV_SPAWN | EV_ONLY_R),    ET_E01, 24, -10 },
    /* ---- 合流: 元の一本道(柵沿い) ---- */
    { AT(542, EV_DIR | EV_ONLY_L),      DIR_FRONT, 0, 0 },
    { AT(542, EV_SPAWN | EV_ONLY_L),    ET_E01, 22, -10 },
    { AT(556, EV_DIR),                  DIR_NONE, 0, 0 },
    { AT(556, EV_TEXT),                 T_BACK, 0, 0 },
    { AT(562, EV_DIR),                  DIR_FRONT, 0, 0 },
    { AT(562, EV_SPAWN),                ET_E01, 22, 10 },
    { AT(562, EV_SPAWN),                ET_E01, 24, -10 },
    { AT(574, EV_SPAWN),                ET_E04, 24, -18 },
    { AT(574, EV_SPAWN),                ET_MEDKIT, 18, 10 },
    { AT(584, EV_DIR),                  DIR_BACK, 0, 0 },
    { AT(586, EV_SPAWN),                ET_E02, -14, 6 },
    { AT(586, EV_SPAWN),                ET_E01, -11, -8 },
    { AT(598, EV_DIR),                  DIR_FRONT, 0, 0 },
    { AT(598, EV_SPAWN),                ET_E01, 22, -10 },
    { AT(598, EV_SPAWN),                ET_E01, 24, 10 },
    { AT(604, EV_SPAWN),                ET_MISBOX, 18, -10 },  /* 誘導弾の箱(大ボスの前) */
    /* ---- 大ボス: 遊園地の用心棒(岩を投げ、ときどき突進)。撃破後はクリアして坑道の入口へ ---- */
    { AT(610, EV_TEXT),                 T_WARNING, 0, 0 },
    { AT(610, EV_VOICE),                VOICE_WARNING, 0, 0 },
    { AT(610, EV_SPEED),                6, 0, 0 },
    { AT(614, EV_BOSS),                 ET_BOSS1, 20, 0 },
    { AT(614, EV_DIR),                  DIR_NONE, 0, 0 },
    { AT(614, EV_SPEED),                24, 0, 0 },
    { AT(614, EV_TEXT),                 T_CLEAR, 0, 0 },
    { AT(674, EV_END),                  0, 0, 0 },
};

/* 地形: (このリングまで, 種類)。リング = 4m。先頭はレールの色。
 * 遊園地の高架 → 空中 → 丘 → 柵沿い(152〜180m に右の斜面の下の宙返りの輪)→ 丘(280〜316m に屋台の並ぶ道。分岐点のまわりは
 * 両側が開けているので分かれる線路が見える)→ 柵沿い(412〜444m に右の丘の上のお城と花火)→ 丘 → 柵沿い(大ボス)
 * → 坑道の入口(ステージ 2 へ) */
static const u8 cave_zones[] = {
    10,
    14, K_PARK, 20, K_AIR, 33, K_HILL, 38, K_PARKSIDE, 45, K_LOOP, 63, K_PARKSIDE, 70, K_HILL, 79, K_STALLS,
    91, K_HILL, 103, K_PARKSIDE, 111, K_CASTLE, 131, K_PARKSIDE, 155, K_HILL, 167, K_PARKSIDE, 0xFF, K_EXIT,
};

const Stage stage_data = {
    cave_rail, park_pal, cave_events, cave_captions,
    10,         /* 遊園地の高架の 10m 地点から */
    0,
    S(18.5),    /* A でオープニングを飛ばす先(着地した後) */
    SONG_STAGE1,
    cave_zones, 0x5EED,
    "STAGE 1", "THE ESCAPE",
    0, 0,
    { cave_left, cave_right }, cave_tail,
    1,          /* 撃たなければ右(敵ばかり) */
    1,          /* やさしい進行 */
    0,          /* パレットは切り替えない(面全体が遊園地のまわりの夕暮れ) */
    84,         /* オープニングを飛ばしたらここ[m]へ */
};
