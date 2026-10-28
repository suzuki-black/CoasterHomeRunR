/* stage1_cave.c — ステージ 1「THE ESCAPE」: 遊園地からの脱出(仕様書 5 章)
 *
 * 流れ: オープニング(獣人の窃盗団が営む遊園地で村の宝を取り返し、コースターで逃げる。出入り口は囲まれているので、
 *       レールの切れ目からジャンプして柵の外の丘のレールに着地)→ 獣人が気づいて後ろから追ってくる
 *       → 柵沿いの周回 A(雑魚が前後から、中ボスの大猿人の親方)→ 宙返りの輪と屋台の道(敵は出さない。看板だけ)
 *       → 分岐(左 = 救急箱が多い / 右 = 敵ばかり)→ 合流して柵沿いの周回 B(大ボスの遊園地の用心棒)
 *       → お城と花火(敵は出さない)→ 坑道の入口へ飛び込んでステージ 2(地底湖)
 * 周回区間では、敵(ボスも)が残っている間は同じコースを回り続ける(見せ場に敵を持ち込まないように)。
 * 周回区間の中の敵は位置ではなく撃破で進める波(WAVE と EV_WAIT)で出す。位置で出すと、早く倒す人は次の敵まで待たされ、
 * 2 周目からは新しい敵が来なかった(2026-10-27 ユーザーと決めた方針)。
 * 向きの命令(EV_DIR)は、警告を消す DIR_NONE と、その後で警告を戻す最初の 1 つだけ使う(警告は残っている敵の側を指すので、
 * それ以外の向きの命令は要らない)
 * 面全体が遊園地とその周り(夕暮れ)。着地の後は、周回区間の外は位置で(EV_AT)、中は撃破で(WAVE と EV_WAIT)進める。A でオープニングを飛ばすと着地した先(skip_m)へ。
 * ステージデータは定数だけで書き、ゲームモジュールとして 0x8000 にリンクする(仕様書 7.3)。初期値つきの変数は置けない。
 */
#include "game.h"
#include "env.h"
#include "sound.h"
#include "pcm.h"

#define S(sec) ((u16)((sec) * 60))

