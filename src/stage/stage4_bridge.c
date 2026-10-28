/* stage4_bridge.c — ステージ 4: 海の鉄橋(森と町の間)
 *
 * 流れ: 森を抜けた岬のトンネルから飛び出すと、なぜか周りは海で、長い鉄橋(トラス)が続いている
 *       → 周回 A(前の海から半魚人が跳び上がってくる、後ろからも半魚人と追ってくる猿人、前から半魚人とコウモリ人)
 *       → アーチ橋(敵は出さない)→ 周回 B(半魚人・猿人・コウモリ人)→ 向こう岸の港が見えてくる(敵は出さない)
 *       → 周回 C(大ダコ人。後ろから追ってきて墨を吐き、橋の両脇の海から足を伸ばして橋をつかむ)
 *       → 沖の小島の灯台を見ながら向こう岸の港町(ステージ 5)へ
 * 鉄橋はまっすぐにしか描けないので、周回区間は曲がらない(同じまっすぐな橋を繰り返す。128m = 白波と小舟の模様の 2 回り)。
 * 周回区間では、敵(ボスも)が残っている間は同じ所を走り続ける。周回区間の外は位置で(EV_AT)、中は撃破で(WAVE と EV_WAIT)進める。
 * 半魚人・大ダコ人の動きと絵は、GAME_SEA を付けてコンパイルした game.c・models.c にだけ入っている(Makefile の game4.ihx)。
 * ステージデータは定数だけで書き、読み込み先(0xB400)に合わせて単独でリンクする(stage1_cave.c と同じ)。
 */
#include "game.h"
#include "env.h"
#include "sound.h"
#include "pcm.h"

#define S(sec) ((u16)((sec) * 60))

/* (長さ[m], ヨー角の変化(65536 = 1 周、正 = 右), 1m あたりの高さ変化(1/16m), 速度(m/秒 × 4))。鉄橋はまっすぐ */
static const RailSeg bridge_rail[] = {
    {  12,       0,   0, 16 },   /*   0〜 12m 岬のトンネル */
    {  24,       0,   0, 18 },   /*  12〜 36m 鉄橋(周回 A の手前。欄干を手前から奥まで 1 枚で描く) */
    {  64,       0,   0, 18 },   /*  36〜100m 周回 A: 雑魚 */
    {  24,       0,   0, 18 },   /* 100〜124m 周回を抜ける */
    {  28,       0,   0, 18 },   /* 124〜152m 見せ場: アーチ橋 */
    {  24,       0,   0, 18 },   /* 152〜176m 周回 B の手前 */
    {  64,       0,   0, 16 },   /* 176〜240m 周回 B: 雑魚 */
    {  24,       0,   0, 18 },   /* 240〜264m 周回を抜ける */
    {  16,       0,   0, 18 },   /* 264〜280m 見せ場: 向こう岸の港が見えてくる */
    {  24,       0,   0, 18 },   /* 280〜304m 周回 C の手前 */
    {  64,       0,   0, 14 },   /* 304〜368m 周回 C: 大ダコ人 */
    {  24,       0,   0, 18 },   /* 368〜392m 周回を抜ける(沖の小島の灯台が見えてくる) */
    {  28,       0,   0, 18 },   /* 392〜420m 向こう岸へ */
    {   0,       0,   0,  0 },
};
/* 周回区間: (始まりのリング, 長さのリング数)。リング = 4m。鉄橋はまっすぐなので周回もまっすぐな 64m の繰り返し
 * (64m = 白波と小舟の模様の 1 回り。128m だと一掃した後に走る残りが長かった。2026-10-28) */
static const u8 bridge_loops[] = { 36 / 4, 64 / 4,  176 / 4, 64 / 4,  304 / 4, 64 / 4,  0 };

