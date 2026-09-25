/* env.c — 環境ポリゴン(仕様書 5.1 / 7.4)。試作版は洞窟の六角トンネルと木の枠
 *
 * レール上 RSTEP[m] ごとに六角形の断面(リング)を置き、隣り合うリングの間を 6 枚の四角形で塗る。
 * ペインター法: カメラから遠い区間から順に描く。木の枠はリングの面に立つ 3 枚の四角形で、
 * 「そのリングの手前側の区間」を描いた直後に描く(トンネルの内側からは壁に隠れないため)。
 *
 * 振り向き中は前後両方の区間を、距離の遠い順に交互に描く。
 */
#include "env.h"
#include "rail.h"

#define RSTEP 4
#define NF    5            /* 前方の区間数(20m) */
#define NB    5            /* 後方の区間数 */
#define NR    (NF + NB + 1)
#define VR    NR           /* 仮リング(カメラの 0.6m 先/後ろ)の番号 */
#define FAR_K 3            /* この番号から先(12m〜)のリングは「六角形 1 枚」で近似する */
#define VR_D  38           /* 仮リングまでの距離(1/64m) */

/* 断面(レール=目の位置からの相対。u=右 v=上、8.8 m) */
static const i16 hex_u[6] = { -256, 256, 486, 307, -307, -486 };
static const i16 hex_v[6] = {  358, 358,  51, -333, -333,   51 };
/* 面 k(頂点 k と k+1 の間)の色。[偶数区間 / 奇数区間][面]
 *   面: 天井, 右上の壁, 右下の壁, 床, 左下の壁, 左上の壁 */
static const u8 band_cols[2][6] = {
    { 10, 12, 13, 14, 13, 12 },
    { 11, 13, 12, 15, 12, 13 },
};
extern const u8 *g_band_col;
void band_fast(const i16 *sa, const i16 *sb);
/* 木の枠: 床を除く 5 面に沿った帯。内側の縁 = 頂点をリング中心へ 1/8 寄せた点(画面上で) */
#define FRAME_IN 224    /* 8.8: 7/8 */

static V3  ring[NR + 1][6];
static i16 ring_s[NR + 1][12];   /* 投影済みの画面座標 (x, y) × 6 */
static u8  ring_front[NR + 1];   /* bit k = 頂点 k が z >= NEAR_Z */
static u8  ring_ok[NR + 1];
static u16 ring_m0;

/* リング r(-NB..NF)の rail 上の番号(m) */
static i16 ring_meter(i8 r)
{
    return (i16)ring_m0 + (i16)r * RSTEP;
}

/* リング idx を、ワールドのレール点 p(ヨー yaw)から作る */
static void ring_make(u8 idx, i16 px, i16 py, i16 pz, u16 yaw)
{
    V3 c;
    u8 d, k, fm = 0;
    i16 cs, sn;
    V3 *v = ring[idx];
    i16 *o = ring_s[idx];
    xform(px, py, pz, &c);
    d = (u8)((yaw >> 8) - cam_yaw);
    cs = COS(d);
    sn = SIN(d);
    for (k = 0; k < 6; k++, v++, o += 2) {
        v->x = c.x + fmul(hex_u[k], cs);
        v->z = c.z - fmul(hex_u[k], sn);
        v->y = c.y + hex_v[k];
        if (v->z >= NEAR_Z) {
            project2(v, o);
            fm |= 1 << k;
        }
    }
    ring_front[idx] = fm;
    ring_ok[idx] = 1;
}

static void ring_calc(i8 r)
{
    i16 m = ring_meter(r);
    RailPt *p;
    if (m < 0 || m >= (i16)rail_len) return;
    p = &rail[m];
    ring_make((u8)(r + NB), p->x, p->y, p->z, p->yaw);
}

/* リング a と b の間の 6 面。4 頂点とも手前(z >= NEAR_Z)なら投影済みの座標で直接塗り、
 * 一部だけならニアクリップ(poly3)へ回す */
static void band_draw(u8 a, u8 b, u8 par)
{
    u8 k, k1, fa, fb, ma, mb;
    const i16 *sa, *sb;
    V3 q[4];
    if (!ring_ok[a] || !ring_ok[b]) return;
    fa = ring_front[a];
    fb = ring_front[b];
    if (!fa && !fb) return;
    if (fa == 0x3F && fb == 0x3F) {
        g_band_col = band_cols[par];
        band_fast(ring_s[a], ring_s[b]);
        return;
    }
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
            raster(4, band_cols[par][k]);
        } else if (ma + mb) {
            q[0] = ring[a][k];
            q[1] = ring[a][k1];
            q[2] = ring[b][k1];
            q[3] = ring[b][k];
            poly3(q, 4, band_cols[par][k]);
        }
    }
}

/* 区間 r(リング r と r+1 の間) */
static void seg_draw(i8 r)
{
    if (r < -NB || r >= NF) return;
    band_draw((u8)(r + NB), (u8)(r + NB + 1), (u8)((ring_meter(r) / RSTEP) & 1));
}

/* 仮リングをレール位置 sv に作る */
static u8 vring_make(u16 sv)
{
    V3 p;
    u16 yaw;
    if (sv > rail_s_max()) return 0;
    rail_at(sv, &p, &yaw);
    ring_make(VR, p.x, p.y, p.z, yaw);
    return ring_front[VR] == 0x3F;
}

