/* shared.c — 常駐部に置く、ゲームの状態と環境の作業領域
 *
 * ゲーム本体(game.c)と地形(env.c)はステージごとのモジュールとしてページ2に読み込む(オーバーレイ、仕様書 7.3)。
 * 常駐部の asm(geom.s の draw_model・ring_make)や main.c、openMSX のテスト(engine.noi のシンボルを読む)が
 * 触る変数は、モジュールが入れ替わっても番地が変わらないよう、ここ(常駐部)で定義する。
 */
#include "game.h"
#include "env.h"

/* game.c */
HitRect hits[MAX_HIT];
u8  n_hits;
u16 cam_s, cam_v;
u8  facing;
u16 aim_x, aim_y;
u8  life;
u8  g_cont_left;
u8  mis_left;                    /* 誘導弾の残り(ステージをまたいで持ち越す。ゲームの始めに 3 発) */
u8  attack_dir, boss_alive;
u16 sc_tick;
u8  route;
u16 g_score;
u16 g_perf_logic[4];

/* env.c(リングの頂点・投影。geom.s の ring_make が書く) */
V3  ring_v[ENV_NR + 1][6];
i16 ring_s[ENV_NR + 1][12];
i16 ring_c[ENV_NR + 1][3];
u8  ring_front[ENV_NR + 1];
u8  ring_ok[ENV_NR + 1];
u8  g_env_zones[ENV_ZONES_MAX];
u16 g_env_seed;
u8  g_env_loops[ENV_LOOPS_MAX];
/* いまページ2に出しているマッパーのセグメント。割り込みはサウンドの後にこれを戻すので、割り込みを入れる前から
 * 正しい値(起動時のページ2 = セグメント 1)でなければならない(0 だとページ2がページ3と同じ RAM になり、壊れる) */
u8  g_seg2 = SEG_GAME;
