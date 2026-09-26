/* env.c — 環境ポリゴン(仕様書 5.1 / 7.4)。洞窟(鉱山の坑道)
 *
 * レール上 RSTEP[m] ごとに断面(リング)を置き、隣り合うリングの間を 6 枚の四角形で塗る。
 * 断面はリングごとに不規則(ステージ開始時に乱数で作る。幅と天井の高さはゆっくり変わる)。
 * 面の色も乱数で岩肌の色から選び、トロッコのランプに照らされたように手前ほど明るく、奥ほど暗くする。
 * 床にはレールと枕木、ところどころに坑道の支保工(柱と梁)・鍾乳石・床の岩を置く。
 * リング面の飾りとレールは、リング中心の投影と 1/z(ring_c)から 2D で求める(投影を増やさない)。
 *
 * ペインター法: カメラから遠い区間から順に描く。区間ごとに「面 → 枕木 → レール」、
 * リング面の飾りはそのリングの手前側の区間の後に描く(トンネルの内側からは壁に隠れないため)。
 * 振り向き中は前後両方の区間を、距離の遠い順に交互に描く。
 */
#include "env.h"
#include "rail.h"

#define RSTEP 4            /* リングの間隔[m] */
#define NF    5            /* 前方の区間数(20m) */
#define NB    5            /* 後方の区間数 */
#define NR    (NF + NB + 1)
#define VR    NR           /* 仮リング(カメラの 0.6m 先/後ろ)の番号 */
#define FAR_K 3            /* この番号から先(12m〜)のリングは「断面 1 枚」で近似する */
#define VR_D  38           /* 仮リングまでの距離(1/64m) */

/* ステージ開始時に作る表(ページ0の RAM の空き。sys.s の sys_page0_ram 参照) */
#define PROF   ((i16 *)0x1A00)    /* [リング][6 頂点][u, v]、最大 100 リング = 2400 B */
#define TONE   ((u8 *)0x2400)     /* [リング][6 面] の基本の色 */
#define DECO   ((u8 *)0x2680)     /* [リング] 飾りの印 */
#define MAX_RINGS 100
enum { D_STALAC = 1, D_ROCK_L = 2, D_ROCK_R = 4, D_TIMBER = 8 };

/* 色(パレット 9-15、stage のパレットで決まる): 9 = 木、10〜12 = 岩(暗→明)、13〜14 = 土(暗→明)、15 = レール */
#define C_WOOD   9
#define C_ROCK0 10
#define C_ROCK1 11
#define C_ROCK2 12
#define C_DIRT0 13
#define C_DIRT1 14
#define C_RAIL  15

/* raster.s: 辺キャッシュつきの塗り(リングの辺・放射方向の辺を面どうしで共有する) */
extern const u8 *g_band_col;
extern const i16 *g_ring_s;
void eslot_clear(void);
void band_fast(u8 a, u8 b);
void hex_fast(u8 r);
i16 qmul(i16 v, u16 q);
/* geom.s: リングの頂点(断面は g_rm_prof) */
extern const i16 *g_rm_prof;
V3  ring_v[NR + 1][6];
i16 ring_s[NR + 1][12];          /* 投影済みの画面座標 (x, y) × 6 */
i16 ring_c[NR + 1][3];           /* 中心の投影 (x, y) と q = 2^23/z(手前でなければ q = 0) */
u8  ring_front[NR + 1];          /* bit k = 頂点 k が z >= NEAR_Z */
u8  ring_ok[NR + 1];
void ring_make(u8 idx, const RailPt *p);
static u16 ring_m0;
static u8  ring_no[NR + 1];      /* リングの通し番号(断面・色・飾りの表を引く) */
static i16 vprof[12];            /* 仮リングの断面(前後のリングから補間) */

/* ---------------------------------------------------------------------------
 * ステージ開始時: 断面・色・飾りを乱数で作る
 * --------------------------------------------------------------------------- */
static u16 rng;
static u8 rnd(void)
{
    rng = rng * 25173 + 13849;
    return (u8)(rng >> 8);
}
/* -amp..+amp */
static i16 jit(i16 amp)
{
    return (i16)(((i16)rnd() - 128) * amp) >> 7;
}

/* 基本の断面(u, v): 天井の左右、右の壁、床の右左、左の壁 */
static const i16 base_u[6] = { -230, 230, 490, 310, -310, -490 };
static const i16 base_v[6] = {  380, 360,  30, -333, -333,  50 };

