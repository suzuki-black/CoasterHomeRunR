/* rail.c — レール表の生成と補間
 *
 * 点は 2m ごと(RAIL_STEP)。区間の列を続けて足していける(rail_add)。分岐は、分岐点までを作ったところの
 * 状態を覚えておき(rail_fork)、選ばれた側の区間と合流後の区間を分岐点から作り直す(rail_refork)。
 */
#include "rail.h"

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
