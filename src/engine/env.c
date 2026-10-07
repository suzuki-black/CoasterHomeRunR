/* env.c — 環境ポリゴン(仕様書 5.1 / 7.4)
 *
 * レール上 RSTEP[m] ごとに断面(リング)を置き、隣り合うリングの間を 6 枚の四角形で塗る。
 * 断面の形・面の色・飾りは「地形の種類」(ステージの zones で区間ごとに指定)で決まる。
 *   洞窟(炭鉱の坑道): ほぼ垂直な壁と平らな天井。手前ほど明るく奥ほど暗い(トロッコのランプ)。
 *   地底湖: 大きな空洞の水面と岩壁の岩棚、垂れ下がる鍾乳石、水面の岩(レールは無い。筏で渡る)。
 *   森: 土の道、両側の下草と茂み、木の幹と葉の塊。
 *   町: 両側の家並み(壁と屋根。家と家の間は空く)。村の入り口: 通りの突き当たりに村の門。
 *   遊園地: コースターの高架(細い軌道と支柱。遠くにテント・観覧車・灯り)。丘: 草の斜面を下る土の道。空中: 何も描かない。
 *   柵沿い: 左に赤と黄の縞の柵(上に灯り)、柵の上に見えるテント・メリーゴーラウンド・観覧車、右は下っていく草の斜面。
 * 色 0 の面は塗らない(屋外の区間は天井の面が空)。屋外が見えるときは、先に空と地面を
 * 地平線の高さで塗り分け(仕様書 5.1 の 2 色塗り)てから区間を描く。
 * リング面の飾りとレールは、リング中心の投影と 1/z(ring_c)から 2D で求める(投影を増やさない)。
 *
 * ペインター法: カメラから遠い区間から順に描く。区間ごとに「面 → 枕木 → レール」、
 * リング面の飾りはそのリングの手前側の区間の後に描く。
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

/* ステージ開始時に作る表。断面(一番大きい)はページ0の 0x2E00-0x3FFF(いつも見える)、色・飾り・種類は
 * 地形モジュール(セグメント 5)の中の 0xB600-0xBBFF(地形の処理からしか使わない)。
 * モジュールのコードは 0xB600 まで、ページ0の表とコード(_PAGE0)は 0x2E00 まで(tools/mkrom.py が検査する) */
#define MAX_RINGS 192              /* 768m */
#define PROF   ((i16 *)0x2E00)    /* [リング][6 頂点][u, v] = 4608 B(ページ0) */
#define TONE   ((u8 *)0xB600)     /* [リング][6 面] の基本の色(0 = 塗らない) = 1152 B */
#define DECO   ((u8 *)0xBA80)     /* [リング] 飾りの印 */
#define KIND   ((u8 *)0xBB40)     /* [リング] 地形の種類(〜0xBBFF。0xBC00 からはモジュールの変数) */
/* 飾りの印。LAMP / TIMBER は森では木の幹 / 葉の塊。
 * 町では LAMP_L / LAMP_R = 左 / 右の家の始まり(手前の妻壁)、ROCK_L / ROCK_R = 家の終わり、TIMBER = 自宅の正面 */
enum { D_LAMP_L = 1, D_ROCK_L = 2, D_ROCK_R = 4, D_TIMBER = 8, D_LAMP_R = 16,
       D_OPEN = 32,      /* 天井が無い(空が見える) */
       D_NORAIL = 64,    /* レールを描かない */
       D_EMPTY = 128 };  /* 面を 1 枚も描かない(空中) */

/* 色(パレット 9-15、stage のパレットで決まる)
 *   洞窟(ステージ 1): 9 = 木、10〜12 = 岩(暗→明)、13〜14 = 土(暗→明)、15 = レール
 *   地底湖(出口の坑道も): 9 = 木、10 = 暗い岩・レール、11 = 岩、12 = 鍾乳石、13 = 水、14 = 明るい水
 *   森: 9 = 幹・落ち葉、10 = 影・レール、11 = 土の道、12 = 下草、13 = 茂み、14 = 葉、15 = 空
 *   遊園地・丘: 9 = 木・枕木、10 = 暗い骨組み・レール、11 = 土の道、12 = 草、13 = 赤(テント・ゴンドラ)、14 = 灯り、15 = 空
 *   町: 9 = 戸、10 = 窓・レール、11 = 道、12 = 壁、13 = 屋根、14 = 別の色の壁(自宅前では自宅の壁)、15 = 空 */
#define C_WOOD   9
#define C_ROCK0 10
#define C_ROCK1 11
#define C_ROCK2 12
#define C_DIRT0 13
#define C_DIRT1 14
#define C_RAIL  15
#define C_GRASS 12
#define C_WATER 13
#define C_WATER1 14
#define C_SKY   15

/* raster.s: 辺キャッシュつきの塗り(リングの辺・放射方向の辺を面どうしで共有する) */
extern const u8 *g_band_col;
extern const i16 *g_ring_s;
void eslot_clear(void);
void band_fast(u8 a, u8 b);
void hex_fast(u8 r);
/* geom.s: リングの頂点(断面は g_rm_prof) */
extern const i16 *g_rm_prof;
/* リングの頂点・投影(実体は常駐部の shared.c。geom.s の ring_make が書く) */
extern V3  ring_v[NR + 1][6];
extern i16 ring_s[NR + 1][12];   /* 投影済みの画面座標 (x, y) × 6 */
extern i16 ring_c[NR + 1][3];    /* 中心の投影 (x, y) と q = 2^23/z(手前でなければ q = 0) */
extern u8  ring_front[NR + 1];   /* bit k = 頂点 k が z >= NEAR_Z */
extern u8  ring_ok[NR + 1];
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

