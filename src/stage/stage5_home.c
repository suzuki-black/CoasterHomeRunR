/* stage5_home.c — ステージ 5: 村の入り口(仕様書 5 章 #7)とエンディング(5.2)
 *
 * 流れ: 村へ続く通り → 雑魚 → 亜人の王(窃盗団の頭)B04(撃破までスクリプトは止まる)
 *       → 村の門の前まで走ってゆっくり止まる(HOME AT LAST!)。門の前で村人が出迎える
 *       → エンディング(SCREEN 1、ひらがな。取り戻した宝の量で村の暮らしが 3 通り)
 * 村の門はレールの終わりのリングに正面を向けて置かれる(env.c の K_HOME)。
 * エンディングの文と BGM もこのステージのデータに置く(最後のステージのデータはエンディングの間も読み込んだまま)。
 * ステージデータは定数だけで書き、読み込み先(0xB400)に合わせて単独でリンクする(stage1_cave.c と同じ)。
 */
#include "game.h"
#include "env.h"

#define S(sec) ((u16)((sec) * 60))

static const RailSeg home_rail[] = {
    {  16,       0,   0, 16 },
    {  12,  0x2000,   0, 14 },
    {  24,       0,   0, 12 },
    {  70,       0,   0,  8 },   /* 〜122m。村の門は 120m のリング */
    {   0,       0,   0,  0 },
};

static const u8 home_zones[] = {
    10,
    0xFF, K_HOME,                /* 全部が村へ続く通り(家はクリームの壁だけ。突き当たりに村の門) */
};

/* 町と同じ。14 = 村の門の札と村人の顔(桃色。ほかの家は 12 のクリームだけ) */
static const u8 home_pal[14] = {
    0x31, 2,  0x12, 1,  0x33, 3,  0x64, 6,  0x41, 1,  0x74, 5,  0x26, 4,
};

enum { T_NONE, T_SEE, T_WARNING, T_KING, T_HOME };
static const Caption home_captions[] = {
    { "ALMOST THERE!",    C_YELLOW, 1 },
    { "WARNING!",         C_RED,    2 },
    { "THE KING!",        C_RED,    2 },
    { "HOME AT LAST!",    C_YELLOW, 1 },
};

/* BGM「王」: 160BPM のイ短調(Am → F → Dm → E)。8 分音符 = 11 ティック。最後の戦い */
#define E8 11
#define Q  22
static const u8 bgm_melody[] = {
    69,E8, 69,E8, 72,E8, 69,E8, 76,Q,  75,Q,             /* Am */
    77,E8, 76,E8, 74,E8, 72,E8, 74,Q,  71,Q,             /* F  */
    74,E8, 74,E8, 77,E8, 74,E8, 81,Q,  80,Q,             /* Dm */
    76,Q,  68,Q,  71,Q,  74,Q,                           /* E  */
    0xFF, 0,
};
static const u8 bgm_bass[] = {
    45,E8, 57,E8, 45,E8, 57,E8, 45,E8, 57,E8, 45,E8, 57,E8,
    41,E8, 53,E8, 41,E8, 53,E8, 41,E8, 53,E8, 41,E8, 53,E8,
    38,E8, 50,E8, 38,E8, 50,E8, 38,E8, 50,E8, 38,E8, 50,E8,
    40,E8, 52,E8, 40,E8, 52,E8, 40,E8, 52,E8, 44,E8, 47,E8,
    0xFF, 0,
};
#undef E8
#undef Q

/* エンディングの BGM「おしまい」: 100BPM のヘ長調(F → B♭ → C → F)。のどか */
#define E8 18
#define Q  36
#define H  72
static const u8 end_melody[] = {
    72,Q,  69,E8, 72,E8, 77,H,                           /* F  */
    74,Q,  77,E8, 74,E8, 70,H,                           /* B♭ */
    72,Q,  74,E8, 76,E8, 79,Q,  76,Q,                    /* C  */
    77,H,  72,Q,  0,Q,                                   /* F  */
    0xFF, 0,
};
static const u8 end_bass[] = {
    41,Q, 48,Q, 45,Q, 48,Q,
    46,Q, 53,Q, 50,Q, 53,Q,
    48,Q, 55,Q, 52,Q, 55,Q,
    41,Q, 48,Q, 41,H,
    0xFF, 0,
};