/* 地形: (このリングまで, 種類)。リング = 4m。周回 A と B の間にアーチ橋(124〜152m。28m の径間 1 つ)。
 * 向こう岸の港(264m から右。見えてからは同じ大きさで残る)と、沖の小島の灯台(440m の左。最後の周回を抜けると遠くに見えてくる)は
 * env.c の sea_waves が描く。灯台は 70m 先まで見えるので、周回の継ぎ目(368m)から 70m より離した。
 * 敵の出ない区間は約 10 秒まで: 見せ場は周回の間に 1 つずつ置く(2026-10-25 ユーザーの指摘) */
static const u8 bridge_zones[] = {
    10,                          /* レールの色(暗い色) */
    3,    K_EXIT,                /* リング 0〜2(12m): 岬のトンネル */
    31,   K_BRIDGE,
    38,   K_ARCHBR,
    0xFF, K_BRIDGE,
};

/* 海の鉄橋: 9: 枕木 10: レール・暗い岩 11: 遠くの線路・岩・向こう岸 12: 海(鮮やかな青) 13: 鉄骨(赤茶) 14: 白波・地平線のもや 15: 空 */
static const u8 bridge_pal[14] = {
    0x31, 1,  0x11, 1,  0x23, 3,  0x16, 2,  0x51, 2,  0x67, 7,  0x46, 6,
};

enum { T_NONE, T_SEA, T_FISH, T_OCTO, T_LAND, T_CLEAR };
static const Caption bridge_captions[] = {
    { "THE SEA?!",      C_YELLOW, 1 },
    { "FISH MEN!",      C_RED,    1 },
    { "GIANT OCTOPUS!", C_RED,    1 },
    { "LAND AHEAD!",    C_YELLOW, 1 },
    { "STAGE CLEAR!",   C_YELLOW, 1 },
};

#define AT(m, cmd) (m), (cmd) | EV_AT

