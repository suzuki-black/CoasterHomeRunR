/* game.h — ゲーム進行(仕様書 2〜5 章) */
#ifndef GAME_H
#define GAME_H
#include "rail.h"

/* 敵モデル = カメラ正対の矩形の集まり(仕様書 7.4)。単位 1/16m、y は足元から上 */
typedef struct { i8 x0, y0, x1, y1; u8 color; u8 part; } MRect;   /* part: 0=胴 1=頭 2=脚 3=飾り(縁取り・当たり判定に数えない細部) */
typedef struct { const MRect *r; u8 n; } Model;
extern const Model models[];     /* 敵の種類ごと(models.c、ページ0) */
extern const Model model_e02b;   /* コウモリ人の翼を下げた絵 */
extern const Model model_tentl;  /* 画面の左に見える大ダコ人の足(先を右へ丸める。海の面だけ。models.c の GAME_SEA) */

enum { ET_NONE, ET_E01, ET_E02, ET_E03, ET_E04, ET_E05, ET_B01, ET_TREASURE, ET_MEDKIT,
       ET_BOSS1,     /* 洞窟の主(ステージ 1 の大ボス) */
       ET_ROCK,      /* 投げた岩(敵弾。撃ち落とせる) */
       ET_ARROW,     /* 矢(E04 の敵弾) */
       ET_MAGIC,     /* 魔法弾・水の玉(E05・ボスの敵弾。大きく遅い) */
       ET_CRATE,     /* 地底湖に浮いた木箱の上の救急箱 */
       ET_BOSS2,     /* 湖の主(ステージ 2 のボス。後ろから追ってくる大ガエル人) */
       ET_B02,       /* 森の番人(ステージ 3 のボス。巨大コウモリ人) */
       ET_B03,       /* 祭りの山車(町。獣人たちが引いて後ろから追ってくる。花火を撃ち、ときどきぶつかってくる) */
       ET_B04,       /* 亜人の王(自宅前のラスボス。キリン人系) */
       ET_SIGNL,     /* 分岐の合図: お腹に L と書かれた獣人(攻撃しない。撃つと左へ。L・R とも消える) */
       ET_SIGNR,     /* 同じく R(右へ) */
       ET_MISBOX,    /* 誘導弾の箱(撃つと誘導弾が 1 発増える) */
       /* ここから海の面(ステージ 4 の鉄橋)だけの敵。動きと絵は GAME_SEA を付けた game.c・models.c にだけ入る */
       ET_E06,       /* 半魚人: 海から跳び上がって橋に乗り、歩いて寄ってくる(後ろなら追いかける) */
       ET_B07,       /* 大ダコ人(ステージ 4 のボス。後ろから追ってきて墨を吐き、橋の両脇に足を伸ばしてくる) */
       ET_TENT,      /* 大ダコ人の足(後ろの海から伸びて橋をつかみ、叩いてくる) */
       ET_B08,       /* 大ナマズ人(地底湖の大ボス。前の水面に出る。絵は GAME_S2 を付けた models.c にだけ入る) */
       ET_COUNT };
#define IS_SHOT(t) ((t) == ET_ROCK || (t) == ET_ARROW || (t) == ET_MAGIC)

/* ステージスクリプト(仕様書 7.7)。時刻は 60Hz ティック */
enum {
    EV_SPAWN,   /* a = 敵、b = 距離[m]、c = 横位置(1/8m) */
    EV_DIR,     /* a = 襲撃方向(警告表示) */
    EV_GO,      /* a = 0 でコースター発進、1 で止める */
    EV_BOSS,    /* a = ボス、b = 距離[m]。撃破までスクリプトは止まる */
    EV_END,     /* ステージクリア */
    EV_TEXT,    /* a = 字幕の番号(0 = 消す)。約 2 秒で消える */
    EV_SPEED,   /* a = コースターの速度(m/秒 × 4)、0 = レールの指定に戻す */
    EV_CTRL,    /* a = 1 で操作できる、0 で操作できない(演出中) */
    EV_TURN,    /* a = 0 で前、1 で後ろへ自動で振り向く */
    EV_GRAB,    /* 宝箱を取る(宝箱がカメラへ飛んできて消える) */
    EV_BGM,     /* a = 曲の番号(songs.h の SONG_)を鳴らす、0 で止める */
    EV_FX,      /* a = 効果音、b = 揺れのフレーム数(着地など) */
    EV_PAL,     /* a = 0 でステージの palette、1 で palette2 に切り替える(外から坑道へ入ったときなど) */
    EV_VOICE,   /* a = 声(pcm.h の VOICE_)を鳴らす(ボスの前の Warning! など) */
    EV_WAIT,    /* 敵(ボスも)が全滅するまで待ち、さらに a フレーム待ってから次へ(周回区間の波。位置ではなく撃破で進める) */
};
/* 周回区間の波の命令: 時刻も位置も見ず、前の命令に続けてすぐ実行する(EV_WAIT で区切る) */
#define WAVE(cmd) 0, (cmd)
/* cmd にこの印を付けると、tick は時刻ではなくレール上の位置[m](カメラがそこを過ぎたら実行) */
#define EV_AT 0x80
/* 分岐のどちらかを通ったときだけ実行する(通らなかった側の命令は待たずに飛ばす) */
#define EV_ONLY_L 0x40
#define EV_ONLY_R 0x20
#define EV_CMD(c) ((c) & 0x1F)
enum { DIR_NONE, DIR_FRONT, DIR_BACK };
typedef struct {
    u16 tick;
    u8  cmd;
    u8  a;       /* SPAWN/BOSS: 敵 ID、DIR: 方向 */
    i8  b;       /* SPAWN/BOSS: カメラからのレール距離[m](負 = 後方) */
    i8  c;       /* SPAWN: 横位置(1/8m、正 = 右) */
} Event;