void env_build(u16 seed)
{
    u8 r, k, n;
    i16 w = 256, h = 256;
    rng = seed;
    n = (u8)(rail_len / RSTEP + 2);
    if (n > MAX_RINGS) n = MAX_RINGS;
    for (r = 0; r < n; r++) {
        i16 *p = &PROF[r * 12];
        u8 *t = &TONE[r * 6];
        u8 d = 0;
        /* 幅と天井の高さはゆっくり変える(狭い所・広い所) */
        w += jit(28); if (w < 205) w = 205; if (w > 330) w = 330;
        h += jit(24); if (h < 220) h = 220; if (h > 320) h = 320;
        for (k = 0; k < 6; k++) {
            i16 u = (i16)(((i32)base_u[k] * w) >> 8);
            i16 v = base_v[k];
            if (k == 3 || k == 4) {            /* 床は平らに(レールを敷く) */
                u += jit(24);
            } else if (k <= 1) {               /* 天井 */
                u += jit(80);
                v = (i16)(((i32)v * h) >> 8) + jit(70);
            } else {                            /* 壁のふくらみ */
                u += jit(90);
                v += jit(90);
            }
            p[k * 2] = u;
            p[k * 2 + 1] = v;
            /* 面の基本の色: 天井は暗い岩、上の壁は中間、ランプに照らされる下の壁は明るめ、床は土 */
            if (k == 3)            t[k] = (rnd() & 1) ? C_DIRT1 : C_DIRT0;
            else if (k == 0)       t[k] = (rnd() < 150) ? C_ROCK0 : C_ROCK1;
            else if (k == 1 || k == 5) t[k] = (rnd() < 128) ? C_ROCK1 : C_ROCK2;
            else                   t[k] = (rnd() < 170) ? C_ROCK2 : C_ROCK1;
        }
        if (rnd() < 90) d |= D_STALAC | ((rnd() & 3) << 4);
        if (rnd() < 60) d |= D_ROCK_L;
        if (rnd() < 60) d |= D_ROCK_R;
        if (r % 5 == 2) d |= D_TIMBER;
        DECO[r] = d;
    }
}

/* ---------------------------------------------------------------------------
 * リング
 * --------------------------------------------------------------------------- */
/* リング r(-NB..NF)の rail 上の番号(m) */
static i16 ring_meter(i8 r)
{
    return (i16)ring_m0 + (i16)r * RSTEP;
}

static void ring_calc(i8 r)
{
    i16 m = ring_meter(r);
    u8 idx = (u8)(r + NB);
    if (m < 0 || m >= (i16)rail_len) return;
    ring_no[idx] = (u8)(m / RSTEP);
    g_rm_prof = &PROF[ring_no[idx] * 12];
    ring_make(idx, &rail[m]);
}

/* 仮リングをレール位置 sv に作る(断面は前後のリングから補間) */
static u8 vring_make(u16 sv)
{
    V3 p;
    RailPt rp;
    u8 k, no;
    i16 f;
    const i16 *a, *b;
    if (sv > rail_s_max()) return 0;
    no = (u8)((sv >> 6) / RSTEP);
    f = (i16)(sv - (u16)no * RSTEP * 64) ;       /* 0..255(1/64m × 4m) */
    a = &PROF[no * 12];
    b = &PROF[(no + 1) * 12];
    for (k = 0; k < 12; k++) vprof[k] = a[k] + (i16)(((i32)(b[k] - a[k]) * f) >> 8);
    rail_at(sv, &p, &rp.yaw);
    rp.x = p.x; rp.y = p.y; rp.z = p.z;
    ring_no[VR] = no;
    g_rm_prof = vprof;
    ring_make(VR, &rp);
    return ring_front[VR] == 0x3F;
}

/* ---------------------------------------------------------------------------
 * リング面の 2D の形: 中心 + (u, v) * q
 * --------------------------------------------------------------------------- */
static void rpt(u8 i, u8 idx, i16 u, i16 v)
{
    const i16 *c = ring_c[idx];
    scr[i * 2] = c[0] + qmul(u, (u16)c[2]);
    scr[i * 2 + 1] = c[1] - qmul(v, (u16)c[2]);
}

/* リング面の四角形(u0..u1 × v0..v1) */
static void rquad(u8 idx, i16 u0, i16 u1, i16 v0, i16 v1, u8 color)
{
    rpt(0, idx, u0, v0);
    rpt(1, idx, u1, v0);
    rpt(2, idx, u1, v1);
    rpt(3, idx, u0, v1);
    raster(4, color);
}

#define FLOOR_V (-333)

