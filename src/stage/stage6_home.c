/* stage6_home.c — ステージ 6: 村の入り口(仕様書 5 章 #7)とエンディング(5.2)
 *
 * 流れ: 村へ続く通り → 周回 A(雑魚)→ 田んぼ(敵は出さない)→ 周回 B(雑魚)→ 水車(敵は出さない)
 *       → 周回 C(重い曲の中で亜人の王(窃盗団の頭)B04 が現れ、台詞の後に激しい曲で戦う)→ 村の門の前まで走ってゆっくり止まる(HOME AT LAST!)。門の前で村人が出迎える
 * 周回区間では、敵(ボスも)が残っている間は同じコースを回り続ける(見せ場に敵を持ち込まないように)。周回区間の外は位置で(EV_AT)、中は撃破で(WAVE と EV_WAIT)進める。
 *       → エンドロール(SCREEN 1、英語の結びの文とスタッフのクレジット。scene.c)
 * 村の門はレールの終わりのリングに正面を向けて置かれる(env.c の K_HOME)。
 * エンドロールの曲もこのステージのデータで決める(文とクレジットは場面のモジュール scene.c)。
 * ステージデータは定数だけで書き、読み込み先(0xB400)に合わせて単独でリンクする(stage1_cave.c と同じ)。
 */
#include "game.h"
#include "sound.h"
#include "pcm.h"
#include "env.h"

#define S(sec) ((u16)((sec) * 60))

static const RailSeg home_rail[] = {
    {  16,       0,   0, 16 },   /*   0〜 16m */
    {  12,  0x2000,   0, 14 },
    {  24,       0,   0, 14 },   /*  28〜 52m 周回 A の手前 */
    LOOP_TURN(14),               /*  52〜164m 周回 A: 雑魚 */
    {  24,       0,   0, 14 },   /* 164〜188m 周回を抜ける */
    {  12,       0,   0, 12 },   /* 188〜200m 見せ場: 田んぼ */
    {  24,       0,   0, 14 },   /* 200〜224m 周回 B の手前 */
    LOOP_TURN(14),               /* 224〜336m 周回 B: 雑魚 */
    {  24,       0,   0, 14 },   /* 336〜360m 周回を抜ける */
    {  20,       0,   0, 14 },   /* 360〜380m 見せ場: 水車 */
    {  24,       0,   0, 14 },   /* 380〜404m 周回 C の手前 */
    LOOP_TURN(12),               /* 404〜516m 周回 C: 亜人の王 */
    {  24,       0,   0, 12 },   /* 516〜540m 周回を抜ける */
    {  36,       0,   0,  8 },   /* 〜576m。村の門は最後のリング */
    {   0,       0,   0,  0 },
};
/* 周回区間: (始まりのリング, 長さのリング数)。リング = 4m */
static const u8 home_loops[] = { 52 / 4, 112 / 4,  224 / 4, 112 / 4,  404 / 4, 112 / 4,  0 };

/* 地形: (このリングまで, 種類)。リング = 4m。村へ続く道(柵と畑と茅葺きの農家)、田んぼ(188〜200m)、水車(360〜380m)、
 * また村へ続く道、突き当たり(572m)に村の門。
 * 敵の出ない区間は約 10 秒まで: 見せ場は周回の間に 1 つずつ置く(2026-10-25 ユーザーの指摘) */
static const u8 home_zones[] = {
    10,
    47, K_HOME, 50, K_PADDY, 90, K_HOME, 95, K_MILL, 0xFF, K_HOME,
};

/* 9: 木・柱 10: 遠くの山・レール 11: 土の道 12: 畑・あぜ・苗の緑 13: 茅葺きの藁 14: 村の門の札と村人の顔(桃色) 15: 空。
 * 町と同じ色(クリームの壁と暗い赤の屋根)だと町と見分けにくかったので、田舎の色にした(2026-10-18 ユーザーの指摘) */
static const u8 home_pal[14] = {
    0x31, 2,  0x12, 1,  0x43, 3,  0x31, 5,  0x62, 5,  0x74, 5,  0x26, 4,
};

enum { T_NONE, T_SEE, T_WARNING, T_KING, T_HOME, T_CLEAR, T_FURTHER };
static const Caption home_captions[] = {
    { "ALMOST THERE!",    C_YELLOW, 1 },
    { "WARNING!",         C_RED,    2 },
    { "THE KING!",        C_RED,    2 },
    { "HOME AT LAST!",    C_YELLOW, 1 },
    { "STAGE CLEAR!",   C_YELLOW, 1 },
    { "NO FURTHER!",     C_RED,    1 },
};


#define AT(m, cmd) (m), (cmd) | EV_AT