/* 線形: 遊園地のコースター → 巻き上げ → 落下 → レールの切れ目からジャンプ → 丘に着地 → 柵沿い・丘。
 * (長さ[m](偶数), ヨー角の変化(65536 = 1 周、正 = 右), 1m あたりの高さ変化(1/16m), 速度(m/秒 × 4))
 * 周回区間(stage_loops)は、まっすぐ 24m → 右へ 1 周(16m で 90 度 × 4)→ まっすぐ 24m の 112m。前後もまっすぐ 24m 以上にする
 * (戻ったときに景色が変わらないように)。332m に分岐点: 左(救急箱ばかり)と右(敵ばかり)。どちらも同じ長さ 128m のひし形で、
 * 460m で元の一本道に戻る */
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
    {  26,       0,  0, 16 },   /* 130〜156m 柵沿い(周回 A の手前) */
    LOOP_TURN(14),              /* 156〜268m 周回 A: 雑魚と中ボス(大猿人の親方) */
    {  24,       0,  0, 16 },   /* 268〜292m 周回を抜ける */
    {  16,       0,  0, 16 },   /* 292〜308m 見せ場: 右の斜面の下の宙返りの輪(敵は出さない) */
    {  24,       0,  0, 14 },   /* 308〜332m 屋台の並ぶ道 → 分岐点(L・R の看板だけ) */
    {   0,       0,  0,  0 },
};
static const RailSeg cave_left[] = {       /* 左: 34 度左へ出て、67 度右へ戻し、34 度左で元の向き */
    {  16, -0x1800,  0, 16 },
    {  40,       0,  0, 16 },
    {  16,  0x3000,  0, 16 },
    {  40,       0,  0, 16 },
    {  16, -0x1800,  0, 16 },
    {   0,       0,  0,  0 },
};
static const RailSeg cave_right[] = {      /* 右: 左の鏡写し */
    {  16,  0x1800,  0, 16 },
    {  40,       0,  0, 16 },
    {  16, -0x3000,  0, 16 },
    {  40,       0,  0, 16 },
    {  16,  0x1800,  0, 16 },
    {   0,       0,  0,  0 },
};
static const RailSeg cave_tail[] = {       /* 460m〜 合流後 */
    {  16,  0x1800,  0, 16 },   /* 460〜476m 右へ */
    {  24,       0,  0, 16 },   /* 476〜500m 柵沿い(周回 B の手前) */
    LOOP_TURN(12),              /* 500〜612m 周回 B: 大ボス(遊園地の用心棒) */
    {  24,       0,  0, 20 },   /* 612〜636m 周回を抜ける */
    {  32,       0,  0, 20 },   /* 636〜668m 見せ場: 右の丘の上のお城と花火 */
    {  24,       0,  0, 16 },   /* 668〜692m 坑道の入口 */
    {   0,       0,  0,  0 },
};
/* 周回区間: (始まりのリング, 長さのリング数)。リング = 4m */
static const u8 cave_loops[] = { 156 / 4, 112 / 4,  500 / 4, 112 / 4,  0 };

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
    /* ---- 柵沿い(周回 A の手前): 前から猿人、柵の上から弓兵 ---- */
    { AT(110, EV_SPAWN),                ET_E01, 22, 10 },
    { AT(110, EV_SPAWN),                ET_E01, 24, -10 },
    { AT(126, EV_SPAWN),                ET_E01, 22, -10 },
    { AT(126, EV_SPAWN),                ET_E01, 22, 10 },
    { AT(140, EV_SPAWN),                ET_E04, 24, -18 },
    { AT(140, EV_SPAWN),                ET_E01, 22, 10 },
    /* ---- 周回 A(156〜268m): ここからは位置ではなく撃破で進める。波を全滅させるたびに次の波が来て、
     *      最後の中ボスとその後の波を倒したら AREA CLEAR!(倒し切るまで周回する) ---- */
    { AT(156, EV_WAIT),                 15, 0, 0 },
    { WAVE(EV_SPAWN),                   ET_E01, 22, -10 },
    { WAVE(EV_SPAWN),                   ET_E01, 25, 10 },
    { WAVE(EV_SPAWN),                   ET_E01, 27, 0 },
    { WAVE(EV_WAIT),                    15, 0, 0 },
    /* 後ろからコウモリ人と、走って追ってくる猿人 */
    { WAVE(EV_SPAWN),                   ET_E02, -14, 6 },
    { WAVE(EV_SPAWN),                   ET_E02, -16, -6 },
    { WAVE(EV_SPAWN),                   ET_E01, -11, 8 },
    { WAVE(EV_WAIT),                    15, 0, 0 },
    { WAVE(EV_SPAWN),                   ET_E02, -14, -6 },
    { WAVE(EV_SPAWN),                   ET_E02, -16, 6 },
    { WAVE(EV_SPAWN),                   ET_E01, -12, -8 },
    { WAVE(EV_WAIT),                    15, 0, 0 },
    /* 前から猿人と弓兵、救急箱 */
    { WAVE(EV_SPAWN),                   ET_E01, 22, 10 },
    { WAVE(EV_SPAWN),                   ET_E04, 24, -18 },
    { WAVE(EV_SPAWN),                   ET_MEDKIT, 18, -10 },
    { WAVE(EV_SPAWN),                   ET_E01, 24, 10 },
    { WAVE(EV_WAIT),                    20, 0, 0 },
    /* 中ボス: 大猿人の親方(撃破までスクリプトは止まる)。WARNING! の後、少し置いてから出る */
    { WAVE(EV_SPAWN),                   ET_MISBOX, 18, 10 },   /* 誘導弾の箱(中ボスの前) */
    { WAVE(EV_TEXT),                    T_WARNING, 0, 0 },
    { WAVE(EV_VOICE),                   VOICE_WARNING, 0, 0 },
    { WAVE(EV_SPEED),                   6, 0, 0 },
    { WAVE(EV_WAIT),                    50, 0, 0 },
    { WAVE(EV_BOSS),                    ET_B01, 16, 0 },
    { WAVE(EV_SPEED),                   0, 0, 0 },
    { WAVE(EV_SPAWN),                   ET_E02, -14, 6 },
    { WAVE(EV_SPAWN),                   ET_E01, -11, -8 },
    /* ---- 周回を抜けた(268m を過ぎるのは敵がいなくなってから): 宙返りの輪(敵は出さない)と屋台の道 ---- */
    { AT(270, EV_DIR),                  DIR_NONE, 0, 0 },
    /* ---- 分岐: 線路が Y 字に分かれる手前、左の線路の脇に L、右の脇に R(攻撃しない)。先に倒した側へ。倒さなければ右(規定) ---- */
    { AT(296, EV_TEXT),                 T_PICK, 0, 0 },
    { AT(302, EV_SPAWN),                ET_SIGNL, 24, -20 },
    { AT(302, EV_SPAWN),                ET_SIGNR, 24, 20 },
    { AT(306, EV_DIR),                  DIR_FRONT, 0, 0 },
    { AT(306, EV_SPAWN),                ET_E01, 22, 0 },       /* 屋台の道に猿人(周回 A を抜けてから約 10 秒で敵が来るように) */
    /* ---- 左: 救急箱が多い(敵は少なめ) / 右: 敵ばかり(332〜460m) ---- */
    { AT(334, EV_TEXT | EV_ONLY_L),     T_LROAD, 0, 0 },
    { AT(334, EV_TEXT | EV_ONLY_R),     T_RROAD, 0, 0 },
    { AT(336, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, 10 },
    { AT(336, EV_SPAWN | EV_ONLY_R),    ET_E01, 24, -10 },
    { AT(336, EV_SPAWN | EV_ONLY_L),    ET_E01, 22, 10 },
    { AT(340, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, 10 },
    { AT(344, EV_SPAWN | EV_ONLY_L),    ET_E01, 22, -10 },
    { AT(350, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, -10 },
    { AT(350, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, 10 },
    { AT(350, EV_SPAWN | EV_ONLY_R),    ET_E01, 26, 0 },
    { AT(358, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, -10 },
    { AT(358, EV_SPAWN | EV_ONLY_L),    ET_E02, -14, 6 },
    { AT(364, EV_SPAWN | EV_ONLY_R),    ET_E04, 24, -18 },
    { AT(364, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, 10 },
    { AT(376, EV_SPAWN | EV_ONLY_R),    ET_E02, -14, 6 },
    { AT(376, EV_SPAWN | EV_ONLY_R),    ET_E02, -16, -6 },
    { AT(378, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, 10 },
    { AT(382, EV_SPAWN | EV_ONLY_L),    ET_E02, -16, -6 },
    { AT(390, EV_SPAWN | EV_ONLY_R),    ET_E01, -11, 8 },
    { AT(390, EV_SPAWN | EV_ONLY_R),    ET_E02, -14, -6 },
    { AT(394, EV_SPAWN | EV_ONLY_L),    ET_E01, 22, 10 },
    { AT(394, EV_SPAWN | EV_ONLY_L),    ET_E01, 24, -10 },
    { AT(400, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, -10 },
    { AT(406, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, 10 },
    { AT(406, EV_SPAWN | EV_ONLY_R),    ET_E01, 22, -10 },
    { AT(406, EV_SPAWN | EV_ONLY_R),    ET_E01, 25, 0 },
    { AT(416, EV_SPAWN | EV_ONLY_L),    ET_E04, 24, -18 },
    { AT(418, EV_SPAWN | EV_ONLY_L),    ET_MEDKIT, 18, 10 },
    { AT(420, EV_SPAWN | EV_ONLY_R),    ET_E04, 24, -18 },
    { AT(420, EV_SPAWN | EV_ONLY_R),    ET_E01, 24, 10 },
    { AT(432, EV_SPAWN | EV_ONLY_R),    ET_E02, -14, 6 },
    { AT(432, EV_SPAWN | EV_ONLY_R),    ET_E02, -16, -6 },
    { AT(436, EV_SPAWN | EV_ONLY_L),    ET_E02, -14, 6 },
    /* ---- 合流: 柵沿い(周回 B の手前) ---- */
    { AT(462, EV_TEXT),                 T_BACK, 0, 0 },
    { AT(468, EV_SPAWN),                ET_E01, 22, 10 },
    { AT(468, EV_SPAWN),                ET_E01, 24, -10 },
    { AT(480, EV_SPAWN),                ET_E04, 24, -18 },
    { AT(480, EV_SPAWN),                ET_MEDKIT, 18, 10 },
    { AT(492, EV_SPAWN),                ET_E02, -14, 6 },
    { AT(492, EV_SPAWN),                ET_E01, -11, -8 },
    /* ---- 周回 B(500〜612m): 波 → 大ボス(遊園地の用心棒。岩を投げ、ときどき突進)。倒したら AREA CLEAR! ---- */
    { AT(500, EV_WAIT),                 15, 0, 0 },
    { WAVE(EV_SPAWN),                   ET_E01, 22, -10 },
    { WAVE(EV_SPAWN),                   ET_E01, 24, 10 },
    { WAVE(EV_SPAWN),                   ET_E04, 24, 18 },
    { WAVE(EV_WAIT),                    15, 0, 0 },
    { WAVE(EV_SPAWN),                   ET_E02, -14, 6 },
    { WAVE(EV_SPAWN),                   ET_E02, -16, -6 },
    { WAVE(EV_WAIT),                    20, 0, 0 },
    { WAVE(EV_SPAWN),                   ET_MISBOX, 18, -10 },  /* 誘導弾の箱(大ボスの前) */
    { WAVE(EV_TEXT),                    T_WARNING, 0, 0 },
    { WAVE(EV_VOICE),                   VOICE_WARNING, 0, 0 },
    { WAVE(EV_SPEED),                   6, 0, 0 },
    { WAVE(EV_WAIT),                    50, 0, 0 },
    { WAVE(EV_BOSS),                    ET_BOSS1, 20, 0 },
    { WAVE(EV_SPEED),                   0, 0, 0 },
    { WAVE(EV_TEXT),                    T_CLEAR, 0, 0 },     /* 最後のボスを倒した: 面のクリア */
    /* ---- 周回を抜けた: お城と花火を見ながら坑道の入口へ(ステージ 2) ---- */
    { AT(614, EV_DIR),                  DIR_NONE, 0, 0 },
    { AT(614, EV_SPEED),                24, 0, 0 },
    { AT(686, EV_END),                  0, 0, 0 },
};

/* 地形: (このリングまで, 種類)。リング = 4m。先頭はレールの色。
 * 遊園地の高架 → 空中 → 丘 → 柵沿い(周回 A)→ 宙返りの輪(292〜308m)→ 屋台の並ぶ道(308〜332m。分岐点のまわりは
 * 両側が開けているので分かれる線路が見える)→ 丘(分岐)→ 柵沿い(周回 B)→ お城と花火(636〜668m)→ 坑道の入口(ステージ 2 へ)。
 * 周回 A を抜けてから看板(撃って道を選ぶ)が出るまでは約 9 秒(敵の出ない区間を長くしない。2026-10-25 ユーザーの指摘) */
static const u8 cave_zones[] = {
    10,
    14, K_PARK, 20, K_AIR, 33, K_HILL, 73, K_PARKSIDE, 77, K_LOOP, 83, K_STALLS,
    119, K_HILL, 159, K_PARKSIDE, 167, K_CASTLE, 0xFF, K_EXIT,
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
    cave_loops,
};