/* 区間(リング a → b)の床のレール 2 本と枕木 */
static void rails_draw(u8 a, u8 b)
{
    u8 side;
    if (!ring_c[a][2] || !ring_c[b][2]) return;
    /* 枕木(区間の手前のリングの面に、横長の板) */
    rquad(a, -180, 180, FLOOR_V, FLOOR_V + 14, C_WOOD);
    for (side = 0; side < 2; side++) {
        i16 u0 = side ? 102 : -128, u1 = side ? 128 : -102;
        rpt(0, a, u0, FLOOR_V);
        rpt(1, a, u1, FLOOR_V);
        rpt(2, b, u1, FLOOR_V);
        rpt(3, b, u0, FLOOR_V);
        raster(4, C_RAIL);
    }
}

/* リング idx の面の飾り: 坑道の支保工(柱 2 本と梁)、鍾乳石、床の岩 */
static void deco_draw(u8 idx)
{
    u8 d;
    if (!ring_ok[idx] || !ring_c[idx][2]) return;
    d = DECO[ring_no[idx]];
    if (!d) return;
    if (d & D_TIMBER) {
        rquad(idx, -300, -250, FLOOR_V, 300, C_WOOD);
        rquad(idx, 250, 300, FLOOR_V, 300, C_WOOD);
        rquad(idx, -330, 330, 270, 320, C_WOOD);
    }
    if (d & D_STALAC) {
        i16 o = (i16)((d >> 4) & 3) * 70 - 105;     /* 横位置をばらす */
        rpt(0, idx, o - 60, 340);
        rpt(1, idx, o + 50, 340);
        rpt(2, idx, o - 5, 150);
        raster(3, C_ROCK2);
    }
    if (d & D_ROCK_L) {
        rpt(0, idx, -420, FLOOR_V);
        rpt(1, idx, -250, FLOOR_V);
        rpt(2, idx, -330, FLOOR_V + 110);
        raster(3, C_ROCK2);
    }
    if (d & D_ROCK_R) {
        rpt(0, idx, 240, FLOOR_V);
        rpt(1, idx, 400, FLOOR_V);
        rpt(2, idx, 310, FLOOR_V + 90);
        raster(3, C_ROCK2);
    }
}

/* ---------------------------------------------------------------------------
 * 区間
 * --------------------------------------------------------------------------- */
static u8 band_col[6];

/* 面の色: リング na の基本の色を、区間の遠さ dist(0 = 手前)で暗くする(ランプの明かり) */
static void set_band_col(u8 na, u8 dist)
{
    u8 k, c;
    const u8 *t = &TONE[na * 6];
    for (k = 0; k < 6; k++) {
        c = t[k];
        if (dist >= 2) {
            if (c == C_ROCK2) c = C_ROCK1;
            else if (c == C_ROCK1) c = C_ROCK0;
            else c = C_DIRT0;
        }
        band_col[k] = c;
    }
    g_band_col = band_col;
}

/* リング a と b の間の 6 面 + レール。4 頂点とも手前(z >= NEAR_Z)なら投影済みの座標で直接塗り、
 * 一部だけならニアクリップ(poly3)へ回す */
static void band_draw(u8 a, u8 b, u8 dist)
{
    u8 k, k1, fa, fb, ma, mb;
    const i16 *sa, *sb;
    V3 q[4];
    if (!ring_ok[a] || !ring_ok[b]) return;
    fa = ring_front[a];
    fb = ring_front[b];
    if (!fa && !fb) return;
    set_band_col(ring_no[a < b ? a : b], dist);
    if (fa == 0x3F && fb == 0x3F) {
        band_fast(a, b);
    } else {
        sa = ring_s[a];
        sb = ring_s[b];
        for (k = 0; k < 6; k++) {
            k1 = (k == 5) ? 0 : k + 1;
            ma = (u8)((fa >> k) & 1) + (u8)((fa >> k1) & 1);
            mb = (u8)((fb >> k) & 1) + (u8)((fb >> k1) & 1);
            if (ma + mb == 4) {
                scr[0] = sa[k * 2];  scr[1] = sa[k * 2 + 1];
                scr[2] = sa[k1 * 2]; scr[3] = sa[k1 * 2 + 1];
                scr[4] = sb[k1 * 2]; scr[5] = sb[k1 * 2 + 1];
                scr[6] = sb[k * 2];  scr[7] = sb[k * 2 + 1];
                raster(4, band_col[k]);
            } else if (ma + mb) {
                q[0] = ring_v[a][k];
                q[1] = ring_v[a][k1];
                q[2] = ring_v[b][k1];
                q[3] = ring_v[b][k];
                poly3(q, 4, band_col[k]);
            }
        }
    }
    if (dist < 3) rails_draw(a, b);
}

