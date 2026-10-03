/* render.c — カメラ変換・ニアクリップ・透視投影・凸多角形の塗り(仕様書 7.4 / 7.5)
 *
 * 画面座標は 12.4 固定小数点のブロック単位(64×46 ブロックの 3D 画面)。
 * 投影(project2)と塗り(raster)はアセンブラ(raster.s)。ここはニアクリップと矩形。
 */
#include "render.h"

i16 cam_x, cam_y, cam_z;
u8  cam_yaw;
i8  cam_pitch;
i16 cam_psin, cam_pcos;
i16 cam_sin, cam_cos;

static V3  clip_v[10];

void render_set_camera(i16 x, i16 y, i16 z, u16 yaw, i8 pitch)
{
    cam_x = x; cam_y = y; cam_z = z;
    cam_yaw = (u8)(yaw >> 8);
    cam_sin = SIN(cam_yaw);
    cam_cos = COS(cam_yaw);
    cam_pitch = pitch;
    cam_psin = SIN((u8)pitch);
    cam_pcos = COS((u8)pitch);
}

/* ワールド → カメラ。ワールド座標は 16bit で折り返すが、差分(±128m 以内)は正しく出る。
 * 計算は geom.s の xform3(xf_core: 定数との掛け算をその場で行う)。式は x' = dx*cos - dz*sin、z' = dx*sin + dz*cos、
 * ピッチがあれば y' = y*pcos - z*psin、z' = y*psin + z*pcos */
void xform3(const i16 *w, V3 *o);

void xform(i16 wx, i16 wy, i16 wz, V3 *o)
{
    i16 w[3];
    w[0] = wx; w[1] = wy; w[2] = wz;
    xform3(w, o);
}

u8 project(const V3 *v, i16 *sx, i16 *sy)
{
    i16 o[2];
    if (v->z < NEAR_Z) return 0;
    project2(v, o);
    *sx = o[0];
    *sy = o[1];
    return 1;
}

void poly3(const V3 *v, u8 n, u8 color)
{
    u8 i, m = 0, ain, bin;
    const V3 *a = &v[n - 1], *b = v;
    V3 *o = clip_v;
    /* ニアクリップ(Sutherland-Hodgman、z >= NEAR_Z を残す)。辺 a→b ごとに交点と b を出す */
    ain = a->z >= NEAR_Z;
    for (i = 0; i < n; i++, a = b, b++) {
        bin = b->z >= NEAR_Z;
        if (ain != bin) {
            i16 dz = b->z - a->z;
            i16 t = NEAR_Z - a->z;
            o->x = a->x + muldiv(b->x - a->x, t, dz);
            o->y = a->y + muldiv(b->y - a->y, t, dz);
            o->z = NEAR_Z;
            o++; m++;
        }
        if (bin) {
            o->x = b->x; o->y = b->y; o->z = b->z;
            o++; m++;
        }
        ain = bin;
    }
    if (m < 3) return;
    for (i = 0, o = clip_v; i < m; i++, o++) project2(o, &scr[i * 2]);
    raster(m, color);
}

u8 rect_clip(i16 x0, i16 y0, i16 x1, i16 y1)
{
    x0 = (x0 + 7) >> 4; x1 = (x1 + 7) >> 4;
    y0 = (y0 + 7) >> 4; y1 = (y1 + 7) >> 4;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > 64) x1 = 64;
    if (y1 > GAME_ROWS) y1 = GAME_ROWS;
    if (x0 >= x1 || y0 >= y1) return 0;
    g_rx = (u8)x0; g_ry = (u8)y0;
    g_rw = (u8)(x1 - x0); g_rh = (u8)(y1 - y0);
    return 1;
}
