/* rail.h — レール(仕様書 7.7)
 *
 * レール区間 = (長さ[m], ヨー角変化, 高さ変化, 速度) の列から、2m ごとのレール表を作る。
 * レール上の位置 s は 1/64m 単位(u16、最大 1024m)。
 */
#ifndef RAIL_H
#define RAIL_H
#include "render.h"

typedef struct {
    u8  len;    /* 区間の長さ[m](偶数。0 で終端) */
    i16 dyaw;   /* 区間全体のヨー角変化(65536 = 1 周。正 = 右) */
    i8  dy;     /* 1m あたりの高さ変化(1/16m) */
    u8  spd;    /* 速度(m/秒 × 4) */
} RailSeg;

typedef struct { i16 x, y, z; u16 yaw; } RailPt;

/* ページ3 の空き(0xE100-0xEFFF)に置く。DATA/BSS(0xCC00-0xDFFF)を圧迫しないため。点は 2m ごと */
#define RAIL_STEP 2
#define RAIL_MAX 400                                /* 800m */
#define rail     ((RailPt *)0xE100)                 /* 8 × 400 = 3200 B */
#define rail_spd ((u8 *)(0xE100 + 8 * RAIL_MAX))     /* 400 B → 0xEE90 まで */
#define RAIL_PT(m) (&rail[(m) >> 1])                /* レール上の位置 m[m](偶数)の点 */
extern u16    rail_len;        /* 長さ[m] */

/* 区間の列(長さは偶数[m])からレール表を作る: rail_begin → rail_add … */
void rail_begin(void);
void rail_add(const RailSeg *segs);
/* 分岐: 分岐点まで作ったところで rail_fork、選んだ側を rail_refork(分岐の区間, 合流後の区間)で作り直す */
void rail_fork(void);
u16  rail_fork_m(void);        /* 分岐点の位置[m] */
void rail_refork(const RailSeg *branch, const RailSeg *tail);
/* 分岐点から segs をたどった点を out へ最大 max 個(2m ごと)。戻り値は個数 */
u8   rail_sample(const RailSeg *segs, RailPt *out, u8 max);
/* 位置 s の点(ワールド)と向き。s は末尾で止める */
void rail_at(u16 s, V3 *p, u16 *yaw);
/* 位置 s から右へ lat、上へ up(8.8 m)の点をカメラ空間へ */
void rail_cam(u16 s, i16 lat, i16 up, V3 *o);
u16  rail_s_max(void);

#endif