/* 基本の断面(u, v)。頂点は 天井(空)の左右 → 右 → 床の右左 → 左 の順(面 k = 頂点 k → k+1)。
 * 洞窟は壁の中ほどを天井の角とほぼ同じ横位置にして、ほぼ垂直な四角い坑道にする */
static const i16 base_uv[K_COUNT][12] = {
    /* 洞窟 */  { -320, 320,  320, 320,  345,  -40,  300, -333, -300, -333, -345,  -40 },
    /* 出口 */  { -320, 320,  320, 320,  345,  -40,  300, -333, -300, -333, -345,  -40 },
    /* 地底湖 */{-1300,1400, 1300,1400, 1150,  420, 1000, -380,-1000, -380,-1150,  420 },
    /* 森   */  { -760,1400,  760,1400,  720,  250,  420, -345, -420, -345, -720,  250 },
    /* 町   */  { -950, 900,  950, 900,  650,  520,  650, -345, -650, -345, -650,  520 },
    /* 村   */  { -950, 900,  950, 900,  650,  520,  650, -345, -650, -345, -650,  520 },
    /* 遊園地 */{ -400, 200,  400, 200,  230, -333,  200, -333, -200, -333, -230, -333 },
    /* 丘   */  { -900, 400,  900, 400,  900, -250,  380, -345, -380, -345, -900, -250 },
    /* 空中 */  { -400, 200,  400, 200,  230, -333,  200, -333, -200, -333, -230, -333 },
    /* 柵沿い */{ -520, 560, 1000, 200, 1000, -700,  400, -345, -400, -345, -520, -345 },
};
#define HOUSE_U0  650      /* 家の壁(道に面した面)の横位置 */
#define HOUSE_U1 1250      /* 家の奥行きの端 */
#define EAVE_V    520      /* 軒の高さ */
#define RIDGE_U   950      /* 棟の横位置と高さ */
#define RIDGE_V   900
static u8 hs_st[2], hs_cnt[2], hs_col[2];   /* 家並み: 左右それぞれの今の状態(1 = 家)・残りリング数・壁の色 */
static u8 rail_col;