/* 区間 r(リング r と r+1 の間) */
static void seg_draw(i8 r)
{
    u8 d;
    if (r < -NB || r >= NF) return;
    d = (u8)(r < 0 ? -r - 1 : r);
    band_draw((u8)(r + NB), (u8)(r + NB + 1), d);
}

static void frame_draw(i8 r)
{
    if (r < -NB || r > NF) return;
    deco_draw((u8)(r + NB));
}

/* リング r の開口部を塞げるか(6 頂点とも手前) */
static u8 cap_ok(i8 r)
{
    u8 idx = (u8)(r + NB);
    return ring_ok[idx] && ring_front[idx] == 0x3F;
}

static void hex_draw(i8 r, u8 color)
{
    g_color = color;
    hex_fast((u8)(r + NB));
}

void env_draw(u16 s, u8 look)
{
    /* look: 視線の相対ヨー(0 = 前、128 = 後ろ) */
    u8 fwd = !(look > 80 && look < 176);
    u8 back = (look > 48 && look < 208);
    i8 r, k, rn;
    u8 capped = 1;

    ring_m0 = (s >> 6) - (s >> 6) % RSTEP;
    for (r = 0; r <= NR; r++) ring_ok[r] = 0;
    g_ring_s = &ring_s[0][0];
    eslot_clear();
    for (r = back ? -NB : 0; r <= (fwd ? NF : 1); r++) ring_calc(r);

    /* トンネルの内側からは壁が視界を全部覆うので、奥の開口部を黒く塞げば
     * バッファの全消去は要らない。レールの端で塞げないときだけ全消去する */
    if (fwd && !cap_ok(NF)) capped = 0;
    if (back && !cap_ok(-NB)) capped = 0;
    if (!capped) buf_fill(C_BLACK, BUF);

    if (fwd && !back) {
        u16 sv = s + VR_D;
        i8 kn = FAR_K;
        /* 遠方(12m〜): リングの断面を一番暗い岩の色で手前から奥へ重ねる(遠いリングほど画面の内側)。
         * どれかのリングが画面外へ回り込んでいれば全部を普通に描く */
        if (capped) {
            for (k = FAR_K; k < NF; k++) if (!cap_ok(k)) break;
            if (k == NF) {
                for (k = FAR_K; k < NF; k++) hex_draw(k, C_ROCK0);
            } else {
                kn = NF;
            }
            hex_draw(NF, C_BLACK);
        } else {
            kn = NF;
        }
        /* 手前: 6 面ずつ。カメラを含む区間は「0.6m 先の仮リング → その先の最初のリング」で置き換える
         * (仮リングの頂点は全部画面外に投影されるので、ニアクリップ無しで画面の縁まで覆える) */
        rn = ((u16)(ring_m0 + RSTEP) * 64 > sv + 6) ? 1 : 2;
        for (k = kn - 1; k >= rn; k--) { seg_draw(k); frame_draw(k + 1); }
        if (vring_make(sv)) {
            band_draw(VR, (u8)(rn + NB), 0);
            frame_draw(rn);
        } else {
            for (k = rn - 1; k >= 0; k--) { seg_draw(k); frame_draw(k + 1); }
        }
        return;
    }
    if (back && !fwd && s >= VR_D + 6) {
        u16 sv = s - VR_D;
        i8 kn = -FAR_K;
        if (capped) {
            for (k = -FAR_K; k > -NB; k--) if (!cap_ok(k)) break;
            if (k == -NB) {
                for (k = -FAR_K; k > -NB; k--) hex_draw(k, C_ROCK0);
            } else {
                kn = -NB;
            }
            hex_draw(-NB, C_BLACK);
        } else {
            kn = -NB;
        }
        rn = ((u16)ring_m0 * 64 + 6 < sv) ? 0 : -1;
        for (k = kn; k < rn; k++) { seg_draw(k); frame_draw(k); }
        if (vring_make(sv)) {
            band_draw((u8)(rn + NB), VR, 0);
            frame_draw(rn);
        } else {
            for (k = rn; k <= 0; k++) { seg_draw(k); frame_draw(k); }
        }
        return;
    }
    if (capped) {
        if (fwd) hex_draw(NF, C_BLACK);
        if (back) hex_draw(-NB, C_BLACK);
    }

    /* 振り向き中(と後方の端): 前後の区間を距離の遠い順に交互に描き、カメラを含む区間はクリップ */
    for (k = NB > NF ? NB : NF; k >= 1; k--) {
        if (fwd && k < NF) { seg_draw(k); frame_draw(k + 1); }
        if (back) { seg_draw(-k); frame_draw(-k); }
    }
    seg_draw(0);
    frame_draw(1);
    frame_draw(0);
}