typedef struct { const char *text; u8 color; u8 scale; } Caption;

typedef struct {
    const RailSeg *track;
    const u8      *palette;   /* 9-15 の 7 色(RB, G)× 7 */
    const Event   *events;
    const Caption *captions;  /* 字幕(番号 1 から) */
    u8  start_m;              /* 開始位置[m] */
    u8  start_back;           /* 1 = 後ろを向いて始まる(オープニング) */
    u16 skip_tick;            /* 演出中に A を押したらこの時刻まで飛ばす(0 = 飛ばせない) */
    u8  bgm;                  /* BGM の曲の番号(songs.h の SONG_。src/sound/songs.mml) */
    const u8 *zones;          /* 地形の区間(env.h の env_build) */
    u16 seed;                 /* 地形の乱数の種 */
    const char *name;         /* ステージの始めに出す名前(2 行目は title2) */
    const char *title2;
    /* 最後のステージだけ: エンディング(SCREEN 1、BIOS のフォント)の文と BGM */
    const u8 *ending;         /* (使っていない。エンドロールの文は scene.c。2026-10-25 に英語のエンドロールにした) */
    u8  end_bgm;              /* エンディングの曲の番号(0 = 無し) */
    /* 分岐(無ければ 0): track は分岐点まで、branch[0/1] = 左 / 右(同じ長さで同じ合流点に戻る形にする)、tail = 合流後。
     * 分岐点より前で L・R の獣人のどちらかを撃つとそちらへ。撃たなければ branch_def の側 */
    const RailSeg *branch[2];
    const RailSeg *tail;
    u8  branch_def;           /* 0 = 左、1 = 右 */
    /* 1 = やさしい進行: 片側(前 / 後ろ)に敵が残っている間は、反対側から来る敵と警告を待たせる(スクリプトの時刻も止まる) */
    u8  gentle;
    const u8 *palette2;       /* 途中で切り替える 2 つ目のパレット(EV_PAL。無ければ 0) */
    u8  skip_m;               /* 演出を A で飛ばしたとき、カメラをこの位置[m]まで進める(0 = 進めない) */
    /* 周回区間(無ければ 0): (始まりのリング, 長さのリング数) の組を並べ、0 で終わる。区間の終わりに来たとき敵(ボスも)が
     * 残っていれば、始まりへ戻ってもう一周する(敵は復活しない。残っている敵も一緒に戻る)。レールは区間の曲がりの合計を
     * 360 度にし、始まりと終わりの前後 24m(LOOP_K 本のリング)をまっすぐで平らにする(地形モジュールが同じ景色に写す) */
    const u8 *loops;
} Stage;
/* 周回区間のレール(112m): まっすぐ 24m → 右へ 1 周(16m で 90 度 × 4)→ まっすぐ 24m。手前と後ろもまっすぐ 24m 以上にする。
 * v = 速度(m/秒 × 4) */
#define LOOP_TURN(v) \
    {  24,       0,  0, v }, \
    {  16,  0x4000,  0, v }, \
    {  16,  0x4000,  0, v }, \
    {  16,  0x4000,  0, v }, \
    {  16,  0x4000,  0, v }, \
    {  24,       0,  0, v }

/* モジュール(仕様書 7.3 のオーバーレイ)。ROM の 2 バンク(16KB)ずつ
 *   地形モジュール: ステージごと(その面の地形の処理だけ)。ステージ 1 はバンク 4〜5、ステージ n(2〜)はバンク 14 + 2n〜
 *     (18〜27)→ マッパーのセグメント 5。ステージの始めに読み込む
 *   ゲームモジュール: ステージ n(1〜)はバンク 4 + 2n〜 → ページ2(セグメント 1)。ステージの始めに読み込む。
 *     0x8000 = jp game_run、0x8003 = そのステージの Stage へのポインタ */
