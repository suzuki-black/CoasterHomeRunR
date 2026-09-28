/* rail.c — レール表の生成と補間
 *
 * 点は 2m ごと(RAIL_STEP)。区間の列を続けて足していける(rail_add)。分岐は、分岐点までを作ったところの
 * 状態を覚えておき(rail_fork)、選ばれた側の区間と合流後の区間を分岐点から作り直す(rail_refork)。
 */
#include "rail.h"

u16 rail_len;                    /* 長さ[m](点の数 × 2) */
static u16 rb_n, rb_yaw;         /* 次に置く点の番号と、そのときの向き・位置 */
static i16 rb_x, rb_y, rb_z;
static u16 fk_n, fk_yaw;         /* 分岐点の状態 */
static i16 fk_x, fk_y, fk_z;

void rail_begin(void)
{
    rb_n = 0; rb_yaw = 0;
    rb_x = rb_y = rb_z = 0;
    rail_len = 0;
}

void rail_add(const RailSeg *segs)
{
    u8 i, steps;
    for (; segs->len; segs++) {
        steps = segs->len >> 1;
        {
            i16 dyaw = segs->dyaw / steps;
            for (i = 0; i < steps && rb_n < RAIL_MAX; i++) {
                RailPt *p = &rail[rb_n];
                p->x = rb_x; p->y = rb_y; p->z = rb_z; p->yaw = rb_yaw;
                rail_spd[rb_n] = segs->spd;
                rb_n++;
                rb_yaw += dyaw;
                rb_x += SIN(rb_yaw >> 8) << 1;
                rb_z += COS(rb_yaw >> 8) << 1;
                rb_y += (i16)segs->dy << 5;
            }
        }
    }
    rail_len = rb_n << 1;
}

void rail_fork(void)
{
    fk_n = rb_n; fk_yaw = rb_yaw;
    fk_x = rb_x; fk_y = rb_y; fk_z = rb_z;
}

u16 rail_fork_m(void)
{
    return fk_n << 1;
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

void rail_at(u16 s, V3 *p, u16 *yaw)
{
    u16 i;
    i16 f;
    RailPt *a, *b;
    if (s > rail_s_max()) s = rail_s_max();
    i = s >> 7;
    f = (s & 127) << 1;          /* 8.8 の割合 */
    a = &rail[i];
    b = a + 1;
    p->x = a->x + fmul(b->x - a->x, f);
    p->y = a->y + fmul(b->y - a->y, f);
    p->z = a->z + fmul(b->z - a->z, f);
    *yaw = a->yaw + fmul((i16)(b->yaw - a->yaw), f);
}

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