static const Event home_events[] = {
    { S(0),    EV_DIR,   DIR_NONE,    0,   0 },
    { S(0),    EV_BGM,   SONG_STAGE6, 0,   0 },
    { S(1),    EV_TEXT,  T_SEE,       0,   0 },
    /* ---- 周回 A(52〜164m。敵が残っていれば何周でも) ---- */
    { AT(30, EV_DIR),    DIR_FRONT,   0,   0 },
    { AT(30, EV_SPAWN),  ET_E01,      22, -10 },
    { AT(34, EV_SPAWN),  ET_E01,      24,  10 },
    { AT(44, EV_SPAWN),  ET_E01,      22,  10 },
    { AT(44, EV_SPAWN),  ET_E03,      24, -10 },
    { AT(52, EV_WAIT),       15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E05,      20,   8 },
    { WAVE(EV_SPAWN),        ET_E03,      22,  10 },
    { WAVE(EV_SPAWN),        ET_E02,     -14,   6 },
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E01,     -10,  -8 },
    { WAVE(EV_SPAWN),        ET_E05,     -12,   6 },
    { WAVE(EV_SPAWN),        ET_E01,      22,  10 },
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E03,      24, -10 },
    { WAVE(EV_SPAWN),        ET_MEDKIT,   16,   0 },
    { WAVE(EV_SPAWN),        ET_E05,      20,  -8 },
    /* ---- 周回を抜けた: 田んぼ(敵は出さない) ---- */
    { AT(166, EV_DIR), DIR_NONE, 0, 0 },
    /* ---- 周回 B(224〜336m): 前後から雑魚 ---- */
    { AT(202, EV_DIR), DIR_FRONT, 0, 0 },
    { AT(202, EV_SPAWN), ET_E01, 22, -10 },
    { AT(202, EV_SPAWN), ET_E01, 24, 10 },
    { AT(212, EV_SPAWN), ET_E03, 24, -10 },
    { AT(222, EV_SPAWN), ET_E05, 20, 8 },
    { AT(224, EV_WAIT),      15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E01, 22, 10 },
    { WAVE(EV_SPAWN),        ET_E03, 22, -10 },
    { WAVE(EV_SPAWN),        ET_E02, -14, 6 },
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E01, -10, -8 },
    { WAVE(EV_SPAWN),        ET_E05, -12, 6 },
    { WAVE(EV_SPAWN),        ET_E01, 22, 10 },
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E03, 24, -10 },
    { WAVE(EV_SPAWN),        ET_MEDKIT, 16, 0 },
    { WAVE(EV_SPAWN),        ET_E05, 20, -8 },
    { WAVE(EV_SPAWN),        ET_E01, 22, -10 },
    /* ---- 周回を抜けた: 水車(敵は出さない) ---- */
    { AT(338, EV_DIR), DIR_NONE, 0, 0 },
    /* ---- 周回 C(404〜516m): 雑魚 → 亜人の王 ---- */
    { AT(382, EV_DIR), DIR_FRONT, 0, 0 },
    { AT(382, EV_SPAWN), ET_E01, 22, 10 },
    { AT(388, EV_SPAWN), ET_MISBOX, 18, -10 },
    { AT(392, EV_SPAWN), ET_E03, 24, -10 },
    { AT(404, EV_WAIT),      15, 0, 0 },
    { WAVE(EV_BGM),          SONG_KING1, 0, 0 },   /* 重くゆっくり高まる曲。ボスが現れて台詞を言うまで(game.c の talk_update) */
    { WAVE(EV_TEXT),         T_WARNING, 0, 0 },
    { WAVE(EV_VOICE),        VOICE_WARNING, 0, 0 },
    { WAVE(EV_SPEED),        4, 0, 0 },
    { WAVE(EV_WAIT),         220, 0, 0 },
    { WAVE(EV_TURN),         0, 0, 0 },
    { WAVE(EV_TALK),         VOICE_KING, SONG_KING2, T_FURTHER },
    { WAVE(EV_BOSS),         ET_B04, 16, 0 },
    { WAVE(EV_SPEED),        0, 0, 0 },
    { WAVE(EV_BGM),          SONG_STAGE6, 0, 0 },   /* 倒したら 6 面の曲へ戻す(クリアの曲は加算で鳴る) */
    { WAVE(EV_TEXT),         T_CLEAR, 0, 0 },   /* 最後のボスを倒した: 面のクリア */
    /* ---- 周回を抜けた: 村の門の前まで走ってゆっくり止まる ---- */
    { AT(518, EV_DIR), DIR_NONE, 0, 0 },
    { AT(518, EV_SPEED), 16, 0, 0 },
    { AT(561, EV_TEXT), T_HOME, 0, 0 },
    { AT(561, EV_SPEED), 2, 0, 0 },
    { AT(565, EV_END), 0, 0, 0 },
};

const Stage stage_data = {
    home_rail, home_pal, home_events, home_captions,
    2,
    0,
    0,
    SONG_STAGE6,
    home_zones, 0x4D9E,
    "STAGE 6", "THE VILLAGE",
    0, SONG_ENDING,              /* エンドロールの文とクレジットは場面のモジュール(scene.c)。曲だけここで決める */
    { 0, 0 }, 0, 0,
    0,          /* やさしい進行はしない */
    0, 0,
    home_loops,
};