static const Event bridge_events[] = {
    { S(0),    EV_DIR,   DIR_NONE,    0,   0 },
    { S(0),    EV_BGM,   SONG_STAGE4, 0,   0 },
    { AT(13, EV_TEXT),   T_SEA,       0,   0 },   /* トンネルを出たら */
    /* ---- 周回 A(36〜100m。敵が残っていれば何周でも): 前の海から半魚人が跳び上がってくる ---- */
    { AT(30, EV_DIR),    DIR_FRONT,   0,   0 },
    { AT(30, EV_TEXT),   T_FISH,      0,   0 },
    { AT(30, EV_SPAWN),  ET_E06,      20,  26 },
    { AT(36, EV_WAIT),       15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E06,      20, -26 },
    { WAVE(EV_SPAWN),        ET_E06,      20,  26 },
    { WAVE(EV_SPAWN),        ET_E06,      22, -26 },
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_MEDKIT,   18,   6 },
    { WAVE(EV_SPAWN),        ET_E02,      22,  -8 },
    { WAVE(EV_SPAWN),        ET_E06,      20, -26 },
    { WAVE(EV_SPAWN),        ET_E06,      22,  26 },
    /* 後ろから: 半魚人と、走って追ってくる猿人 */
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E02,      22,   8 },
    { WAVE(EV_SPAWN),        ET_E06,     -10,  26 },
    { WAVE(EV_SPAWN),        ET_E01,     -10,  -8 },
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E06,     -10, -26 },
    { WAVE(EV_SPAWN),        ET_E02,     -14,   6 },
    { WAVE(EV_SPAWN),        ET_E06,     -10,  26 },
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E01,     -12,   8 },
    /* 前から: 半魚人とコウモリ人、救急箱 */
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E06,      20,  26 },
    { WAVE(EV_SPAWN),        ET_E02,      22,  -8 },
    { WAVE(EV_SPAWN),        ET_E06,      20, -26 },
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_MEDKIT,   18,  -6 },
    /* ---- 周回を抜けた: アーチ橋(敵は出さない) ---- */
    { AT(102, EV_DIR), DIR_NONE, 0, 0 },
    /* ---- 周回 B(176〜240m): 前後から半魚人、追ってくる猿人とコウモリ人 ---- */
    { AT(154, EV_DIR), DIR_FRONT, 0, 0 },
    { AT(154, EV_SPAWN), ET_E06, 20, 26 },
    { AT(160, EV_SPAWN), ET_E06, 20, -26 },
    { AT(168, EV_SPAWN), ET_E02, 22, 8 },
    { AT(176, EV_WAIT),      15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E06, 20, 26 },
    { WAVE(EV_SPAWN),        ET_E06, 22, -26 },
    { WAVE(EV_SPAWN),        ET_MEDKIT, 18, 6 },
    { WAVE(EV_SPAWN),        ET_E06, -10, 26 },
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E01, -10, -8 },
    { WAVE(EV_SPAWN),        ET_E06, -10, -26 },
    { WAVE(EV_SPAWN),        ET_E02, -14, 6 },
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E06, 20, -26 },
    { WAVE(EV_SPAWN),        ET_E02, 22, -8 },
    { WAVE(EV_SPAWN),        ET_E06, 20, 26 },
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E06, 22, -26 },
    { WAVE(EV_SPAWN),        ET_E06, -10, 26 },
    { WAVE(EV_SPAWN),        ET_E02, -14, -6 },
    { WAVE(EV_WAIT),         15, 0, 0 },
    { WAVE(EV_SPAWN),        ET_E06, 20, -26 },
    { WAVE(EV_SPAWN),        ET_E06, 22, 26 },
    /* ---- 周回を抜けた: 向こう岸の港が見えてくる(敵は出さない) ---- */
    { AT(242, EV_DIR), DIR_NONE, 0, 0 },
    /* ---- 周回 C(304〜368m): 雑魚 → 大ダコ人(後ろから。撃破までスクリプトは止まる。橋の両脇に足を伸ばしてくる)。 */
    /* 向きの声(Enemy behind!)を先に言い切ってから Warning!(ボス戦の間は向きの声を鳴らさない) ---- */
    { AT(282, EV_DIR), DIR_FRONT, 0, 0 },
    { AT(282, EV_SPAWN), ET_E06, 20, 26 },
    { AT(288, EV_SPAWN), ET_E02, 22, -8 },
    { AT(288, EV_SPAWN), ET_E06, 20, -26 },
    { AT(294, EV_SPAWN), ET_MISBOX, 18, 6 },
    { AT(298, EV_SPAWN), ET_E06, 20, 26 },
    { AT(304, EV_WAIT),      15, 0, 0 },
    { WAVE(EV_TEXT),         T_OCTO, 0, 0 },
    { WAVE(EV_VOICE),        VOICE_WARNING, 0, 0 },
    { WAVE(EV_SPEED),        14, 0, 0 },
    { WAVE(EV_WAIT),         50, 0, 0 },
    { WAVE(EV_BOSS),         ET_B07, -12, 0 },
    { WAVE(EV_TEXT),         T_CLEAR, 0, 0 },   /* 最後のボスを倒した: 面のクリア */
    /* ---- 周回を抜けた: 灯台を見ながら向こう岸へ ---- */
    { AT(370, EV_DIR), DIR_NONE, 0, 0 },
    { AT(370, EV_SPEED), 18, 0, 0 },
    { AT(374, EV_TEXT), T_LAND, 0, 0 },
    { AT(410, EV_END), 0, 0, 0 },
};

const Stage stage_data = {
    bridge_rail, bridge_pal, bridge_events, bridge_captions,
    2,
    0,
    0,          /* 演出は飛ばさない(走りながら始まる) */
    SONG_STAGE4,
    bridge_zones, 0x51E7,
    "STAGE 4", "THE BRIDGE",
    0, 0,
    { 0, 0 }, 0, 0,
    0,          /* やさしい進行はしない */
    0, 0,
    bridge_loops,
};
