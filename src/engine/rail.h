/* rail.h — レール(仕様書 7.7)
 *
 * レール区間 = (長さ[m], ヨー角変化, 高さ変化, 速度) の列から、1m ごとのレール表を作る。
 * レール上の位置 s は 1/64m 単位(u16、最大 1024m)。
 */
#ifndef RAIL_H
#define RAIL_H
#include "render.h"

typedef struct {
    u8  len;    /* 区間の長さ[m](0 で終端) */
    i16 dyaw;   /* 区間全体のヨー角変化(65536 = 1 周。正 = 右) */
    i8  dy;     /* 1m あたりの高さ変化(1/16m) */
    u8  spd;    /* 速度(m/秒 × 4) */
} RailSeg;

typedef struct { i16 x, y, z; u16 yaw; } RailPt;

/* ページ3 の空き(0xE100-0xEFFF)に置く。DATA/BSS(0xCC00-0xDFFF)を圧迫しないため */
#define RAIL_MAX 400
#define rail     ((RailPt *)0xE100)                 /* 8 × 400 = 3200 B */
#define rail_spd ((u8 *)(0xE100 + 8 * RAIL_MAX))     /* 400 B → 0xEE90 まで */
extern u16    rail_len;        /* 点の数 */

void rail_build(const RailSeg *segs);
/* 位置 s の点(ワールド)と向き。s は末尾で止める */
void rail_at(u16 s, V3 *p, u16 *yaw);
/* 位置 s から右へ lat、上へ up(8.8 m)の点をカメラ空間へ */
void rail_cam(u16 s, i16 lat, i16 up, V3 *o);
u16  rail_s_max(void);

#endif