/* リング r の木の枠。6 頂点とも手前のときだけ描く */
static void frame_draw(i8 r)
{
    u8 idx = (u8)(r + NB), k, k1;
    i16 m = ring_meter(r);
    i16 inner[12], cx, cy;
    const i16 *o;
    V3 c;
    RailPt *p;
    if (r < -NB || r > NF) return;
    if (!ring_ok[idx] || ((m / RSTEP) & 1) || ring_front[idx] != 0x3F) return;
    p = &rail[m];
    xform(p->x, p->y, p->z, &c);
    if (c.z < NEAR_Z) return;
    project2(&c, inner);
    cx = inner[0];
    cy = inner[1];
    o = ring_s[idx];
    for (k = 0; k < 12; k += 2) {
        inner[k]     = cx + fmul(o[k] - cx, FRAME_IN);
        inner[k + 1] = cy + fmul(o[k + 1] - cy, FRAME_IN);
    }
    for (k = 0; k < 6; k++) {
        if (k == 3) continue;          /* 床には枠を付けない */
        k1 = (k == 5) ? 0 : k + 1;
        scr[0] = o[k * 2];      scr[1] = o[k * 2 + 1];
        scr[2] = o[k1 * 2];     scr[3] = o[k1 * 2 + 1];
        scr[4] = inner[k1 * 2]; scr[5] = inner[k1 * 2 + 1];
        scr[6] = inner[k * 2];  scr[7] = inner[k * 2 + 1];
        raster(4, 9);
    }
}

/* リング r の開口部を黒で塞げるか(6 頂点とも手前) */
static u8 cap_ok(i8 r)
{
    u8 idx = (u8)(r + NB);
    return ring_ok[idx] && ring_front[idx] == 0x3F;
}

static void hex_draw(i8 r, u8 color)
{
    u8 idx = (u8)(r + NB), k;
    for (k = 0; k < 12; k++) scr[k] = ring_s[idx][k];
    raster(6, color);
}

static void cap_draw(i8 r)
{
    hex_draw(r, C_BLACK);
}

void env_draw(u16 s, u8 look)
{
    /* look: 視線の相対ヨー(0 = 前、128 = 後ろ) */
    u8 fwd = !(look > 80 && look < 176);
    u8 back = (look > 48 && look < 208);
    i8 r, k, rn;
    u8 capped = 1;

    ring_m0 = (s >> 6) & ~(RSTEP - 1);
    for (r = 0; r <= NR; r++) ring_ok[r] = 0;
    for (r = back ? -NB : 0; r <= (fwd ? NF : 1); r++) ring_calc(r);

    /* トンネルの内側からは壁が視界を全部覆うので、奥の開口部を黒く塞げば
     * バッファの全消去は要らない。レールの端で塞げないときだけ全消去する */
    if (fwd && !cap_ok(NF)) capped = 0;
    if (back && !cap_ok(-NB)) capped = 0;
    if (!capped) buf_fill(C_BLACK, BUF);

    if (fwd && !back) {
        u16 sv = s + VR_D;
        i8 kn = FAR_K;
        /* 遠方(12m〜): リングの六角形を帯の色で手前から奥へ重ねる(遠いリングほど画面の内側)。
         * 6 面ずつ塗るより辺が 1/4 で済む。どれかのリングが画面外へ回り込んでいれば全部を普通に描く */
        if (capped) {
            for (k = FAR_K; k < NF; k++) if (!cap_ok(k)) break;
            if (k == NF) {
                for (k = FAR_K; k < NF; k++)
                    hex_draw(k, band_cols[(ring_meter(k) / RSTEP) & 1][1]);
            } else {
                kn = NF;
            }
            cap_draw(NF);
        } else {
            kn = NF;
        }
        /* 手前: 6 面ずつ。カメラを含む区間は「0.6m 先の仮リング → その先の最初のリング」で置き換える
         * (仮リングの頂点は全部画面外に投影されるので、ニアクリップ無しで画面の縁まで覆える) */
        rn = ((u16)(ring_m0 + RSTEP) * 64 > sv + 6) ? 1 : 2;
        for (k = kn - 1; k >= rn; k--) { seg_draw(k); frame_draw(k + 1); }
        if (vring_make(sv)) {
            band_draw(VR, (u8)(rn + NB), (u8)(((sv >> 6) / RSTEP) & 1));
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
                for (k = -FAR_K; k > -NB; k--)
                    hex_draw(k, band_cols[(ring_meter(k - 1) / RSTEP) & 1][1]);
            } else {
                kn = -NB;
            }
            cap_draw(-NB);
        } else {
            kn = -NB;
        }
        rn = ((u16)ring_m0 * 64 + 6 < sv) ? 0 : -1;
        for (k = kn; k < rn; k++) { seg_draw(k); frame_draw(k); }
        if (vring_make(sv)) {
            band_draw((u8)(rn + NB), VR, (u8)(((sv >> 6) / RSTEP) & 1));
            frame_draw(rn);
        } else {
            for (k = rn; k <= 0; k++) { seg_draw(k); frame_draw(k); }
        }
        return;
    }
    if (capped) {
        if (fwd) cap_draw(NF);
        if (back) cap_draw(-NB);
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
