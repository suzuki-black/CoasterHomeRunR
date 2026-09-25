/* rail.c — レール表の生成と補間 */
#include "rail.h"

u16 rail_len;

void rail_build(const RailSeg *segs)
{
    i16 x = 0, y = 0, z = 0;
    u16 yaw = 0;
    u16 n = 0;
    u8 i;
    for (; segs->len; segs++) {
        i16 dyaw = segs->dyaw / segs->len;
        for (i = 0; i < segs->len && n < RAIL_MAX; i++) {
            rail[n].x = x; rail[n].y = y; rail[n].z = z; rail[n].yaw = yaw;
            rail_spd[n] = segs->spd;
            n++;
            yaw += dyaw;
            x += SIN(yaw >> 8);
            z += COS(yaw >> 8);
            y += (i16)segs->dy << 4;
        }
    }
    rail_len = n;
}

u16 rail_s_max(void)
{
    return (u16)((rail_len - 1) << 6) - 1;
}

void rail_at(u16 s, V3 *p, u16 *yaw)
{
    u16 i;
    i16 f;
    RailPt *a, *b;
    if (s > rail_s_max()) s = rail_s_max();
    i = s >> 6;
    f = (s & 63) << 2;           /* 8.8 の割合 */
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