#define ENV_BANK(n)  ((n) == 1 ? 4 : 14 + 2 * (n))
#define GAME_BANK(n) (4 + 2 * (n))
#define STAGE_COUNT  6
#define SEG_GAME     1
#define SEG_ENV      5
#define SEG_SND      8   /* 音のモジュール(FM の曲。起動時に 1 回読み込む) */
#define SND_BANK     28  /* 音のモジュールの ROM のバンク(28-29。6〜17 はゲームモジュール、18〜27 はステージ 2〜6 の地形モジュール) */
#define SEG_VOICE    9   /* 声のモジュール(内蔵 PCM の声。起動時に 1 回読み込む) */
#define SEG_SCENE   10   /* 場面のモジュール(エンディング・ゲームオーバー・隠しコンフィグ・SCREEN 3 の作り直し。起動時に 1 回) */
#define SCENE_BANK  40   /* 場面のモジュールの ROM のバンク(40-41) */
#define VOICE_BANK   30  /* 声のモジュールの ROM のバンク(30-31) */
#define MOD_STAGE    (*(const Stage * const *)0x8003)
typedef u8 (*GameRunFn)(const Stage *st, u8 first);
#define MOD_GAME_RUN ((GameRunFn)0x8000)

/* ステージを 1 つ遊ぶ(ゲームモジュールの中)。first = 1 ならライフとスコアを初期化する。
 * 戻り値 0 = ゲームオーバー、1 = クリア */
u8 game_run(const Stage *st, u8 first);
extern u16 g_score;          /* スコア / 10 */
extern u8  life;
extern u8  mis_left;
extern u8  g_stage;

/* ゲームの状態のうち、常駐部(描画・テスト・メイン)からも見るもの。実体は shared.c(常駐) */
typedef struct { u8 x0, y0, x1, y1; i16 z; u8 id; } HitRect;   /* 当たり判定: 直前に描いた矩形(ブロック座標) */
#define MAX_HIT 48
extern HitRect hits[MAX_HIT];     /* geom.s の draw_model が足す */
extern u8  n_hits;
extern u16 cam_s, cam_v;          /* カメラのレール位置(1/64m)と速さ */
extern u8  facing;                /* 0 前 / 1 後ろ */
extern u16 aim_x, aim_y;          /* 照準(ピクセル)。テストの自動操作が書き換える */
extern u8  attack_dir, boss_alive;
/* 周回区間(rail.c)。g_loop_p = 次の周回区間(g_env_loops の中)、g_lap_fast = 0 以外なら一掃した後で、周回を抜けた後の
 * まっすぐな区間まで速く走る(1 = 一掃したところ。モジュールが AREA CLEAR! を出して 2 にする) */
extern const u8 *g_loop_p;
extern u8  g_lap_fast;
extern u16 g_fast_end;                  /* 速く走るのをやめる位置(0 = 無し) */
u16 loop_update(i8 fs, const Event *next);   /* next = 次に実行する台本の命令(台本が終わっていれば 0) */
extern const Caption g_area_cap;        /* 「AREA CLEAR!」(どの面でも同じなので常駐部に置く) */
extern u16 sc_tick;               /* スクリプトの時刻 */
extern u8  route;                 /* 分岐で選ばれている側(0 左 / 1 右) */
extern u16 g_perf_logic[4];

/* 隠しコンフィグ(menu.c。タイトルでコナミコマンド)。並びは menu.c の項目と同じ */
typedef struct {
    u8 damage;      /* 1 回の被弾で減る耐久(1〜8) */
    u8 god;         /* 1 = 被弾しても耐久が減らない */
    u8 stage;       /* 始めるステージ(1〜) */
    u8 life;        /* 始めの耐久(1〜8) */
    u8 oneshot;     /* 1 = どの敵も 1 発で倒せる */
    u8 route;       /* 分岐: 0 = 撃って選ぶ、1 = 左に固定、2 = 右に固定 */
    u8 easy;        /* やさしい進行: 0 = ステージの指定どおり、1 = 常に、2 = しない */
    u8 fps;         /* 1 = 左上に fps とティック数 */
    u8 xfer;        /* 転送: 0 = AUTO(ファームウェアに CHGCPU があれば Z80 で送る)、1 = 常に R800 */
} Config;
extern Config g_cfg;
void cfg_defaults(void);
void title(void);

/* 表示(main.c) */
extern u8 g_spr_attr[17];
extern const u8 pal_fixed[];  /* 0-8 */
void present(void);
void set_stage_palette(const u8 *pal);
void flash_palette(void);

/* 計測用(openMSX のテストが読む) */
extern u8  g_perf_ticks;     /* 直前のフレームにかかったティック数 */
extern u16 g_perf_render;
extern u16 g_perf_xfer;      /* vdp_xfer の時間 */
extern u16 g_perf_env;       /* env_draw までの時間 */    /* 直前のフレームの描画時間(タイマ値 ≒ 3.9µs) */
extern u8  g_scene;          /* 0=タイトル 1=ゲーム 2=ゲームオーバー 3=クリア */

#endif