/* 地形 kind のリング r の断面・色・飾りを作る。w / h は幅と高さのゆっくりした変化(256 = 基本) */
static void ring_build(u8 r, u8 kind, i16 w, i16 h)
{
    i16 *p = &PROF[r * 12];
    u8 *t = &TONE[r * 6];
    const i16 *b = base_uv[kind];
    u8 k, d = 0;
    for (k = 0; k < 6; k++) {
        i16 u = b[k * 2];
        i16 v = b[k * 2 + 1];
        if (kind >= K_TOWN) {
            /* 町: 家の壁はまっすぐ(幅も変えない。妻壁の位置と合わせる) */
        } else if (kind <= K_EXIT) {
            u = (i16)(((i32)u * w) >> 8);
            if (k == 3 || k == 4) {            /* 床は平らに(レールを敷く) */
                u += jit(12);
            } else if (k <= 1) {               /* 天井: 手掘りの凹凸 */
                u += jit(24);
                v = (i16)(((i32)v * h) >> 8) + jit(22);
            } else {                            /* 壁: 手掘りの凹凸(突き出しすぎない) */
                u += jit(28);
                v += jit(40);
            }
        } else if (k != 3 && k != 4) {          /* 屋外: 岸・高台・茂みの凹凸。床(水面・道)は平ら */
            u = (i16)(((i32)u * w) >> 8) + jit(40);
            v = (i16)(((i32)v * h) >> 8) + jit(30);
        }
        p[k * 2] = u;
        p[k * 2 + 1] = v;
    }
    switch (kind) {
    case K_CAVE:
        /* 天井は板張り(暗い茶)、壁は石炭(黒〜濃い灰、ときどき明るい岩)、床は土 */
        for (k = 0; k < 6; k++) {
            if (k == 3)       t[k] = (rnd() & 1) ? C_DIRT1 : C_DIRT0;
            else if (k == 0)  t[k] = C_DIRT0;
            else              t[k] = (rnd() < 100) ? C_ROCK0 : ((rnd() < 170) ? C_ROCK1 : C_ROCK2);
        }
        d = D_TIMBER;                           /* 坑木の枠組はリングごと */
        if (rnd() < 50) d |= D_LAMP_L;
        else if (rnd() < 60) d |= D_LAMP_R;
        if (rnd() < 40) d |= D_ROCK_L;
        if (rnd() < 40) d |= D_ROCK_R;
        break;
    case K_EXIT:                                /* 洞窟の出口(屋外のパレット: 暗い岩と土だけで描く) */
        for (k = 0; k < 6; k++) t[k] = (k == 3 || (rnd() & 1)) ? C_ROCK1 : C_ROCK0;
        d = D_TIMBER;
        break;
    case K_LAKE:
        /* 天井は暗い岩、岩壁は岩棚(上)と水際(下)、水面は波の縞 */
        t[0] = C_ROCK0;
        t[1] = t[5] = C_ROCK1;
        t[2] = t[4] = (rnd() & 1) ? C_ROCK1 : C_ROCK0;
        t[3] = (r & 1) ? C_WATER : C_WATER1;
        d = D_NORAIL;
        if (rnd() < 110) d |= D_LAMP_L;         /* 鍾乳石 */
        if (rnd() < 110) d |= D_LAMP_R;
        if (rnd() < 40) d |= D_ROCK_L;          /* 水面の岩 */
        else if (rnd() < 40) d |= D_ROCK_R;
        break;
    case K_PARK:                                /* 高架の軌道: 面は無く、レールと枕木と支柱だけ(下の地面まで見える) */
        for (k = 0; k < 6; k++) t[k] = 0;
        d = D_OPEN | D_EMPTY;
        if (!(r & 3)) d |= (r & 4) ? D_LAMP_L : D_LAMP_R;   /* 遠くのテント */
        if ((r & 3) == 2) d |= D_TIMBER;                       /* 観覧車 */
        if (r & 1) d |= D_ROCK_L | D_ROCK_R;                   /* 軌道わきの灯り */
        break;
    case K_HILL:                                /* 草の斜面を下る土の道 */
        t[0] = t[1] = t[5] = 0;
        t[2] = t[4] = C_GRASS;
        t[3] = C_ROCK1;
        d = D_OPEN;
        if (rnd() < 110) d |= D_LAMP_L;         /* 柵の杭 */
        if (rnd() < 110) d |= D_LAMP_R;
        if (rnd() < 60) d |= D_ROCK_L;          /* 茂み */
        if (rnd() < 60) d |= D_ROCK_R;
        break;
    case K_AIR:
        for (k = 0; k < 6; k++) t[k] = 0;
        d = D_OPEN | D_NORAIL | D_EMPTY;
        break;
    case K_PARKSIDE:                            /* 左 = 縞の柵(リングごとに赤と黄)、道、右 = 下る草の斜面 */
        t[0] = t[1] = 0;
        t[2] = C_GRASS;
        t[3] = C_ROCK1;
        t[4] = C_GRASS;
        t[5] = (r & 1) ? C_DIRT0 : C_DIRT1;
        d = D_OPEN;
        if (rnd() < 90) d |= D_ROCK_R;          /* 右の茂み */
        if (rnd() < 90) d |= D_LAMP_R;          /* 右の柵の杭 */
        break;
    case K_TOWN:
    case K_HOME: {
        /* 家並み: 左右それぞれ「家(1〜3 リング)」と「間(1〜2 リング)」を交互に。
         * 家の始まりのリングに手前の妻壁、終わりのリングに奥の妻壁の印を付ける */
        u8 sd;
        t[0] = 0;
        t[3] = C_ROCK1;
        d = D_OPEN;
        for (sd = 0; sd < 2; sd++) {
            u8 prev = hs_st[sd];
            if (!hs_cnt[sd]) {
                hs_st[sd] ^= 1;
                if (hs_st[sd]) {
                    hs_cnt[sd] = 1 + (rnd() % 3);
                    hs_col[sd] = (kind == K_TOWN && (rnd() & 1)) ? C_DIRT1 : C_ROCK2;
                } else {
                    hs_cnt[sd] = (rnd() < 100) ? 2 : 1;
                }
            }
            hs_cnt[sd]--;
            if (hs_st[sd] && !prev) d |= sd ? D_LAMP_R : D_LAMP_L;
            if (!hs_st[sd] && prev) d |= sd ? D_ROCK_R : D_ROCK_L;
            /* 右 = 面 1(屋根)・2(壁)、左 = 面 5(屋根)・4(壁) */
            t[sd ? 1 : 5] = hs_st[sd] ? C_DIRT0 : 0;
            t[sd ? 2 : 4] = hs_st[sd] ? hs_col[sd] : 0;
        }
        break;
    }
    default:                                    /* 森 */
        t[0] = 0;
        t[1] = C_DIRT0; t[5] = C_DIRT0;         /* 茂み(濃い緑) */
        t[2] = C_GRASS; t[4] = C_GRASS;         /* 下草 */
        t[3] = (rnd() < 90) ? C_WOOD : C_ROCK1; /* 土の道(ところどころ落ち葉) */
        d = D_OPEN;
        if (rnd() < 170) d |= D_LAMP_L;         /* 左の幹 */
        if (rnd() < 170) d |= D_LAMP_R;         /* 右の幹 */
        if (rnd() < 150) d |= D_TIMBER;         /* 頭上の葉 */
        break;
    }
    DECO[r] = d;
    KIND[r] = kind;
}

