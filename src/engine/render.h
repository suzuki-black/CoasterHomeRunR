/* render.h — 3D 描画(仕様書 7.4 / 7.5) */
#ifndef RENDER_H
#define RENDER_H
#include "engine.h"

/* 座標は 8.8 固定小数点(1.0 = 1m)。カメラ空間は x=右 y=上 z=奥 */
typedef struct { i16 x, y, z; } V3;

#define NEAR_Z   128        /* ニアクリップ 0.5m */
#define PROJ_F   512        /* 焦点距離 32 ブロック × 16(画面座標は 12.4) */
#define SCR_CX   (32 * 16)  /* 画面中心(12.4) */
#define SCR_CY   (23 * 16)

/* カメラ(render_begin で設定) */
extern i16 cam_x, cam_y, cam_z;
extern u8  cam_yaw;          /* 視線のヨー(256 分割) */
extern i8  cam_pitch;        /* 視線のピッチ(256 分割、正 = 上を向く)。0 なら計算を省く */
extern i16 cam_psin, cam_pcos;
extern i16 cam_sin, cam_cos;

void render_set_camera(i16 x, i16 y, i16 z, u16 yaw, i8 pitch);
void xform(i16 wx, i16 wy, i16 wz, V3 *o);
/* 凸多角形(n ≤ 6)をカメラ空間で描く。ニアクリップ→投影→塗り */
void poly3(const V3 *v, u8 n, u8 color);
void poly3p(const V3 *const *pv, u8 n, u8 color);   /* 同じで、頂点をポインタの並びで渡す(raster.s) */
/* 画面座標(12.4)の矩形をクリップして g_rx.. に設定。空なら 0 */
u8 rect_clip(i16 x0, i16 y0, i16 x1, i16 y1);

/* 点を投影(12.4)。z < NEAR_Z なら 0 */
u8 project(const V3 *v, i16 *sx, i16 *sy);

#endif
