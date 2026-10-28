/* rail.c — レール表の生成と補間
 *
 * 点は 2m ごと(RAIL_STEP)。区間の列を続けて足していける(rail_add)。分岐は、分岐点までを作ったところの
 * 状態を覚えておき(rail_fork)、選ばれた側の区間と合流後の区間を分岐点から作り直す(rail_refork)。
 */
#include "rail.h"
#include "game.h"
#include "env.h"

u16 rail_len;                    /* 長さ[m](点の数 × 2) */
/* 次に置く点の番号と、そのときの向き・位置(geom.s。rail_add がそこで使う) */
extern u16 rb_n, rb_yaw;
extern i16 rb_x, rb_y, rb_z;
static u16 fk_n, fk_yaw;         /* 分岐点の状態 */
static i16 fk_x, fk_y, fk_z;

void rail_begin(void)
{
    rb_n = 0; rb_yaw = 0;
    rb_x = rb_y = rb_z = 0;
    rail_len = 0;
}

/* rail_add は geom.s(アセンブラ。C だと分岐を選んだフレームで約 190 点を作り直すのに 13ms かかっていた) */

void rail_fork(void)
{
    fk_n = rb_n; fk_yaw = rb_yaw;
    fk_x = rb_x; fk_y = rb_y; fk_z = rb_z;
}

u16 rail_fork_m(void)
{
    return fk_n << 1;
}

/* 分岐点から branch をたどった点を out へ最大 max 個(2m ごと)。選ばれていない側の線路を描くのに使う */
u8 rail_sample(const RailSeg *segs, RailPt *out, u8 max)
{
    u8 i, steps, n = 0;
    u16 yaw = fk_yaw;
    i16 x = fk_x, y = fk_y, z = fk_z;
    for (; segs->len && n < max; segs++) {
        i16 dyaw;
        steps = segs->len >> 1;
        dyaw = segs->dyaw / steps;
        for (i = 0; i < steps && n < max; i++) {
            out[n].x = x; out[n].y = y; out[n].z = z; out[n].yaw = yaw;
            n++;
            yaw += dyaw;
            x += SIN(yaw >> 8) << 1;
            z += COS(yaw >> 8) << 1;
            y += (i16)segs->dy << 5;
        }
    }
    return n;
}

void rail_refork(const RailSeg *branch, const RailSeg *tail)
{
    rb_n = fk_n; rb_yaw = fk_yaw;
    rb_x = fk_x; rb_y = fk_y; rb_z = fk_z;
    rail_add(branch);
    rail_add(tail);
}

u16 rail_s_max(void)
{
    return (u16)((rail_len - RAIL_STEP) << 6) - 1;
}

/* rail_at は geom.s(アセンブラ。補間の割合との掛け算をその場で行う) */

void rail_cam(u16 s, i16 lat, i16 up, V3 *o)
{
    V3 p;
    u16 yaw;
    u8 h;
    rail_at(s, &p, &yaw);
    h = yaw >> 8;
    if (lat) {
        p.x += fmul(lat, COS(h));
        p.z -= fmul(lat, SIN(h));
    }
    xform(p.x, p.y + up, p.z, o);
}

/* ---------------------------------------------------------------------------
 * 周回区間(Stage.loops)。区間の終わりに来たとき敵(ボスも)が残っていれば、始まりへ戻ってもう一周する
 * (地形モジュールが、終わりの前後のリングを始まりの前後と同じにしてある)。常駐部に置く(ゲームモジュールの空きが少ない)
 * --------------------------------------------------------------------------- */
const u8 *g_loop_p;
u8 g_lap_fast;
u16 g_fast_end;                  /* 速く走るのをやめる位置(周回を抜けてから、抜けた後のまっすぐな 24m の終わり) */
const Caption g_area_cap = { "AREA CLEAR!", C_YELLOW, 1 };

/* ゲームモジュールの trolley_update が、カメラを進めた後に毎フレーム呼ぶ。fs = 残っている敵の向き(foe_side)、
 * next = 次に実行する台本の命令(台本が終わっていれば 0)。
 * 戻り値: カメラを動かした距離(敵も同じだけ動かす。0 = 動かしていない)。一掃したら g_lap_fast を 1 にする
 * (モジュールが AREA CLEAR! を出して 2 にする。最後の周回なら字幕は出さずに 2 にする) */
u16 loop_update(i8 fs, const Event *next)
{
    const u8 *lp = g_loop_p;
    u16 b;
    u8 done;
    if (g_fast_end && cam_s >= g_fast_end) { g_lap_fast = 0; g_fast_end = 0; }
    if (!lp[0]) return 0;
    b = (u16)(lp[0] + lp[1]) << 8;
    /* 区間の命令を全部実行したか: 次の命令が区間の終わりより先の位置の命令(か台本の終わり)。周回区間の波(WAVE)が残っていれば
     * まだ(たまたま敵がいなくても、次の波を出すまで周回を続ける) */
    done = !next || ((next->cmd & EV_AT) && next->tick >= (b >> 6));
    if (cam_s >= b) {
        if (boss_alive || fs || !done) {
            u16 d = (u16)lp[1] << 8;
            cam_s -= d;
            /* 警告は残っている敵の側へ(台本の向きのままだと、すぐ後ろに張り付いた敵に気づけず回り続けた。2026-10-25) */
            if (fs) attack_dir = (fs < 0) ? DIR_BACK : DIR_FRONT;
            return d;
        }
        /* 一掃して抜けた: 抜けた後のまっすぐな 24m(継ぎ目のために見せ場を置けない所)も速く走る */
        g_loop_p = lp + 2;
        if (!g_lap_fast) g_lap_fast = lp[2] ? 1 : 2;
        g_fast_end = b + 24 * 64;
    } else if (!g_lap_fast && cam_s >= (u16)lp[0] << 8 && !boss_alive && !fs && done) {
        /* 区間の命令を全部実行し、敵もいない: 一掃した。周回の残りは速く走って抜ける
         * (全滅させても 1 周の残りを何もないまま走っていた。2026-10-25 ユーザーの指摘) */
        /* 最後の周回(ボス)では AREA CLEAR! を出さない(台本がボスを倒した後に面のクリアの字幕を出す。クリアの表示が
         * AREA CLEAR!・STAGE CLEAR!・集計と 3 回続いていた。2026-10-28 ユーザーと決めた) */
        g_lap_fast = lp[2] ? 1 : 2;
        if (cam_s < ((u16)lp[0] << 8) + 24 * 64) {
            /* 始まりのまっすぐな 24m にいれば、終わりの先の同じ所(景色は同じ)へ移って、そのまま抜ける
             * (波で進めると、2 周目の始まりで一掃して 1 周近く走ることがあった。2026-10-28) */
            u16 d = (u16)lp[1] << 8;
            cam_s += d;
            return (u16)-d;
        }
    }
    return 0;
}