/* g_env_zones: 先頭 = 屋外のレールの色、以降 (この番号のリングまで, 地形の種類) の組。0xFF まで */
void envm_build(void)
{
    u8 r, n, kind;
    i16 w = 256, h = 256;
    const u8 *zones = g_env_zones;
    rng = g_env_seed;
    rail_col = *zones++;
    hs_st[0] = hs_st[1] = 0;
    hs_cnt[0] = hs_cnt[1] = 0;
    n = (u8)(rail_len / RSTEP + 2);
    if (n > MAX_RINGS) n = MAX_RINGS;
    for (r = 0; r < n; r++) {
        while (r >= zones[0]) zones += 2;
        kind = zones[1];
        /* 幅と高さはゆっくり変える(少し狭い所・広い所) */
        w += jit(14); if (w < 235) w = 235; if (w > 290) w = 290;
        h += jit(12); if (h < 240) h = 240; if (h > 285) h = 285;
        ring_build(r, kind, w, h);
    }
    /* 自宅: レールの終わりのリングに正面を置く */
    r = (u8)((rail_len - 1) / RSTEP);
    if (r < n && KIND[r] == K_HOME) DECO[r] |= D_TIMBER;
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
    ring_make(idx, RAIL_PT(m));
}

/* envm_draw が後回しにしたリング(ring_d0〜ring_d1)を作る(仮リングを作れなかったとき) */
static i8 ring_d0, ring_d1;
static void ring_late(void)
{
    i8 r;
    for (r = ring_d0; r <= ring_d1; r++) ring_calc(r);
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
 * リング面の 2D の形: 中心 + (u, v) * q(raster.s の rpt_a / rquad_a)
 *   RPT: scr[i] にリング idx の面の点 (u, v)。RQUAD: u0..u1 × v0..v1 の四角形を color で塗る
 *   (リング面の形は画面上でそのまま縮小されるので軸に沿った長方形になり、多角形の塗りを通さずに長方形で塗る)
 *   引数はスタックでなく変数で渡す(C の関数呼び出しの準備が重いため)
 * --------------------------------------------------------------------------- */
extern i16 g_pu, g_pv, g_qu0, g_qu1, g_qv0, g_qv1;
void rpt_a(u8 i, u8 idx);
void rquad_a(u8 idx);
#define RPT(i, idx, u, v)  do { g_pu = (u); g_pv = (v); rpt_a((i), (idx)); } while (0)
#define RQUAD(idx, u0, u1, v0, v1, color) \
    do { g_qu0 = (u0); g_qu1 = (u1); g_qv0 = (v0); g_qv1 = (v1); g_color = (color); rquad_a(idx); } while (0)

#define FLOOR_V (-333)

/* 区間(リング a → b)の床のレール 2 本と枕木 */
static void rails_draw(u8 a, u8 b)
{
    u8 side;
    if (!ring_c[a][2] || !ring_c[b][2]) return;
    if (DECO[ring_no[a < b ? a : b]] & D_NORAIL) return;
    /* 枕木(区間の手前のリングの面に、横長の板) */
    RQUAD(a, -180, 180, FLOOR_V, FLOOR_V + 14, C_WOOD);
    for (side = 0; side < 2; side++) {
        i16 u0 = side ? 102 : -128, u1 = side ? 128 : -102;
        RPT(0, a, u0, FLOOR_V);
        RPT(1, a, u1, FLOOR_V);
        RPT(2, b, u1, FLOOR_V);
        RPT(3, b, u0, FLOOR_V);
        raster(4, KIND[ring_no[a < b ? a : b]] == K_CAVE ? C_RAIL : rail_col);
    }
}

/* 床の石(三角形)。u0..u1 の幅、高さ ht */
static void rock_draw(u8 idx, i16 u0, i16 u1, i16 ht, u8 color)
{
    RPT(0, idx, u0, FLOOR_V);
    RPT(1, idx, u1, FLOOR_V);
    RPT(2, idx, (u0 + u1) >> 1, FLOOR_V + ht);
    raster(3, color);
}

static u8 view_back;              /* 1 = 後ろだけを見ている(家の奥の妻壁を描く) */

/* 家の妻壁(リング面の、道から奥への壁と三角の破風、窓 2 つ)。sgn = 1 右 / -1 左 */
static void house_end(u8 idx, i8 sgn, u8 col)
{
    i16 u0 = HOUSE_U0 * sgn, u1 = HOUSE_U1 * sgn, ur = RIDGE_U * sgn;
    RQUAD(idx, u0, u1, FLOOR_V, EAVE_V, col);
    RPT(0, idx, u0, EAVE_V);
    RPT(1, idx, u1, EAVE_V);
    RPT(2, idx, ur, RIDGE_V);
    raster(3, col);
    RQUAD(idx, 720 * sgn, 880 * sgn, 170, 380, C_ROCK0);
    RQUAD(idx, 1020 * sgn, 1180 * sgn, 170, 380, C_ROCK0);
}

/* 村の門(道の突き当たり): 太い柱 2 本、冠木と小さな屋根、村の名の札(14 番の色)。門の前で村人が出迎える */
static void home_draw(u8 idx)
{
    u8 i;
    RQUAD(idx, -760, -600, FLOOR_V, 800, C_WOOD);        /* 柱 */
    RQUAD(idx, 600, 760, FLOOR_V, 800, C_WOOD);
    RQUAD(idx, -900, 900, 700, 800, C_WOOD);             /* 冠木 */
    RPT(0, idx, -1000, 800);                              /* 屋根 */
    RPT(1, idx, 1000, 800);
    RPT(2, idx, 0, 1050);
    raster(3, C_DIRT0);
    RQUAD(idx, -260, 260, 520, 690, C_DIRT1);            /* 札 */
    for (i = 0; i < 4; i++) {                             /* 村人(白い服、14 番の顔) */
        i16 u = (i < 2) ? -560 + i * 150 : 420 + (i - 2) * 150;
        RQUAD(idx, u, u + 90, FLOOR_V, FLOOR_V + 230, C_WHITE);
        RQUAD(idx, u + 15, u + 75, FLOOR_V + 230, FLOOR_V + 300, C_DIRT1);
    }
}

/* 遊園地の出し物(u = 横位置、g = 地面の高さ)。柵の外から見るときは g を柵の上端にして、柵より上に見える部分だけを描く
 * (柵より下を描くと、手前の柵の前に出てしまうため) */
static void park_tent(u8 idx, i16 u, i16 g)
{
    RQUAD(idx, u, u + 600, g, g + 400, C_DIRT0);
    RPT(0, idx, u - 80, g + 400); RPT(1, idx, u + 680, g + 400); RPT(2, idx, u + 300, g + 850);
    raster(3, C_DIRT1);
    RQUAD(idx, u + 290, u + 310, g + 850, g + 1050, C_ROCK0);        /* 旗竿と三角旗 */
    RPT(0, idx, u + 310, g + 1050); RPT(1, idx, u + 310, g + 970); RPT(2, idx, u + 450, g + 1010);
    raster(3, C_WHITE);
}

/* メリーゴーラウンド: 白い胴、金色の柱、赤い円錐の屋根に白い帯、てっぺんの旗(uc = 中心)。
 * full = 0 なら屋根から上だけ(柵の向こうに屋根だけ見える) */
static void park_carousel(u8 idx, i16 uc, i16 g, u8 full)
{
    u8 i;
    if (full) {
        RQUAD(idx, uc - 500, uc + 500, g, g + 400, C_WHITE);
        for (i = 0; i < 4; i++) {
            i16 u = uc - 420 + i * 280;
            RQUAD(idx, u, u + 40, g + 400, g + 650, C_DIRT1);
        }
    }
    RPT(0, idx, uc - 650, g + 650); RPT(1, idx, uc + 650, g + 650); RPT(2, idx, uc, g + 1150);
    raster(3, C_DIRT0);
    RQUAD(idx, uc - 450, uc + 450, g + 650, g + 700, C_WHITE);
    RPT(0, idx, uc, g + 1150); RPT(1, idx, uc, g + 1250); RPT(2, idx, uc + 140, g + 1200);
    raster(3, C_DIRT1);
}

/* 観覧車: ハの字の脚、暗い輪(内を空の色で抜く)、赤いゴンドラ、軸(uc = 中心、g = 脚の下端、中心は g + 1550) */
static void park_wheel(u8 idx, i16 uc, i16 g)
{
    u8 j, k;
    i16 vc = g + 1550;
    RPT(0, idx, uc - 50, vc); RPT(1, idx, uc + 50, vc);
    RPT(2, idx, uc + 500, g); RPT(3, idx, uc - 500, g);
    raster(4, C_ROCK0);
    for (j = 0; j < 2; j++) {
        i16 r = j ? 880 : 1050;
        for (k = 0; k < 6; k++) RPT(k, idx, uc + fmul(r, SIN(k * 43)), vc + fmul(r, COS(k * 43)));
        raster(6, j ? C_SKY : C_ROCK0);
    }
    for (k = 0; k < 6; k++) {
        i16 u = uc + fmul(1050, SIN(k * 43)), v = vc + fmul(1050, COS(k * 43));
        RQUAD(idx, u - 100, u + 100, v - 200, v - 20, C_DIRT0);
    }
    RQUAD(idx, uc - 40, uc + 40, vc - 40, vc + 40, C_DIRT1);
}

/* 軌道をまたぐ電飾のアーチ: 暗い柱と梁、梁の下に三角旗の列、灯りが点滅する */
static void park_arch(u8 idx)
{
    u8 i, on = (u8)(g_tick >> 3) & 1;
    RQUAD(idx, -420, -360, FLOOR_V, FLOOR_V + 700, C_ROCK0);
    RQUAD(idx, 360, 420, FLOOR_V, FLOOR_V + 700, C_ROCK0);
    RQUAD(idx, -420, 420, FLOOR_V + 640, FLOOR_V + 720, C_ROCK0);
    for (i = 0; i < 6; i++) {
        i16 u = -330 + i * 120;
        RPT(0, idx, u, FLOOR_V + 640); RPT(1, idx, u + 100, FLOOR_V + 640); RPT(2, idx, u + 50, FLOOR_V + 560);
        raster(3, (i & 1) ? C_DIRT0 : C_WHITE);
        RQUAD(idx, u + 30, u + 70, FLOOR_V + 680, FLOOR_V + 710, ((i + on) & 1) ? C_DIRT1 : C_ROCK0);
    }
    for (i = 0; i < 4; i++) {
        i16 v = FLOOR_V + 100 + i * 150;
        u8 c = ((i + on) & 1) ? C_DIRT1 : C_ROCK0;
        RQUAD(idx, -405, -375, v, v + 30, c);
        RQUAD(idx, 375, 405, v, v + 30, c);
    }
}

/* リング idx の面の飾り
 *   洞窟: 坑木の枠組(柱 2 本と梁)、カンテラ、床の落石
 *   崖・川: 岩。森: 木の幹と頭上の葉の塊 */
static void deco_draw(u8 idx)
{
    u8 d, no;
    if (!ring_ok[idx] || !ring_c[idx][2]) return;
    no = ring_no[idx];
    d = DECO[no];
    switch (KIND[no]) {
    case K_CAVE:
    case K_EXIT:
        if (d & D_TIMBER) {
            RQUAD(idx, -330, -280, FLOOR_V, 300, C_WOOD);   /* 左の柱 */
            RQUAD(idx, 280, 330, FLOOR_V, 300, C_WOOD);     /* 右の柱 */
            RQUAD(idx, -345, 345, 282, 322, C_WOOD);        /* 梁 */
        }
        if (d & D_LAMP_L) RQUAD(idx, -275, -240, 120, 170, C_YELLOW);   /* カンテラ */
        if (d & D_LAMP_R) RQUAD(idx, 240, 275, 120, 170, C_YELLOW);
        if (d & D_ROCK_L) rock_draw(idx, -275, -170, 70, C_ROCK2);
        if (d & D_ROCK_R) rock_draw(idx, 170, 270, 60, C_ROCK2);
        break;
    case K_LAKE:
        /* 天井から垂れ下がる鍾乳石と、水面から顔を出した岩 */
        if (d & D_LAMP_L) {
            RPT(0, idx, -900, 1400); RPT(1, idx, -600, 1400); RPT(2, idx, -760, 760);
            raster(3, C_ROCK2);
        }
        if (d & D_LAMP_R) {
            RPT(0, idx, 520, 1400); RPT(1, idx, 820, 1400); RPT(2, idx, 690, 860);
            raster(3, C_ROCK2);
        }
        if (d & D_ROCK_L) rock_draw(idx, -760, -500, 150, C_ROCK0);
        if (d & D_ROCK_R) rock_draw(idx, 480, 720, 130, C_ROCK0);
        break;
    case K_FOREST: {
        i16 o = (no & 1) ? 90 : 0;                        /* 幹の位置をリングごとにずらす */
        if (d & D_TIMBER) {                               /* 頭上の葉の塊 */
            RQUAD(idx, -760, -150 - o, 820, 1160, C_DIRT0);
            RQUAD(idx, 170 + o, 760, 860, 1200, C_DIRT0);
            RQUAD(idx, -520, -260, 1000, 1140, C_DIRT1);
        }
        if (d & D_LAMP_L) RQUAD(idx, -560 + o, -470 + o, FLOOR_V, 900, C_WOOD);
        if (d & D_LAMP_R) RQUAD(idx, 490 - o, 580 - o, FLOOR_V, 940, C_WOOD);
        break;
    }
    case K_PARK:
        /* 高架の支柱(軌道の下へ)、遠くのテント(赤い幕と三角の屋根)、観覧車(暗い輪)、軌道わきの灯り */
        RQUAD(idx, -215, -185, FLOOR_V - 1300, FLOOR_V, C_ROCK0);
        RQUAD(idx, 185, 215, FLOOR_V - 1300, FLOOR_V, C_ROCK0);
        if (d & (D_LAMP_L | D_LAMP_R)) park_tent(idx, (d & D_LAMP_L) ? -1500 : 900, FLOOR_V - 1300);
        if (no % 8 == 6) park_carousel(idx, -2000, FLOOR_V - 1300, 1);
        if (no % 6 == 3) park_arch(idx);
        /* 観覧車は遠く(約 14m より先)に見えるときだけ(近いと画面からはみ出す) */
        if ((d & D_TIMBER) && (u16)ring_c[idx][2] < 2400) park_wheel(idx, 2600, FLOOR_V - 1300);
        if (d & D_ROCK_L) RQUAD(idx, -260, -230, FLOOR_V, FLOOR_V + 90, C_DIRT1);
        if (d & D_ROCK_R) RQUAD(idx, 230, 260, FLOOR_V, FLOOR_V + 90, C_DIRT1);
        break;
    case K_PARKSIDE: {
        /* 柵の上の灯り(点滅)、柵の上に見える出し物(柵の上端より上だけ)、右の茂みと杭 */
        u8 on = (u8)(g_tick >> 3) & 1;
        RQUAD(idx, -545, -505, 520, 580, ((no + on) & 1) ? C_DIRT1 : C_ROCK0);
        if (no % 5 == 1) park_tent(idx, -1600, 560);
        if (no % 10 == 4) park_carousel(idx, -2300, 560 - 650, 0);
        if (no % 10 == 8 && (u16)ring_c[idx][2] < 2400) park_wheel(idx, -3000, 560);
        if (d & D_LAMP_R) RQUAD(idx, 580, 620, FLOOR_V - 60, FLOOR_V + 120, C_WOOD);
        if (d & D_ROCK_R) rock_draw(idx, 650, 900, 130, C_ROCK0);
        break;
    }
    case K_HILL:
        if (d & D_LAMP_L) RQUAD(idx, -620, -580, FLOOR_V, FLOOR_V + 160, C_WOOD);
        if (d & D_LAMP_R) RQUAD(idx, 580, 620, FLOOR_V, FLOOR_V + 160, C_WOOD);
        if (d & D_ROCK_L) rock_draw(idx, -860, -600, 160, C_ROCK0);
        if (d & D_ROCK_R) rock_draw(idx, 600, 860, 140, C_ROCK0);
        break;
    case K_TOWN:
    case K_HOME:
        /* 前を見ているときは家の手前の妻壁、後ろを見ているときは奥の妻壁(見えない側は道側の壁に隠れる) */
        if (!view_back) {
            if (d & D_LAMP_R) house_end(idx, 1, TONE[no * 6 + 2]);
            if (d & D_LAMP_L) house_end(idx, -1, TONE[no * 6 + 4]);
        } else if (no) {
            if (d & D_ROCK_R) house_end(idx, 1, TONE[(no - 1) * 6 + 2]);
            if (d & D_ROCK_L) house_end(idx, -1, TONE[(no - 1) * 6 + 4]);
        }
        if (d & D_TIMBER) home_draw(idx);
        break;
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
    u8 kind = KIND[na];
    for (k = 0; k < 6; k++) {
        c = t[k];
        if (kind == K_EXIT || kind == K_LAKE) {
            /* 奥は暗い岩(水面はそのまま) */
            if (dist >= 2 && c != C_WATER && c != C_WATER1) c = C_ROCK0;
        } else if (kind == K_CAVE && dist >= 2) {
            if (c == C_ROCK2) c = C_ROCK1;
            else if (c == C_ROCK1 || c == C_DIRT0) c = C_ROCK0;
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
    u8 k, k1, fa, fb, ma, mb, na;
    const i16 *sa, *sb;
    V3 q[4];
    if (!ring_ok[a] || !ring_ok[b]) return;
    fa = ring_front[a];
    fb = ring_front[b];
    if (!fa && !fb) return;
    na = ring_no[a < b ? a : b];
    if (DECO[na] & D_EMPTY) {
        /* 面の無い区間(遊園地の高架・空中)。高架はレールだけが見えるので遠くまで描く */
        if (KIND[na] == K_PARK) rails_draw(a, b);
        return;
    }
    set_band_col(na, dist);
    if (fa == 0x3F && fb == 0x3F) {
        band_fast(a, b);
    } else {
        sa = ring_s[a];
        sb = ring_s[b];
        for (k = 0; k < 6; k++) {
            k1 = (k == 5) ? 0 : k + 1;
            ma = (u8)((fa >> k) & 1) + (u8)((fa >> k1) & 1);
            mb = (u8)((fb >> k) & 1) + (u8)((fb >> k1) & 1);
            if (!band_col[k]) continue;
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

/* 遠くのリングを断面 1 枚で近似してよいか(6 頂点とも手前で、水面の見える地底湖ではない) */
static u8 far_ok(i8 r)
{
    u8 idx = (u8)(r + NB);
    return cap_ok(r) && KIND[ring_no[idx]] != K_LAKE;
}

/* リング r〜r+2 の 2 区間を 1 つの帯で描いてよいか: 3 本とも森か地底湖(断面の形がなめらかに変わる地形)。
 * 町の家並みや遊園地の建物は、リングごとに形が変わるので、途中のリングを飛ばすと遠くの屋根や建物の形が欠ける */
static u8 merge_ok(i8 r)
{
    u8 i, kd;
    for (i = 0; i < 3; i++) {
        kd = KIND[ring_no[(u8)(r + NB + i)]];
        if (kd != K_FOREST && kd != K_LAKE) return 0;
    }
    return 1;
}

/* 奥の開口部を塞ぐ色: 坑道は闇(黒)、地底湖は岩壁と同じ暗い岩の色(黒い穴に見えないように) */
static u8 cap_col(i8 r)
{
    return KIND[ring_no[(u8)(r + NB)]] == K_LAKE ? C_ROCK0 : C_BLACK;
}

static void hex_draw(i8 r, u8 color)
{
    g_color = color;
    hex_fast((u8)(r + NB));
}

/* 空と地面(地平線の高さで 2 色に塗り分ける)。far = 一番遠くに見える地形 */
static void sky_draw(u8 far)
{
    i16 yh = SCR_CY;
    u8 row;
    if (cam_pcos > 16) yh += muldiv(PROJ_F, cam_psin, cam_pcos);
    else if (cam_psin < 0) yh = -1;
    row = (yh < 0) ? 0 : (yh >= 48 * 16) ? 48 : (u8)(yh >> 4);
    g_rh = row;
    buf_fill_rows(C_SKY, 0);
    g_rh = 48 - row;
    buf_fill_rows(far == K_FOREST ? C_DIRT0 : far >= K_PARK ? C_GRASS : C_ROCK1, row);
}

void envm_draw(u16 s, u16 look16)
{
    u8 look = (u8)look16;
    /* look: 視線の相対ヨー(0 = 前、128 = 後ろ) */
    u8 fwd = !(look > 80 && look < 176);
    u8 back = (look > 48 && look < 208);
    i8 r, k, rn;
    u8 capped = 1;

    ring_m0 = (s >> 6) - (s >> 6) % RSTEP;
    view_back = back && !fwd;
    for (r = 0; r <= NR; r++) ring_ok[r] = 0;
    g_ring_s = &ring_s[0][0];
    eslot_clear();
    /* 前だけ・後ろだけを見ているときは、カメラを含む区間を仮リングで描くので、その向こう側のリング(d0〜d1)は
     * 仮リングを作れないときにしか使わない。計算はそのときまで後回しにする(空が見えるかの判定はリングの番号だけで決まる) */
    {
        i8 d0 = 1, d1 = 0;
        if (fwd && !back) {
            d0 = 0;
            d1 = ((u16)(ring_m0 + RSTEP) * 64 > s + VR_D + 6) ? 0 : 1;
        } else if (back && !fwd && s >= VR_D + 6) {
            d0 = ((u16)ring_m0 * 64 + 6 < s - VR_D) ? 1 : 0;
            d1 = 1;
        }
        ring_d0 = d0;
        ring_d1 = d1;
    }
    {
        u8 open = 0, far = K_CAVE;
        for (r = back ? -NB : 0; r <= (fwd ? NF : 1); r++) {
            k = (i8)(r + NB);
            if (r >= ring_d0 && r <= ring_d1) {
                i16 m = ring_meter(r);
                if (m < 0 || m >= (i16)rail_len) continue;
                ring_no[k] = (u8)(m / RSTEP);
            } else {
                ring_calc(r);
                if (!ring_ok[k]) continue;
            }
            {
                u8 no = ring_no[k];
                if (DECO[no] & D_OPEN) { open = 1; far = KIND[no]; }
            }
        }
        if (open) {
            /* 屋外が見える: 空と地面を塗ってから描く(奥の開口部は塞がない) */
            sky_draw(far);
            capped = 0;
        } else {
            /* トンネルの内側からは壁が視界を全部覆うので、奥の開口部を黒く塞げば
             * バッファの全消去は要らない。レールの端で塞げないときだけ全消去する */
            if (fwd && !cap_ok(NF)) capped = 0;
            if (back && !cap_ok(-NB)) capped = 0;
            if (!capped) buf_fill(C_BLACK);
        }
    }

    if (fwd && !back) {
        u16 sv = s + VR_D;
        i8 kn = FAR_K;
        /* 遠方(12m〜): リングの断面を一番暗い岩の色で手前から奥へ重ねる(遠いリングほど画面の内側)。
         * どれかのリングが画面外へ回り込んでいれば全部を普通に描く */
        if (capped) {
            for (k = FAR_K; k < NF; k++) if (!far_ok(k)) break;
            if (k == NF) {
                for (k = FAR_K; k < NF; k++) hex_draw(k, C_ROCK0);
            } else {
                kn = NF;
            }
            hex_draw(NF, cap_col(NF));
        } else {
            kn = NF;
        }
        /* 手前: 6 面ずつ。カメラを含む区間は「0.6m 先の仮リング → その先の最初のリング」で置き換える
         * (仮リングの頂点は全部画面外に投影されるので、ニアクリップ無しで画面の縁まで覆える) */
        rn = ((u16)(ring_m0 + RSTEP) * 64 > sv + 6) ? 1 : 2;
        for (k = kn - 1; k >= rn; k--) {
            if (k == NF - 1 && kn == NF && k - 1 >= rn && merge_ok(NF - 2)) {
                /* 遠い 2 区間(12〜20m)は 1 つの帯で描く(途中の 16m のリングは飾りの位置にだけ使う) */
                band_draw((u8)(NB + NF - 2), (u8)(NB + NF), 3);
                frame_draw(NF);
                frame_draw(NF - 1);
                k--;
                continue;
            }
            seg_draw(k); frame_draw(k + 1);
        }
        if (vring_make(sv)) {
            band_draw(VR, (u8)(rn + NB), 0);
            frame_draw(rn);
        } else {
            ring_late();
            for (k = rn - 1; k >= 0; k--) { seg_draw(k); frame_draw(k + 1); }
        }
        return;
    }
    if (back && !fwd && s >= VR_D + 6) {
        u16 sv = s - VR_D;
        i8 kn = -FAR_K;
        if (capped) {
            for (k = -FAR_K; k > -NB; k--) if (!far_ok(k)) break;
            if (k == -NB) {
                for (k = -FAR_K; k > -NB; k--) hex_draw(k, C_ROCK0);
            } else {
                kn = -NB;
            }
            hex_draw(-NB, cap_col(-NB));
        } else {
            kn = -NB;
        }
        rn = ((u16)ring_m0 * 64 + 6 < sv) ? 0 : -1;
        for (k = kn; k < rn; k++) {
            if (k == -NB && kn == -NB && k + 2 <= rn && merge_ok(-NB)) {
                band_draw(0, 2, 3);                  /* 遠い 2 区間は 1 つの帯で(前と同じ) */
                frame_draw(-NB);
                frame_draw(-NB + 1);
                k++;
                continue;
            }
            seg_draw(k); frame_draw(k);
        }
        if (vring_make(sv)) {
            band_draw((u8)(rn + NB), VR, 0);
            frame_draw(rn);
        } else {
            ring_late();
            for (k = rn; k <= 0; k++) { seg_draw(k); frame_draw(k); }
        }
        return;
    }
    if (capped) {
        if (fwd) hex_draw(NF, cap_col(NF));
        if (back) hex_draw(-NB, cap_col(-NB));
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