/* エンディングの文(仕様書 5.2)。(行, 桁, MSX の文字コード…, 0)、0xFF で終わり。
 * 行の bit7 が立った行は「持ち帰った宝の量(残りライフ)」で選ぶ: bit5-6 = 0 多い(6 以上)/ 1 そこそこ(3〜5)/ 2 少ない(1〜2)。
 * ひらがな: あ〜そ = 91h〜9Fh、た〜ん = E0h〜FDh、を = 86h、小さい ゃゅょっ = 8Ch〜8Fh、゛= DEh、。= A1h */
static const u8 ending_text[] = {
    /* わかものは むらの たからものを */
    0x06, 8, 0xFC, 0x96, 0xF3, 0xE9, 0xEA, 0x20, 0xF1, 0xF7, 0xE9, 0x20, 0xE0, 0x96, 0xF7, 0xF3, 0xE9, 0x86, 0,
    /* とりもどしました。 */
    0x08, 11, 0xE4, 0xF8, 0xF3, 0xE4, 0xDE, 0x9C, 0xEF, 0x9C, 0xE0, 0xA1, 0,
    /* むらは まえより ゆたかに なりましたとさ。(宝が多い) */
    0x8A, 5, 0xF1, 0xF7, 0xEA, 0x20, 0xEF, 0x94, 0xF6, 0xF8, 0x20, 0xF5, 0xE0, 0x96, 0xE6, 0x20, 0xE5, 0xF8, 0xEF, 0x9C, 0xE0, 0xE4, 0x9B, 0xA1, 0,
    /* むらは もとの くらしに もどりましたとさ。(宝がそこそこ) */
    0xAA, 4, 0xF1, 0xF7, 0xEA, 0x20, 0xF3, 0xE4, 0xE9, 0x20, 0x98, 0xF7, 0x9C, 0xE6, 0x20, 0xF3, 0xE4, 0xDE, 0xF8, 0xEF, 0x9C, 0xE0, 0xE4, 0x9B, 0xA1, 0,
    /* むらびとは ちょっぴり がっかりしましたとさ。(宝が少ない) */
    0xCA, 3, 0xF1, 0xF7, 0xEB, 0xDE, 0xE4, 0xEA, 0x20, 0xE1, 0x8E, 0x8F, 0xEB, 0xDF, 0xF8, 0x20, 0x96, 0xDE, 0x8F, 0x96, 0xF8, 0x9C, 0xEF, 0x9C, 0xE0, 0xE4, 0x9B, 0xA1, 0,
    /* おしまい */
    0x0E, 14, 0x95, 0x9C, 0xEF, 0x92, 0,
    0xFF,
};

#define AT(m, cmd) (m), (cmd) | EV_AT

static const Event home_events[] = {
    { S(0),    EV_DIR,   DIR_NONE,    0,   0 },
    { S(0),    EV_BGM,   1,           0,   0 },
    { S(1),    EV_TEXT,  T_SEE,       0,   0 },
    { S(4),    EV_DIR,   DIR_FRONT,   0,   0 },
    { S(4),    EV_SPAWN, ET_E01,      22,  10 },
    { S(4),    EV_SPAWN, ET_E03,      24, -10 },
    { S(6),    EV_SPAWN, ET_E05,      20,   8 },
    { S(7),    EV_SPAWN, ET_MEDKIT,   16,   0 },
    /* ---- 亜人の王 ---- */
    { S(9),    EV_TEXT,  T_WARNING,   0,   0 },
    { S(9),    EV_SPEED, 4,           0,   0 },
    { S(10),   EV_BOSS,  ET_B04,      16,  0 },
    /* ---- 村の門の前まで走ってゆっくり止まる ---- */
    { S(10.1), EV_DIR,   DIR_NONE,    0,   0 },
    { S(10.1), EV_SPEED, 16,          0,   0 },
    { AT(107, EV_TEXT),  T_HOME,      0,   0 },
    { AT(107, EV_SPEED), 2,           0,   0 },
    { AT(111, EV_END),   0,           0,   0 },
};

const Stage stage_data = {
    home_rail, home_pal, home_events, home_captions,
    2,
    0,
    0,
    bgm_melody, bgm_bass,
    home_zones, 0x4D9E,
    "STAGE 5", "THE VILLAGE",
    ending_text, end_melody, end_bass,
};
