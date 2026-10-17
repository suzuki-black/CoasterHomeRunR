/* models.c — 敵モデル(仕様書 4 章、7.4)。単位 1/16m、y は足元から上
 *
 * 定数の表はコード領域(0x4000-0xB3FF)を空けるため、ページ0の RAM(エリア _PAGE0 = 0x2C80-)に置く。
 * Makefile で --constseg PAGE0 を付けてコンパイルし、起動時に sys_page0_data が ROM のバンク 1 から読み込む。
 */
#include "game.h"

#define MODEL(a) { a, sizeof(a) / sizeof(a[0]) }
/* 面のモジュールの置き場が足りないので、その面に出ない敵の絵は入れない(U_ の印。入れないものは { 0, 0 })。
 * ステージ 1(GAME_S1)と海の鉄橋(GAME_SEA)のモジュールで使う。Makefile の game1.ihx・game4.ihx */
static const MRect m_e01[] = {          /* 猿人(斧)・茶 */
    { -4,  0, -1,  8, C_BROWN, 2 }, {  1,  0,  4,  8, C_BROWN, 2 },
    { -5,  8,  5, 17, C_BROWN, 0 }, { -3, 17,  3, 23, C_BROWN, 1 },
    { -2, 19,  2, 20, C_BLACK, 1 },
    {  5,  8,  6, 21, C_BLACK, 0 }, {  6, 16, 10, 21, C_BLACK, 0 },
};
static const MRect m_e02a[] = {         /* コウモリ人・紫(翼上げ) */
    { -3,  0,  3,  8, C_PURPLE, 0 }, { -2,  8,  2, 12, C_PURPLE, 1 },
    { -2, 12, -1, 14, C_PURPLE, 1 }, {  1, 12,  2, 14, C_PURPLE, 1 },
    { -12, 5, -3, 10, C_BLACK, 0 },  {  3,  5, 12, 10, C_BLACK, 0 },
};
static const MRect m_e02b[] = {         /* コウモリ人・紫(翼下げ) */
    { -3,  0,  3,  8, C_PURPLE, 0 }, { -2,  8,  2, 12, C_PURPLE, 1 },
    { -2, 12, -1, 14, C_PURPLE, 1 }, {  1, 12,  2, 14, C_PURPLE, 1 },
    { -11, 1, -3,  5, C_BLACK, 0 },  {  3,  1, 11,  5, C_BLACK, 0 },
};
#if !defined(GAME_SEA)
static const MRect m_b01[] = {          /* 大猿人の親方・茶+黒 */
    { -9,  0, -3, 14, C_BROWN, 2 },  {  3,  0,  9, 14, C_BROWN, 2 },
    { -11, 14, 11, 34, C_BROWN, 0 }, { -11, 22, 11, 25, C_BLACK, 0 },
    { -6, 34,  6, 45, C_BROWN, 1 },  { -4, 39, -1, 41, C_BLACK, 1 },
    {  1, 39,  4, 41, C_BLACK, 1 },  { -5, 34,  5, 36, C_BLACK, 1 },
    { 11, 14, 13, 44, C_BLACK, 0 },  { 13, 32, 22, 44, C_BLACK, 0 },
};
#define U_b01 MODEL(m_b01)
#else
#define U_b01 { 0, 0 }
#endif
#if !defined(GAME_SEA)
static const MRect m_treasure[] = {     /* 宝箱・黄(台座つき) */
    { -7, 0, 7, 6, C_BLACK, 0 },
    { -6, 6, 6, 12, C_YELLOW, 0 }, { -6, 12, 6, 15, C_BROWN, 0 },
    { -1, 10, 1, 12, C_BLACK, 0 },
};
#define U_treasure MODEL(m_treasure)
#else
#define U_treasure { 0, 0 }
#endif
#if !defined(GAME_SEA)
static const MRect m_boss1[] = {        /* 洞窟の主(巨大な岩熊人)・茶+黒、赤い目 */
    { -12,  0, -4, 14, C_BROWN, 2 },  {  4,  0, 12, 14, C_BROWN, 2 },
    { -15, 14, 15, 38, C_BROWN, 0 },  {  -9, 18,  9, 32, C_ORANGE, 0 },
    { -21, 16, -15, 36, C_BLACK, 0 }, { 15, 16, 21, 36, C_BLACK, 0 },
    { -23, 12, -19, 17, C_WHITE, 0 }, { 19, 12, 23, 17, C_WHITE, 0 },
    {  -8, 38,  8, 50, C_BROWN, 1 },  {  -8, 50, -4, 54, C_BLACK, 1 },
    {   4, 50,  8, 54, C_BLACK, 1 },  {  -5, 44, -2, 46, C_RED, 1 },
    {   2, 44,  5, 46, C_RED, 1 },    {  -4, 39,  4, 41, C_BLACK, 1 },
};
#define U_boss1 MODEL(m_boss1)
#else
#define U_boss1 { 0, 0 }
#endif
#if !defined(GAME_SEA)
static const MRect m_rock[] = {         /* 投げた岩 */
    { -3, 0, 3, 5, C_BLACK, 0 }, { -2, 1, 2, 4, C_BROWN, 0 },
};
#define U_rock MODEL(m_rock)
#else
#define U_rock { 0, 0 }
#endif
static const MRect m_medkit[] = {       /* 救急箱(撃つと耐久が戻る = 回復)・白い箱に赤い十字、黒い取っ手 */
    { -5, 0, 5, 8, C_WHITE, 0 }, { -1, 1, 1, 7, C_RED, 0 }, { -4, 3, 4, 5, C_RED, 0 },
    { -2, 8, 2, 9, C_BLACK, 0 },
};
static const MRect m_misbox[] = {       /* 誘導弾の箱(撃つと誘導弾が 1 発増える)・水色の箱に黄色の弾、黒い取っ手 */
    { -5, 0, 5, 8, C_CYAN, 0 }, { -1, 1, 1, 7, C_YELLOW, 0 }, { -2, 6, 2, 7, C_YELLOW, 0 },
    { -2, 8, 2, 9, C_BLACK, 0 },
};
#if !defined(GAME_S1) && !defined(GAME_SEA)
static const MRect m_e03[] = {          /* キリン人(槍)・黄。首が長く、槍のリーチが長い */
    { -4,  0, -1, 12, C_YELLOW, 2 }, {  1,  0,  4, 12, C_YELLOW, 2 },
    { -5, 12,  5, 24, C_YELLOW, 0 }, { -3, 15, -1, 17, C_BROWN, 0 }, { 1, 19, 3, 21, C_BROWN, 0 },
    { -2, 24,  2, 38, C_YELLOW, 1 }, { -3, 38,  4, 43, C_YELLOW, 1 },
    {  1, 40,  3, 41, C_BLACK, 1 },  { -3, 43, -2, 46, C_BROWN, 1 }, { 2, 43, 3, 46, C_BROWN, 1 },
    {  6,  4,  7, 40, C_BLACK, 0 },  {  5, 40,  8, 45, C_WHITE, 0 },
};
#define U_e03 MODEL(m_e03)
#else
#define U_e03 { 0, 0 }
#endif
#if !defined(GAME_SEA)
static const MRect m_e04[] = {          /* 弓兵(猿人系)・橙。高台から矢を撃つ */
    { -4,  0, -1,  8, C_ORANGE, 2 }, {  1,  0,  4,  8, C_ORANGE, 2 },
    { -5,  8,  5, 17, C_ORANGE, 0 }, { -3, 17,  3, 23, C_ORANGE, 1 },
    { -2, 19,  2, 20, C_BLACK, 1 },
    { -9,  7, -7, 23, C_BROWN, 0 },  { -7,  8, -6, 22, C_WHITE, 0 },
    {  4, 14,  6, 23, C_BROWN, 0 },
};
#define U_e04 MODEL(m_e04)
#else
#define U_e04 { 0, 0 }
#endif
#if !defined(GAME_S1) && !defined(GAME_SEA)
static const MRect m_e05[] = {          /* 魔法使い(コウモリ人系)・水色。杖から魔法弾 */
    { -5,  0,  5, 12, C_CYAN, 0 },   { -4, 12,  4, 16, C_CYAN, 0 },
    { -2, 16,  2, 20, C_CYAN, 1 },   { -2, 20, -1, 22, C_CYAN, 1 }, { 1, 20, 2, 22, C_CYAN, 1 },
    { -1, 17,  1, 18, C_RED, 1 },
    { -11, 8, -5, 14, C_BLACK, 0 },  {  5,  8, 11, 14, C_BLACK, 0 },
    {  7,  0,  8, 20, C_BROWN, 0 },  {  6, 20,  9, 23, C_RED, 0 },
};
#define U_e05 MODEL(m_e05)
#else
#define U_e05 { 0, 0 }
#endif
#if !defined(GAME_SEA)
static const MRect m_arrow[] = {        /* 矢(正面から見た鏃と矢羽) */
    { -2, 0, 2, 3, C_ORANGE, 0 }, { -1, 1, 1, 2, C_WHITE, 0 },
};
#define U_arrow MODEL(m_arrow)
#else
#define U_arrow { 0, 0 }
#endif
#ifdef GAME_SEA
static const MRect m_magic[] = {        /* 海の面では大ダコ人の墨(黒い玉) */
    { -4, 0, 4, 7, C_BLACK, 0 }, { -2, 2, 2, 5, C_PURPLE, 0 },
};
#else
static const MRect m_magic[] = {        /* 魔法弾・水の玉 */
    { -4, 0, 4, 7, C_RED, 0 }, { -2, 2, 2, 5, C_YELLOW, 0 },
};
#endif
#if !defined(GAME_S1) && !defined(GAME_SEA)
static const MRect m_crate[] = {        /* 湖に浮いた木箱と、その上の救急箱 */
    { -7, 0, 7, 8, C_BROWN, 0 }, { -7, 3, 7, 4, C_BLACK, 0 },
    { -4, 8, 4, 14, C_WHITE, 0 }, { -1, 9, 1, 13, C_RED, 0 }, { -3, 10, 3, 12, C_RED, 0 },
};
#define U_crate MODEL(m_crate)
#else
#define U_crate { 0, 0 }
#endif
#if !defined(GAME_S1) && !defined(GAME_SEA)
static const MRect m_boss2[] = {        /* 湖の主(大ガエル人)・水色+黒、赤い口、黄色い目 */
    { -16,  0, -6,  8, C_CYAN, 2 },   {  6,  0, 16,  8, C_CYAN, 2 },
    { -18,  8, 18, 30, C_CYAN, 0 },   { -12, 10, 12, 24, C_WHITE, 0 },
    { -25, 14, -18, 28, C_CYAN, 0 },  { 18, 14, 25, 28, C_CYAN, 0 },
    { -16, 30, 16, 40, C_CYAN, 1 },   { -14, 30, 14, 33, C_RED, 1 },
    { -15, 40, -7, 46, C_YELLOW, 1 }, {  7, 40, 15, 46, C_YELLOW, 1 },
    { -12, 42, -10, 45, C_BLACK, 1 }, { 10, 42, 12, 45, C_BLACK, 1 },
};
#define U_boss2 MODEL(m_boss2)
#else
#define U_boss2 { 0, 0 }
#endif
#if !defined(GAME_S1) && !defined(GAME_SEA)
static const MRect m_b02[] = {          /* 森の番人(巨大コウモリ人)・紫+黒、赤い目 */
    { -6,  0,  6, 18, C_PURPLE, 0 },  { -5, 18,  5, 26, C_PURPLE, 1 },
    { -5, 26, -3, 31, C_PURPLE, 1 },  {  3, 26,  5, 31, C_PURPLE, 1 },
    { -3, 21, -1, 23, C_RED, 1 },     {  1, 21,  3, 23, C_RED, 1 },
    { -30,  6, -6, 20, C_BLACK, 0 },  {  6,  6, 30, 20, C_BLACK, 0 },
    { -38, 10, -30, 22, C_BLACK, 0 }, { 30, 10, 38, 22, C_BLACK, 0 },
    { -30, 18, -6, 19, C_PURPLE, 0 }, {  6, 18, 30, 19, C_PURPLE, 0 },
};
#define U_b02 MODEL(m_b02)
#else
#define U_b02 { 0, 0 }
#endif
#if !defined(GAME_S1) && !defined(GAME_SEA)
static const MRect m_b03[] = {          /* 祭りの山車: 車輪、木の台、赤と白の幕、提灯、屋根、上で太鼓を叩く猿人 */
    { -16,  0, -10,  6, C_BLACK, 2 }, { 10,  0, 16,  6, C_BLACK, 2 },
    { -18,  6, 18, 12, C_BROWN, 0 },  { -15, 12, 15, 26, C_RED, 0 },   { -15, 18, 15, 20, C_WHITE, 0 },
    { -19, 14, -15, 21, C_YELLOW, 0 }, { 15, 14, 19, 21, C_YELLOW, 0 },
    { -20, 26, 20, 30, C_BLACK, 0 },  { -14, 30, 14, 32, C_BLACK, 0 },
    {  -3, 32,  3, 40, C_BROWN, 1 },  { -2, 40,  2, 45, C_BROWN, 1 },  { -1, 42, 1, 43, C_BLACK, 1 },
    {   4, 33,  9, 38, C_WHITE, 0 },
};
#define U_b03 MODEL(m_b03)
#else
#define U_b03 { 0, 0 }
#endif
#if !defined(GAME_S1) && !defined(GAME_SEA)
static const MRect m_b04[] = {          /* 亜人の王(キリン人系)・黄+紫+白。王冠、槍、魔法の玉 */
    { -5,  0, -2, 14, C_YELLOW, 2 },  {  2,  0,  5, 14, C_YELLOW, 2 },
    { -8, 12,  8, 30, C_PURPLE, 0 },  { -8, 12,  8, 14, C_WHITE, 0 },
    { -11, 14, -8, 30, C_PURPLE, 0 }, {  8, 14, 11, 30, C_PURPLE, 0 },
    { -2, 30,  2, 46, C_YELLOW, 1 },  { -2, 34,  0, 36, C_BROWN, 1 }, { 0, 40, 2, 42, C_BROWN, 1 },
    { -4, 46,  5, 52, C_YELLOW, 1 },  {  2, 48,  4, 50, C_RED, 1 },
    { -4, 52,  4, 54, C_WHITE, 1 },   { -4, 54, -3, 57, C_WHITE, 1 },
    { -1, 54,  1, 58, C_WHITE, 1 },   {  3, 54,  4, 57, C_WHITE, 1 },
    { 10,  4, 11, 54, C_BLACK, 0 },   {  9, 54, 12, 60, C_WHITE, 0 },
    { -14, 26, -10, 30, C_CYAN, 0 },
};
#define U_b04 MODEL(m_b04)
#else
#define U_b04 { 0, 0 }
#endif
#if !defined(GAME_SEA)
static const MRect m_sign[] = {         /* 分岐の合図の獣人(茶の猿人、お腹が白。文字 L / R は game.c が上に描く) */
    { -5,  0, -2, 10, C_BROWN, 2 },   {  2,  0,  5, 10, C_BROWN, 2 },
    { -8, 10,  8, 26, C_BROWN, 0 },   { -6, 12,  6, 24, C_WHITE, 0 },
    { -4, 26,  4, 33, C_BROWN, 1 },   { -2, 29,  2, 30, C_BLACK, 1 },
    { -12, 20, -8, 30, C_BROWN, 0 },  {  8, 20, 12, 30, C_BROWN, 0 },   /* 両手を上げている */
};
#define U_sign MODEL(m_sign)
#else
#define U_sign { 0, 0 }
#endif
#ifdef GAME_SEA
/* ここから海の面(ステージ 4 の鉄橋)だけの敵。ほかの面のモジュールには入れない(Makefile の game4.ihx) */
static const MRect m_e06[] = {          /* 半魚人(三つ又の銛)・水色、白い腹、赤いひれ、黄色い目 */
    { -4,  0, -1,  9, C_CYAN, 2 },   {  1,  0,  4,  9, C_CYAN, 2 },
    { -5,  9,  5, 19, C_CYAN, 0 },   { -3, 10,  3, 18, C_WHITE, 0 },   { -8, 11, -5, 17, C_RED, 0 },
    { -4, 19,  4, 26, C_CYAN, 1 },   { -4, 22, -2, 24, C_YELLOW, 1 },  {  2, 22,  4, 24, C_YELLOW, 1 },
    { -3, 19,  3, 20, C_BLACK, 1 },  { -1, 26,  1, 29, C_RED, 1 },
    {  6,  2,  7, 29, C_BLACK, 0 },  {  4, 29,  9, 30, C_BLACK, 0 },
    {  4, 30,  5, 33, C_BLACK, 0 },  {  8, 30,  9, 33, C_BLACK, 0 },
};
static const MRect m_b07[] = {          /* 大ダコ人・赤。大きな頭、白い目に黒い瞳、下に広がる足と振り上げた足(白い吸盤) */
    { -24,  0, -15,  8, C_RED, 2 },  { -11,  0,  -5, 10, C_RED, 2 },  {  5,  0, 11, 10, C_RED, 2 },  { 15,  0, 24,  8, C_RED, 2 },
    { -17,  8,  17, 22, C_RED, 0 },  { -11, 14,  -4, 21, C_WHITE, 0 }, {  4, 14, 11, 21, C_WHITE, 0 },
    {  -8, 15,  -5, 19, C_BLACK, 0 }, {  5, 15,  8, 19, C_BLACK, 0 },
    { -13, 22,  13, 44, C_RED, 1 },  {  -9, 44,  9, 49, C_RED, 1 },
    { -31, 12, -25, 40, C_RED, 0 },  { 25, 12, 31, 40, C_RED, 0 },
    { -29, 22, -27, 25, C_WHITE, 0 }, { 27, 22, 29, 25, C_WHITE, 0 },
};
/* 大ダコ人の足(海から伸びて橋をつかむ)・赤、内側に白い吸盤、先はレールの側へ丸まっている。画面の右に見える足(m_tent)と左(m_tentl) */
static const MRect m_tent[] = {
    { -4,  0,  4, 34, C_RED, 0 },    { -4, 14, -2, 16, C_WHITE, 0 },  { -4, 21, -2, 23, C_WHITE, 0 },  { -4, 28, -2, 30, C_WHITE, 0 },
    { -3, 34,  3, 42, C_RED, 0 },    { -10, 40, -1, 45, C_RED, 0 },   { -13, 33, -8, 42, C_RED, 0 },   { -12, 36, -9, 38, C_WHITE, 0 },
};
static const MRect m_tentl[] = {
    { -4,  0,  4, 34, C_RED, 0 },    {  2, 14,  4, 16, C_WHITE, 0 },  {  2, 21,  4, 23, C_WHITE, 0 },  {  2, 28,  4, 30, C_WHITE, 0 },
    { -3, 34,  3, 42, C_RED, 0 },    {  1, 40, 10, 45, C_RED, 0 },    {  8, 33, 13, 42, C_RED, 0 },    {  9, 36, 12, 38, C_WHITE, 0 },
};
#endif
const Model models[] = {
    { 0, 0 }, MODEL(m_e01), MODEL(m_e02a), U_e03, U_e04, U_e05,
    U_b01, U_treasure, MODEL(m_medkit), U_boss1, U_rock,
    U_arrow, MODEL(m_magic), U_crate, U_boss2, U_b02,
    U_b03, U_b04, U_sign, U_sign, MODEL(m_misbox),
#ifdef GAME_SEA
    MODEL(m_e06), MODEL(m_b07), MODEL(m_tent),
#else
    { 0, 0 }, { 0, 0 }, { 0, 0 },
#endif
};
const Model model_e02b = MODEL(m_e02b);
#ifdef GAME_SEA
const Model model_tentl = MODEL(m_tentl);
#endif

