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
 *   鉄橋: 床の無い鉄橋(枕木の隙間から下の海が見える)。上弦材と遠くの線路を面で塗り、トラスの斜材・縦材・梁・横桁は細い四角形で描く。
 *         まわりは海(地平線の下の塗り)。海は目の 12m 下にあるものとして白波を流す(近くの枕木より遅く流れ、下の方にあると分かる)。
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

/* 地形モジュールはステージごと(Makefile が ENV_S1〜ENV_S6 を付けて env.c をコンパイルし、ステージの始めに読み込む)。
 * その面で使う地形の処理だけを入れて、空いた所を面ごとの景色の変化に使う。ほかの面の地形を zones に書くと、何も描かない(空中と同じ) */
#if defined(ENV_S1)
#define HAS_EXIT   1       /* 坑道の入口(ステージ 2 へ) */
#define HAS_PARK   1       /* 遊園地の高架・丘・柵沿い(空中は種類の分からない地形と同じ処理) */
#elif defined(ENV_S2)
#define HAS_EXIT   1
#define HAS_LAKE   1
#elif defined(ENV_S3)
#define HAS_FOREST 1
#elif defined(ENV_S4)
#define HAS_EXIT   1       /* 岬のトンネル */
#define HAS_BRIDGE 1
#elif defined(ENV_S5)
#define HAS_TOWN   1
#elif defined(ENV_S6)
#define HAS_HOME   1
#else
#error "ENV_S1〜ENV_S6 のどれかを付けてコンパイルする"
#endif
#ifndef HAS_CAVE
#define HAS_CAVE   0       /* 炭鉱の坑道(今はどの面も使っていない) */
#endif
#ifndef HAS_EXIT
#define HAS_EXIT   0
#endif
#ifndef HAS_LAKE
#define HAS_LAKE   0
#endif
#ifndef HAS_FOREST
#define HAS_FOREST 0
#endif
#ifndef HAS_TOWN
#define HAS_TOWN   0
#endif
#ifndef HAS_HOME
#define HAS_HOME   0
#endif
#ifndef HAS_PARK
#define HAS_PARK   0
#endif
#ifndef HAS_BRIDGE
#define HAS_BRIDGE 0
#endif

#define RSTEP 4            /* リングの間隔[m] */
#define NF    5            /* 前方の区間数(20m) */
#define NB    5            /* 後方の区間数 */
#define NR    (NF + NB + 1)
#define VR    NR           /* 仮リング(カメラの 0.6m 先/後ろ)の番号 */
#define TURN_K 3           /* 振り向きの最中の見える区間の数(12m) */
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
 *   町: 9 = 戸、10 = 窓・レール、11 = 道、12 = 壁、13 = 屋根、14 = 別の色の壁(自宅前では自宅の壁)、15 = 空
 *   鉄橋(入口のトンネルも): 9 = 枕木、10 = レール・暗い岩、11 = 遠くの線路・岩・向こう岸、12 = 海、13 = 鉄骨、14 = 白波・地平線のもや、15 = 空 */
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
static u8  vr_f;                 /* 仮リングの位置: 区間(リング ring_no[VR] からその次まで)の中の 0..255 */

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
    /* 村   */  {-3000, 100, 3000, 100,  560, -150,  480, -345, -480, -345, -560, -150 },
    /* 遊園地 */{ -400, 200,  400, 200,  230, -333,  200, -333, -200, -333, -230, -333 },
    /* 丘   */  { -900, 400,  900, 400,  900, -250,  380, -345, -380, -345, -900, -250 },
    /* 空中 */  { -400, 200,  400, 200,  230, -333,  200, -333, -200, -333, -230, -333 },
    /* 柵沿い */{ -520, 560, 1000, 200, 1000, -700,  400, -345, -400, -345, -520, -345 },
    /* 鉄橋 */  { -520, 900,  520, 900,  520, 840,  200, -345, -200, -345, -520, 840 },
    /* 空き地 */{-1700, 700, 1700, 700, 1400,  -60,  420, -345, -420, -345,-1400,  -60 },
    /* 小川 */  { -900,1400,  900,1400, 1500, -430,  240, -345, -240, -345,-1500, -430 },
    /* 倒木 */  { -330, 330,  330, 330,  400,    0,  300, -345, -300, -345, -400,    0 },
    /* 祠   */  { -760,1400,  760,1400,  720,  250,  420, -345, -420, -345, -720,  250 },
    /* 屋台 */  { -950, 900,  950, 900,  650,  520,  650, -345, -650, -345, -650,  520 },
    /* 運河 */  { -480,-170,  480,-170,  480, -345,  200, -345, -200, -345, -480, -345 },
    /* 広場 */  {-2300,1000, 2300,1000, 2200, -345,  650, -345, -650, -345,-2200, -345 },
    /* 門   */  { -380, 560,  380, 560,  450,  300,  450, -345, -450, -345, -450,  300 },
    /* 田   */  {-3200,-430, 3200,-430,  620, -430,  470, -345, -470, -345, -620, -430 },
    /* 水車 */  {-3200,-430, 3200,-430,  620, -430,  470, -345, -470, -345, -620, -430 },
    /* 滝   */  {-1300,1400, 1300,1400, 1150,  420, 1000, -380,-1000, -380,-1150,  420 },
    /* 結晶 */  { -900, 900,  900, 900,  850,  300,  750, -380, -750, -380, -850,  300 },
    /* 残骸 */  {-1300,1400, 1300,1400, 1150,  420, 1000, -380,-1000, -380,-1150,  420 },
    /* 岩門 */  { -600, 600,  600, 600,  700,  150,  620, -380, -620, -380, -700,  150 },
    /* 屋台道 */{ -900, 400,  900, 400,  900, -250,  380, -345, -380, -345, -900, -250 },
    /* 宙返り */{ -520, 560, 1000, 200, 1000, -700,  400, -345, -400, -345, -520, -345 },
    /* 城   */  { -520, 560, 1000, 200, 1000, -700,  400, -345, -400, -345, -520, -345 },
    /* アーチ */{ -520, 900,  520, 900,  520, 840,  200, -345, -200, -345, -520, 840 },   /* 上弦材の高さは ring_build が径間の中の位置で決める */
};
/* 鉄橋: トラスの面(u = ±BR_U)、上弦材の上端と下端(断面の面 1・5 を細い帯にして上弦材にする)。
 * 床の無い橋(枕木とレールの隙間から、ずっと下の海が見える)。断面の床(面 3)は線路の幅 ±200 で、12m より先だけ塗る */
#define BR_U    520
#define BR_TOP  900
#define BR_CHD  840
#define BR_GIR  FLOOR_V
#define HOUSE_U0  650      /* 家の壁(道に面した面)の横位置 */
#define HOUSE_U1 1250      /* 家の奥行きの端 */
#define EAVE_V    520      /* 軒の高さ */
#define RIDGE_U   950      /* 棟の横位置と高さ */
#define RIDGE_V   900
#if HAS_TOWN
static u8 hs_st[2], hs_cnt[2], hs_col[2];   /* 家並み: 左右それぞれの今の状態(1 = 家)・残りリング数・壁の色 */
#endif
/* 水面のある地底湖の地形(遠くを断面 1 枚で近似しない、奥の開口部を暗い岩で塞ぐ、奥を暗くしても水面はそのまま) */
#define LAKEISH(k) ((k) == K_LAKE || ((k) >= K_FALLS && (k) <= K_ARCH))
#if HAS_BRIDGE
#define AB_SPAN 7                 /* アーチ橋の径間のリング数(28m) */
static u8 ab_j;                   /* アーチ橋の径間の中の位置(ring_build) */
#define BRIDGEISH(k) ((k) == K_BRIDGE || (k) == K_ARCHBR)
#endif
static u8 rail_col;
static u8 nk;                     /* 地形を作っているリングの次のリングの種類(envm_build) */

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
        if ((kind >= K_TOWN && kind <= K_BRIDGE) || (kind >= K_FAIR && kind <= K_MILL) || kind >= K_STALLS) {
            /* 町: 家の壁はまっすぐ(幅も変えない。妻壁の位置と合わせる)。人の作った所(村・遊園地・鉄橋・町の中・田んぼなど)も凹凸を付けない */
        } else if (kind <= K_EXIT || kind == K_HOLLOW) {   /* 坑道と倒木の中: 手掘り・木の洞の凹凸 */
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
#if HAS_CAVE
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
#endif
#if HAS_EXIT
    case K_EXIT:                                /* 洞窟の出口(屋外のパレット: 暗い岩と土だけで描く) */
        for (k = 0; k < 6; k++) t[k] = (k == 3 || (rnd() & 1)) ? C_ROCK1 : C_ROCK0;
        d = D_TIMBER;
        break;
#endif
#if HAS_LAKE
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
    case K_FALLS:                               /* 滝: 湖と同じ色で、左の岩壁に滝 */
    case K_WRECK:                               /* 残骸: 湖と同じ色で、最初のリングにトロッコ、ほかは浮いた板 */
        t[0] = C_ROCK0;
        t[1] = t[5] = C_ROCK1;
        t[2] = t[4] = (rnd() & 1) ? C_ROCK1 : C_ROCK0;
        t[3] = (r & 1) ? C_WATER : C_WATER1;
        d = D_NORAIL;
        if (kind == K_FALLS) d |= D_TIMBER;
        else if (KIND[r - 1] != K_WRECK) d |= D_TIMBER;
        else d |= (r & 1) ? D_ROCK_L : D_ROCK_R;
        break;
    case K_CRYSTAL:                             /* 結晶の洞: 暗い岩に、結晶の光で明るい水面 */
        t[0] = C_ROCK0;
        t[1] = t[5] = C_ROCK0;
        t[2] = t[4] = C_ROCK1;
        t[3] = C_WATER1;
        d = D_NORAIL;
        break;
    case K_ARCH:                                /* 岩の門: 暗い岩のトンネルに、密な鍾乳石 */
        t[0] = t[1] = t[5] = C_ROCK0;
        t[2] = t[4] = C_ROCK1;
        t[3] = (r & 1) ? C_WATER : C_WATER1;
        d = D_NORAIL | D_LAMP_L | D_LAMP_R;
        break;
#endif
#if HAS_PARK
    case K_PARK:                              /* 高架の軌道: 面は無く、レールと枕木と支柱だけ(下の地面まで見える) */
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
    case K_STALLS:                              /* 屋台の並ぶ道: 丘の道と同じ草の斜面 */
        t[0] = t[1] = t[5] = 0;
        t[2] = t[4] = C_GRASS;
        t[3] = C_ROCK1;
        d = D_OPEN;
        break;
    case K_LOOP:
    case K_CASTLE:                              /* 柵沿いと同じ(左 = 縞の柵)。区間の 2 本目のリングに大きな出し物 */
        t[0] = t[1] = 0;
        t[2] = C_GRASS;
        t[3] = C_ROCK1;
        t[4] = C_GRASS;
        t[5] = (r & 1) ? C_DIRT0 : C_DIRT1;
        d = D_OPEN;
        if (r >= 2 && KIND[r - 1] == kind && KIND[r - 2] != kind) d |= D_TIMBER;
        break;
#endif
#if HAS_TOWN
    case K_TOWN:
    case K_FAIR: {
        /* 家並み: 左右それぞれ「家(1〜3 リング)」と「間(1〜2 リング)」を交互に。屋台の通りは家を切らさない。
         * 次のリングから家並みでない地形(運河・広場・門)なら、このリングで家を終える。
         * 家の始まりのリングに手前の妻壁、終わりのリングに奥の妻壁の印を付ける */
        u8 sd;
        t[0] = 0;
        t[3] = C_ROCK1;
        d = D_OPEN;
        for (sd = 0; sd < 2; sd++) {
            u8 prev = hs_st[sd];
            if (nk != K_TOWN && nk != K_FAIR) {
                hs_st[sd] = 0;
                hs_cnt[sd] = 0;
            } else if (kind == K_FAIR) {
                if (!hs_st[sd]) { hs_st[sd] = 1; hs_col[sd] = (rnd() & 1) ? C_DIRT1 : C_ROCK2; }
                hs_cnt[sd] = 0;
            } else {
                if (!hs_cnt[sd]) {
                    hs_st[sd] ^= 1;
                    if (hs_st[sd]) {
                        hs_cnt[sd] = 1 + (rnd() % 3);
                        hs_col[sd] = (rnd() & 1) ? C_DIRT1 : C_ROCK2;
                    } else {
                        hs_cnt[sd] = (rnd() < 100) ? 2 : 1;
                    }
                }
                hs_cnt[sd]--;
            }
            if (hs_st[sd] && !prev) d |= sd ? D_LAMP_R : D_LAMP_L;
            if (!hs_st[sd] && prev) d |= sd ? D_ROCK_R : D_ROCK_L;
            /* 右 = 面 1(屋根)・2(壁)、左 = 面 5(屋根)・4(壁) */
            t[sd ? 1 : 5] = hs_st[sd] ? C_DIRT0 : 0;
            t[sd ? 2 : 4] = hs_st[sd] ? hs_col[sd] : 0;
        }
        break;
    }
    case K_CANAL:                               /* 石の橋: 欄干(面 1・5)と路面。欄干の外は空と水面(sky_draw) */
        t[0] = 0;
        t[1] = t[5] = C_DIRT1;
        t[2] = t[3] = t[4] = C_ROCK1;
        d = D_OPEN;
        break;
    case K_PLAZA:                               /* 広場: 遠くの建物の正面(面 1・5)と石畳(リングごとに色を替える) */
        t[0] = 0;
        t[1] = t[5] = (r & 2) ? C_DIRT1 : C_ROCK2;
        t[2] = t[4] = (r & 1) ? C_ROCK1 : C_ROCK2;
        t[3] = C_ROCK1;
        d = D_OPEN;
        if (r >= 3 && KIND[r - 1] == K_PLAZA && KIND[r - 2] == K_PLAZA && KIND[r - 3] != K_PLAZA) d |= D_TIMBER;   /* 噴水(広場の 3 本目) */
        break;
    case K_GATE:                                /* 門の中: 青灰色の石と暗い天井。奥ほど暗い(set_band_col) */
        t[0] = C_ROCK0;
        t[1] = t[5] = C_DIRT1;
        t[2] = t[4] = (r & 1) ? C_DIRT1 : C_ROCK0;
        t[3] = C_ROCK1;
        d = 0;
        if (r && KIND[r - 1] != K_GATE) d |= D_LAMP_L;     /* 入口の門塔(出口は envm_build が後で印を付ける) */
        break;
#endif
#if HAS_HOME
    case K_HOME:                                /* 村へ続く道: 両側の草の縁に柵の杭、その外は畑の緩い斜面、ときどき茅葺きの農家 */
        t[0] = 0;
        t[1] = t[5] = C_GRASS;
        t[2] = t[4] = C_GRASS;                  /* 縁を木の色で塗ると、近くで大きな土手に見えた */
        t[3] = C_ROCK1;
        d = D_OPEN;
        if (rnd() < 70) d |= D_LAMP_L;          /* 農家 */
        else if (rnd() < 70) d |= D_LAMP_R;
        break;
    case K_PADDY:
    case K_MILL:                                /* 田んぼ: 水を張った田(固定色の水色)と、道の両側のあぜの斜面 */
        t[0] = 0;
        t[1] = t[5] = C_CYAN;
        t[2] = t[4] = C_GRASS;
        t[3] = C_ROCK1;
        d = D_OPEN;
        if (kind == K_PADDY && rnd() < 50) d |= (rnd() & 1) ? D_LAMP_L : D_LAMP_R;   /* かかし */
        if (kind == K_MILL && r >= 2 && KIND[r - 1] == K_MILL && KIND[r - 2] != K_MILL) d |= D_TIMBER;   /* 水車(2 本目) */
        break;
#endif
#if HAS_BRIDGE
    case K_ARCHBR:
        /* アーチ橋: 径間(AB_SPAN リング)の中の位置 j で、アーチ(面 1・5 の細い帯)の高さを放物線にする(両端は床の近く、中央が一番高い) */
        if (!r || KIND[r - 1] != K_ARCHBR) ab_j = 0;
        {
            i16 h = -250 + (i16)(ab_j * (AB_SPAN - ab_j)) * 139;
            p[1] = p[3] = h + 70;
            p[5] = p[11] = h;
        }
        if (++ab_j == AB_SPAN) ab_j = 0;
        /* fall through */
    case K_BRIDGE:                              /* 線路(面 3。12m より先だけ)と、左右の上弦材(面 1・5 の細い帯)だけを塗る */
        t[0] = t[2] = t[4] = 0;
        t[1] = t[5] = C_DIRT0;
        t[3] = C_ROCK1;
        d = D_OPEN;
        break;
#endif
#if HAS_FOREST
    case K_FOREST:
        t[0] = 0;
        t[1] = C_DIRT0; t[5] = C_DIRT0;         /* 茂み(濃い緑) */
        t[2] = C_GRASS; t[4] = C_GRASS;         /* 下草 */
        t[3] = (rnd() < 90) ? C_WOOD : C_ROCK1; /* 土の道(ところどころ落ち葉) */
        d = D_OPEN;
        if (rnd() < 170) d |= D_LAMP_L;         /* 左の幹 */
        if (rnd() < 170) d |= D_LAMP_R;         /* 右の幹 */
        if (rnd() < 150) d |= D_TIMBER;         /* 頭上の葉 */
        if (rnd() < 30) d |= D_ROCK_L;          /* 頭上を横切る倒木 */
        break;
    case K_GLADE:                               /* 空き地: 遠くの低い木立(濃い緑)と、日の当たる明るい草地 */
        t[0] = 0;
        t[1] = t[5] = C_DIRT0;
        t[2] = t[4] = C_DIRT1;
        t[3] = C_ROCK1;
        d = D_OPEN;
        if (rnd() < 150) d |= D_LAMP_L;         /* 花 */
        if (rnd() < 150) d |= D_LAMP_R;
        if (rnd() < 70) d |= D_ROCK_L;          /* 切り株 */
        if (!(r % 3)) d |= D_TIMBER;            /* 遠くの一本木(左右交互) */
        break;
    case K_STREAM:                              /* 小川: 足元は丸太の橋、左右は明るい水面(固定色の水色。空の色だと岸が宙に浮いて見えた)、その先は岸の茂み */
        t[0] = 0;
        t[1] = t[5] = C_DIRT0;
        t[2] = t[4] = C_CYAN;
        t[3] = C_WOOD;
        d = D_OPEN;
        break;
    case K_HOLLOW:                              /* 倒木の中: 木の洞(幹の色と影の木目)。奥ほど暗い(set_band_col) */
        for (k = 0; k < 6; k++) t[k] = (k == 3) ? C_ROCK1 : ((rnd() & 1) ? C_WOOD : C_ROCK0);
        d = 0;
        if (r && KIND[r - 1] != K_HOLLOW) d |= D_LAMP_L;   /* 入口の切り口(出口は envm_build が後で印を付ける) */
        break;
    case K_SHRINE:                              /* 祠のある所: 森と同じ色で、左は木を立てずに祠 */
        t[0] = 0;
        t[1] = t[5] = C_DIRT0;
        t[2] = t[4] = C_GRASS;
        t[3] = C_ROCK1;
        d = D_OPEN;
        break;
#endif
    default:                                    /* 空中(とこのモジュールに入っていない地形): 何も描かない */
        for (k = 0; k < 6; k++) t[k] = 0;
        d = D_OPEN | D_NORAIL | D_EMPTY;
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
#if HAS_TOWN
    hs_st[0] = hs_st[1] = 0;
    hs_cnt[0] = hs_cnt[1] = 0;
#endif
    n = (u8)(rail_len / RSTEP + 2);
    if (n > MAX_RINGS) n = MAX_RINGS;
    for (r = 0; r < n; r++) {
        while (r >= zones[0]) zones += 2;
        kind = zones[1];
        nk = (r + 1 >= zones[0]) ? zones[3] : kind;
        /* 幅と高さはゆっくり変える(少し狭い所・広い所) */
        w += jit(14); if (w < 235) w = 235; if (w > 290) w = 290;
        h += jit(12); if (h < 240) h = 240; if (h > 285) h = 285;
        ring_build(r, kind, w, h);
    }
#if HAS_FOREST
    /* 倒木のトンネルの出口: 最後のリングは道だけを塗り(次の森のリングへ広がる木の面を描かない)、切り口の印を付ける */
    for (r = 1; r < n; r++) {
        if (KIND[r] == K_HOLLOW && (r + 1 >= n || KIND[r + 1] != K_HOLLOW)) {
            u8 *t = &TONE[r * 6];
            t[0] = t[1] = t[2] = t[4] = t[5] = 0;
            DECO[r] |= D_LAMP_R | D_OPEN;
        }
    }
#endif
#if HAS_TOWN
    /* 町の地形の終わりのリング: 門は道だけを塗って出口の門塔の印、運河と広場は左右の面を塗らない
     * (区間の面の色は手前のリングの表から引くので、次の家並みへ斜めに伸びる面になる) */
    for (r = 1; r < n; r++) {
        u8 kd = KIND[r];
        if ((kd == K_GATE || kd == K_CANAL || kd == K_PLAZA) && (r + 1 >= n || KIND[r + 1] != kd)) {
            u8 *t = &TONE[r * 6];
            t[1] = t[5] = 0;
            if (kd == K_GATE) {
                t[0] = t[2] = t[4] = 0;
                DECO[r] |= D_LAMP_R | D_OPEN;
            }
        }
    }
#endif
#if HAS_HOME
    /* 自宅: レールの終わりのリングに正面を置く */
    r = (u8)((rail_len - 1) / RSTEP);
    if (r < n && KIND[r] == K_HOME) DECO[r] |= D_TIMBER;
#endif
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
/* 仮リングの頂点のうち、手前の面(0.5m)より近いものも投影する。森の木の壁は目より 6m も高いので、坂でカメラが少し
 * 傾くだけで上の頂点が 0.3m ほどまで寄り、仮リングが使えずに手前の区間をニアクリップで描いていた(重いフレームの多く)。
 * 0.125m 以上前にあれば、x・y・z を 2 倍か 4 倍にして project2 の範囲に入れて投影する(結果は同じ)。
 * そこはもともと画面のずっと外なので、絵はニアクリップで描いたときと変わらない */
static u8 vring_near(void)
{
    u8 k, sh;
    V3 t;
    const V3 *v = ring_v[VR];
    for (k = 0; k < 6; k++, v++) {
        if (v->z < 32 || v->x < -2048 || v->x > 2047 || v->y < -2048 || v->y > 2047) return 0;
    }
    for (k = 0, v = ring_v[VR]; k < 6; k++, v++) {
        if (v->z >= NEAR_Z) continue;
        sh = v->z >= 64 ? 1 : 2;
        t.x = v->x << sh; t.y = v->y << sh; t.z = v->z << sh;
        project2(&t, &ring_s[VR][k * 2]);
    }
    ring_front[VR] = 0x3F;
    return 1;
}

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
    vr_f = (u8)f;
    rail_at(sv, &p, &rp.yaw);
    rp.x = p.x; rp.y = p.y; rp.z = p.z;
    ring_no[VR] = no;
    g_rm_prof = vprof;
    ring_make(VR, &rp);
    return ring_front[VR] == 0x3F || vring_near();
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

#if HAS_BRIDGE
static u8 br_long;                /* 1 = 鉄橋のレールを区間ごとでなく bridge_rails で 1 枚に描く(前か後ろだけを見ていて、仮リングが鉄橋) */

/* 床の無い鉄橋の、区間の途中の枕木 2 本(約 1.3m ごと)。リング a・b の枕木の両端を画面で 3 等分した位置に 1 行の横線で描く
 * (遠近で少し奥に寄るが、細いので目立たない) */
static void ties_mid(u8 a, u8 b)
{
    u8 k;
    i16 x0, x1, y;
    RPT(0, a, -180, FLOOR_V); RPT(1, a, 180, FLOOR_V);
    RPT(2, b, -180, FLOOR_V); RPT(3, b, 180, FLOOR_V);
    g_color = C_WOOD;
    for (k = 1; k < 3; k++) {
        i16 f = k * 85;                                  /* 1/3・2/3(8.8) */
        x0 = scr[0] + fmul(scr[4] - scr[0], f);
        x1 = scr[2] + fmul(scr[6] - scr[2], f);
        y = scr[1] + fmul(scr[5] - scr[1], f);
        x0 = (x0 + 8) >> 4; x1 = (x1 + 8) >> 4; y = (y + 8) >> 4;
        if (y < 0 || y >= GAME_ROWS) continue;
        if (x0 < 0) x0 = 0;
        if (x1 > 64) x1 = 64;
        if (x1 <= x0) continue;
        g_rx = (u8)x0; g_ry = (u8)y; g_rw = (u8)(x1 - x0); g_rh = 1;
        rect_fill();
    }
}
#endif

/* 区間(リング a → b)の床のレール 2 本と枕木 */
static void rails_draw(u8 a, u8 b)
{
    u8 side;
    if (!ring_c[a][2] || !ring_c[b][2]) return;
    if (DECO[ring_no[a < b ? a : b]] & D_NORAIL) return;
    /* 枕木(区間の手前のリングの面に、横長の板) */
    RQUAD(a, -180, 180, FLOOR_V, FLOOR_V + 14, C_WOOD);
#if HAS_BRIDGE
    if (BRIDGEISH(KIND[ring_no[a < b ? a : b]])) {
        ties_mid(a, b);
        if (br_long) return;                   /* レールは bridge_rails が手前から 12m 先まで 1 枚で描く */
    }
#endif
    for (side = 0; side < 2; side++) {
        i16 u0 = side ? 102 : -128, u1 = side ? 128 : -102;
        RPT(0, a, u0, FLOOR_V);
        RPT(1, a, u1, FLOOR_V);
        RPT(2, b, u1, FLOOR_V);
        RPT(3, b, u0, FLOOR_V);
        raster(4, (HAS_CAVE && KIND[ring_no[a < b ? a : b]] == K_CAVE) ? C_RAIL : rail_col);
    }
}

#if HAS_CAVE || HAS_EXIT || HAS_LAKE || HAS_PARK
/* 床の石(三角形)。u0..u1 の幅、高さ ht */
static void rock_draw(u8 idx, i16 u0, i16 u1, i16 ht, u8 color)
{
    RPT(0, idx, u0, FLOOR_V);
    RPT(1, idx, u1, FLOOR_V);
    RPT(2, idx, (u0 + u1) >> 1, FLOOR_V + ht);
    raster(3, color);
}
#endif

static u8 view_back;              /* 1 = 後ろだけを見ている(家の奥の妻壁を描く) */

#if HAS_TOWN
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

/* 屋台の通り: 左右の屋台(台と縞の日よけ。近いときだけ台の上の品物)と、1 リングおきに通りをまたぐ提灯の綱 */
static void fair_draw(u8 idx, u8 no)
{
    u8 i, c = (no & 1) ? C_RED : C_WHITE;
    RQUAD(idx, -640, -470, FLOOR_V, -170, C_WOOD);
    RQUAD(idx, 470, 640, FLOOR_V, -170, C_WOOD);
    RQUAD(idx, -650, -430, 130, 230, c);                  /* 日よけ(壁から張り出す) */
    RQUAD(idx, 430, 650, 130, 230, c == C_RED ? C_WHITE : C_RED);
    if ((u16)ring_c[idx][2] >= 4096) {                    /* 8m より近い */
        RQUAD(idx, -620, -565, -170, -120, C_ORANGE);
        RQUAD(idx, -545, -490, -170, -130, C_YELLOW);
        RQUAD(idx, 490, 545, -170, -125, C_YELLOW);
        RQUAD(idx, 565, 620, -170, -130, C_RED);
    }
    if (!(no & 1)) {                                      /* 提灯の綱(軒の下の高さ。高いと画面の上の端に隠れる) */
        RQUAD(idx, -650, 650, 470, 490, C_BLACK);
        for (i = 0; i < 5; i++) {
            i16 u = -480 + i * 240;
            RQUAD(idx, u - 55, u + 55, 350, 470, (i & 1) ? C_ORANGE : C_RED);
        }
    }
}

/* 門塔の正面(門の入口・出口のリング面): 左右と上の石の壁、アーチの角、窓、赤い三角の屋根 */
static void gate_face(u8 idx)
{
    RQUAD(idx, -1500, -450, FLOOR_V, 1300, C_DIRT1);
    RQUAD(idx, 450, 1500, FLOOR_V, 1300, C_DIRT1);
    RQUAD(idx, -450, 450, 560, 1300, C_DIRT1);
    RPT(0, idx, 380, 560); RPT(1, idx, 450, 560); RPT(2, idx, 450, 300);
    raster(3, C_DIRT1);
    RPT(0, idx, -450, 300); RPT(1, idx, -450, 560); RPT(2, idx, -380, 560);
    raster(3, C_DIRT1);
    RQUAD(idx, -150, 150, 800, 1050, C_ROCK0);
    RPT(0, idx, -1700, 1300); RPT(1, idx, 1700, 1300); RPT(2, idx, 0, 2000);
    raster(3, C_DIRT0);
}

/* 噴水(広場の左): 石の水盤、柱、吹き上がる水(高さが揺れる)、しぶき */
static void fountain_draw(u8 idx)
{
    i16 h = ((u8)g_tick & 8) ? 650 : 600;
    RQUAD(idx, -1800, -800, FLOOR_V, -200, C_ROCK2);
    RQUAD(idx, -1750, -850, -230, -200, C_CYAN);
    RQUAD(idx, -1350, -1250, -200, 250, C_ROCK2);
    RQUAD(idx, -1330, -1270, 250, h, C_WHITE);
    RQUAD(idx, -1550, -1050, h - 100, h, C_CYAN);
}
#endif

#if HAS_HOME
/* 茅葺きの農家(畑の斜面の上)。白い壁と柱、戸、藁の屋根。sgn = 1 右 / -1 左 */
static void farm_house(u8 idx, i8 sgn)
{
    i16 a = sgn > 0 ? 1200 : -1900, b = a + 700, m = a + 350;
    RQUAD(idx, a, b, -80, 250, C_WHITE);
    RQUAD(idx, a, a + 50, -80, 250, C_WOOD);
    RQUAD(idx, b - 50, b, -80, 250, C_WOOD);
    RQUAD(idx, m - 100, m + 100, -80, 150, C_WOOD);
    RPT(0, idx, a - 150, 250); RPT(1, idx, b + 150, 250); RPT(2, idx, m, 650);
    raster(3, C_DIRT0);
}

/* かかし(田の中): 杭と腕、白い顔、藁の笠。sgn = 1 右 / -1 左 */
static void scarecrow(u8 idx, i8 sgn)
{
    i16 u = 1300 * sgn;
    RQUAD(idx, u - 15, u + 15, -430, -100, C_WOOD);
    RQUAD(idx, u - 150, u + 150, -200, -170, C_WOOD);
    RQUAD(idx, u - 40, u + 40, -100, -20, C_WHITE);
    RPT(0, idx, u - 110, -20); RPT(1, idx, u + 110, -20); RPT(2, idx, u, 60);
    raster(3, C_DIRT0);
}

/* 水車と水車小屋(右)。木の円盤に明るいスポーク 3 本と外周の羽根 6 枚が回る(羽根だけだと散らばった点に見えた) */
#define MW_U 1150
#define MW_V (-120)
static void mill_draw(u8 idx)
{
    u8 k, rot = (u8)(g_tick << 1);
    RQUAD(idx, 1650, 2500, -430, 150, C_WHITE);
    RQUAD(idx, 1950, 2150, -430, 0, C_WOOD);
    RPT(0, idx, 1500, 150); RPT(1, idx, 2650, 150); RPT(2, idx, 2075, 600);
    raster(3, C_DIRT0);
    for (k = 0; k < 6; k++) {
        u8 a = (u8)(k * 43 + rot);
        RPT(k, idx, MW_U + fmul(430, SIN(a)), MW_V + fmul(430, COS(a)));
    }
    raster(6, C_WOOD);
    for (k = 0; k < 3; k++) {
        u8 a = (u8)(k * 85 + rot);
        i16 du = fmul(400, SIN(a)), dv = fmul(400, COS(a)), eu = fmul(30, COS(a)), ev = -fmul(30, SIN(a));
        RPT(0, idx, MW_U - du - eu, MW_V - dv - ev); RPT(1, idx, MW_U - du + eu, MW_V - dv + ev);
        RPT(2, idx, MW_U + du + eu, MW_V + dv + ev); RPT(3, idx, MW_U + du - eu, MW_V + dv - ev);
        raster(4, C_ROCK1);
    }
    for (k = 0; k < 6; k++) {
        u8 a = (u8)(k * 43 + rot + 21);
        i16 u = MW_U + fmul(480, SIN(a)), v = MW_V + fmul(480, COS(a));
        RQUAD(idx, u - 70, u + 70, v - 70, v + 70, C_WOOD);
    }
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
#endif

#if HAS_PARK
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

/* 右の斜面の下のジェットコースターの宙返りの輪(中心 u = 2700、斜面の下 -700 の 900 上): 暗い軌道の輪(内を空の色で抜く)、
 * ハの字の支柱、輪を回る赤いコースター。左の柵の向こうに置くと、近くでは画面の上まで届く柵に隠れる */
#define LP_U 2700
#define LP_V 200
static void park_loop(u8 idx)
{
    u8 j, k, a = (u8)(g_tick << 2);
    RPT(0, idx, LP_U - 60, LP_V); RPT(1, idx, LP_U + 60, LP_V);
    RPT(2, idx, LP_U + 700, -700); RPT(3, idx, LP_U - 700, -700);
    raster(4, C_ROCK0);
    for (j = 0; j < 2; j++) {
        i16 r = j ? 780 : 900;
        for (k = 0; k < 8; k++) RPT(k, idx, LP_U + fmul(r, SIN(k * 32)), LP_V + fmul(r, COS(k * 32)));
        raster(8, j ? C_SKY : C_ROCK0);
    }
    {
        i16 u = LP_U + fmul(840, SIN(a)), v = LP_V + fmul(840, COS(a));
        RQUAD(idx, u - 110, u + 110, v - 90, v + 90, C_DIRT0);
    }
}

/* 右の斜面の向こうの丘の上のお城(中心 u = 3000): 白い城壁と 3 本の塔、赤い円錐の屋根、旗。夕暮れの空に花火(広がって消える 8 つの光) */
static void park_castle(u8 idx)
{
    static const i16 tw[3][3] = { { 2200, 1100, 300 }, { 2950, 1700, 380 }, { 3800, 1100, 300 } };   /* 塔: 中心・上端・半幅 */
    u8 k, t = (u8)g_tick & 63;
    RQUAD(idx, 1900, 4100, -700, 500, C_WHITE);
    for (k = 0; k < 3; k++) {
        i16 c = tw[k][0], top = tw[k][1], w = tw[k][2];
        RQUAD(idx, c - w, c + w, 300, top, C_WHITE);
        RQUAD(idx, c - 60, c + 60, top - 500, top - 250, C_ROCK0);   /* 窓 */
        RPT(0, idx, c - w - 80, top); RPT(1, idx, c + w + 80, top); RPT(2, idx, c, top + 700);
        raster(3, C_DIRT0);
    }
    RQUAD(idx, 2950 - 10, 2950 + 10, 2400, 2700, C_ROCK0);         /* 旗竿と旗 */
    RQUAD(idx, 2960, 3150, 2580, 2700, C_DIRT1);
    if (t < 40) {
        i16 r = 120 + t * 25;
        u8 c = (t < 20) ? C_DIRT1 : C_WHITE;
        for (k = 0; k < 8; k++) {
            i16 u = 1700 + fmul(r, SIN(k * 32)), v = 3100 + fmul(r, COS(k * 32));
            RQUAD(idx, u - 50, u + 50, v - 50, v + 50, c);
        }
    }
}

/* 遊園地の外の屋台の並ぶ道: 左右の屋台(縞の日よけ。14m より近いときは台も)と、3 リングおきに通りをまたぐ提灯の綱。
 * 長方形 1 つでも約 0.2ms かかる。品物・柱まで毎リング描くと 1 フレーム平均 52ms になった(屋台の無い丘は 42ms) */
static void park_stalls(u8 idx, u8 no)
{
    u8 i, c = (no & 1) ? C_DIRT0 : C_WHITE;
    RQUAD(idx, -800, -440, -90, 10, c);                  /* 日よけは台のすぐ上(柱を描かないので、高いと宙に浮いて見えた) */
    RQUAD(idx, 440, 800, -90, 10, c == C_WHITE ? C_DIRT0 : C_WHITE);
    if ((u16)ring_c[idx][2] >= 2400) {
        RQUAD(idx, -760, -480, -330, -140, C_WOOD);
        RQUAD(idx, 480, 760, -330, -140, C_WOOD);
    }
    if (!(no % 3)) {
        RQUAD(idx, -840, 840, 480, 500, C_ROCK0);
        for (i = 0; i < 3; i++) {
            i16 u = -400 + i * 400;
            RQUAD(idx, u - 60, u + 60, 360, 480, (i & 1) ? C_ORANGE : C_DIRT0);
        }
    }
}
#endif

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
#if HAS_CAVE || HAS_EXIT
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
#endif
#if HAS_LAKE
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
    case K_FALLS: {
        /* 左の岩壁を流れ落ちる滝(明るい水の幕に、下へ流れる白い筋 2 本)と、滝つぼのしぶき */
        i16 o = (i16)(((u8)g_tick * 12) & 0x1FF);
        RQUAD(idx, -1300, -820, -380, 1400, C_WATER1);
        RQUAD(idx, -1220, -1180, 1100 - o, 1400 - o, C_WHITE);
        RQUAD(idx, -1080, -1040, 700 - o, 1000 - o, C_WHITE);
        RQUAD(idx, -940, -900, 900 - o, 1200 - o, C_WHITE);
        RQUAD(idx, -1450, -650, -380, -230, C_WHITE);
        break;
    }
    case K_CRYSTAL: {
        /* 壁と天井から生えた光る結晶(紫と水色の三角)。輝きがリングごとにずれて明滅する */
        u8 on = (u8)((no + (g_tick >> 4)) & 1);
        RPT(0, idx, -850, 120); RPT(1, idx, -850, 420); RPT(2, idx, -560, 330);
        raster(3, C_PURPLE);
        RPT(0, idx, -760, -380); RPT(1, idx, -640, -380); RPT(2, idx, -690, -60);
        raster(3, on ? C_WHITE : C_CYAN);
        RPT(0, idx, 850, 200); RPT(1, idx, 850, 480); RPT(2, idx, 580, 300);
        raster(3, on ? C_CYAN : C_WHITE);
        RPT(0, idx, -120, 900); RPT(1, idx, 120, 900); RPT(2, idx, (no & 1) ? 60 : -60, 620);
        raster(3, C_PURPLE);
        break;
    }
    case K_WRECK:
        if (d & D_TIMBER) {
            /* ひっくり返って半分沈んだトロッコ(右。車体は傾き、車輪が上)と、水から突き出た折れたレール(左) */
            RPT(0, idx, 380, -380); RPT(1, idx, 1150, -380); RPT(2, idx, 1100, 60); RPT(3, idx, 440, -40);
            raster(4, C_RED);
            RPT(0, idx, 440, -40); RPT(1, idx, 1100, 60); RPT(2, idx, 1095, 120); RPT(3, idx, 445, 20);
            raster(4, C_ROCK0);
            RQUAD(idx, 500, 660, 10, 150, C_BLACK);
            RQUAD(idx, 880, 1040, 60, 200, C_BLACK);
            RPT(0, idx, -700, -380); RPT(1, idx, -660, -380); RPT(2, idx, -480, 100); RPT(3, idx, -520, 100);
            raster(4, C_ROCK0);
        } else if (d & D_ROCK_L) RQUAD(idx, -720, -480, -385, -355, C_WOOD);   /* 浮いた板 */
        else RQUAD(idx, 460, 700, -385, -355, C_WOOD);
        break;
    case K_ARCH:
        /* 岩の門の天井から密に下がる鍾乳石 */
        RPT(0, idx, -520, 600); RPT(1, idx, -300, 600); RPT(2, idx, -420, 250);
        raster(3, C_ROCK2);
        RPT(0, idx, 260, 600); RPT(1, idx, 480, 600); RPT(2, idx, 380, 300);
        raster(3, C_ROCK2);
        break;
#endif
#if HAS_FOREST
    case K_FOREST: {
        i16 o = (no & 1) ? 90 : 0;                        /* 幹の位置をリングごとにずらす */
        if (d & D_TIMBER) {                               /* 頭上の葉の塊 */
            RQUAD(idx, -760, -150 - o, 820, 1160, C_DIRT0);
            RQUAD(idx, 170 + o, 760, 860, 1200, C_DIRT0);
            RQUAD(idx, -520, -260, 1000, 1140, C_DIRT1);
        }
        if (d & D_LAMP_L) RQUAD(idx, -560 + o, -470 + o, FLOOR_V, 900, C_WOOD);
        if (d & D_LAMP_R) RQUAD(idx, 490 - o, 580 - o, FLOOR_V, 940, C_WOOD);
        if (d & D_ROCK_L) {                               /* 頭上を斜めに横切る倒木(長方形 2 つの段で斜めに見せる。多角形の塗りより軽い) */
            RQUAD(idx, -900, 0, 560, 670, C_WOOD);
            RQUAD(idx, 0, 900, 640, 750, C_WOOD);
        }
        break;
    }
    case K_GLADE:
        /* 草地の花(黄・白・赤の小さな点。草の斜面の高さに置く)、切り株、木立の手前の一本木 */
        if (d & D_LAMP_L) {
            RQUAD(idx, -905, -855, -215, -175, C_YELLOW);
            RQUAD(idx, -1125, -1075, -160, -120, C_WHITE);
        }
        if (d & D_LAMP_R) {
            RQUAD(idx, 825, 875, -230, -190, C_RED);
            RQUAD(idx, 1055, 1105, -165, -125, C_YELLOW);
        }
        if (d & D_ROCK_L) {
            RQUAD(idx, -760, -640, -265, -150, C_WOOD);
            RQUAD(idx, -760, -640, -150, -125, C_ROCK1);  /* 切り口 */
        }
        if (d & D_TIMBER) {
            i16 uc = (no & 1) ? 2100 : -2100;
            RQUAD(idx, uc - 80, uc + 80, -100, 700, C_WOOD);
            RQUAD(idx, uc - 450, uc + 450, 600, 1300, C_DIRT0);
            RQUAD(idx, uc - 300, uc + 150, 1000, 1250, C_DIRT1);
        }
        break;
    case K_STREAM:
        /* 水面のさざ波(リングごとにずらす)と、丸太の橋の縁の杭 */
        {
            i16 o = (no & 1) ? 160 : 0;
            RQUAD(idx, -1000 + o, -800 + o, -405, -388, C_WHITE);
            RQUAD(idx, 700 + o, 900 + o, -400, -383, C_WHITE);
        }
        RQUAD(idx, -265, -235, FLOOR_V, -150, C_WOOD);
        RQUAD(idx, 235, 265, FLOOR_V, -150, C_WOOD);
        break;
    case K_HOLLOW:
        /* 丸太の切り口(木の洞の口のまわりの明るい輪)。外から見るときだけ: 入口はカメラより前、出口は後ろにあるとき
         * (中から見ると、洞の壁の上に描いてしまう) */
        if (((d & D_LAMP_L) && idx > NB) || ((d & D_LAMP_R) && idx <= NB)) {
            static const i16 cut_o[12] = { -560, 820, 560, 820, 900, 240, 700, -345, -700, -345, -900, 240 };
            static const i16 cut_b[12] = { -660, 960, 660, 960, 1040, 270, 820, -345, -820, -345, -1040, 270 };
            u8 k, k1;
            if ((idx >= NB + 2 || idx + 2 <= NB) && ring_front[idx] == 0x3F) {
                /* 8m より遠い: 樹皮の六角形、切り口の六角形、暗い口(断面の速い塗り)の 3 枚。輪を 10 枚の多角形で描くと
                 * 入口が見えている間ずっと 1 フレーム約 7ms 重い。遠い口の中はもともと暗い色なので、見た目はほぼ同じ */
                for (k = 0; k < 6; k++) RPT(k, idx, cut_b[k * 2], cut_b[k * 2 + 1]);
                raster(6, C_WOOD);
                for (k = 0; k < 6; k++) RPT(k, idx, cut_o[k * 2], cut_o[k * 2 + 1]);
                raster(6, C_ROCK1);
                g_color = C_ROCK0;
                hex_fast(idx);
                break;
            }
            /* 近い: 内側 = 洞の断面、その外に明るい切り口の輪(cut_o)、いちばん外に樹皮の縁(cut_b)を輪の形で(口の中が見える) */
            {
            const i16 *pi = &PROF[no * 12];
            for (k = 0; k < 6; k++) {
                if (k == 3) continue;                     /* 底(地面に接する所)は輪が無い */
                k1 = (k == 5) ? 0 : k + 1;
                RPT(0, idx, cut_o[k * 2], cut_o[k * 2 + 1]);
                RPT(1, idx, cut_o[k1 * 2], cut_o[k1 * 2 + 1]);
                RPT(2, idx, cut_b[k1 * 2], cut_b[k1 * 2 + 1]);
                RPT(3, idx, cut_b[k * 2], cut_b[k * 2 + 1]);
                raster(4, C_WOOD);
                RPT(0, idx, pi[k * 2], pi[k * 2 + 1]);
                RPT(1, idx, pi[k1 * 2], pi[k1 * 2 + 1]);
                RPT(2, idx, cut_o[k1 * 2], cut_o[k1 * 2 + 1]);
                RPT(3, idx, cut_o[k * 2], cut_o[k * 2 + 1]);
                raster(4, C_ROCK1);
            }
            }
        }
        break;
    case K_SHRINE:
        /* 左の苔むした祠(石の台、木の祠、苔の屋根、紙垂)と、線路をまたぐ赤い鳥居 */
        RQUAD(idx, -700, -460, FLOOR_V, -250, C_ROCK0);
        RQUAD(idx, -670, -490, -250, -20, C_WOOD);
        RQUAD(idx, -620, -540, -230, -60, C_ROCK0);
        RPT(0, idx, -730, -20); RPT(1, idx, -430, -20); RPT(2, idx, -580, 140);
        raster(3, C_DIRT0);
        RQUAD(idx, -645, -625, -70, -20, C_WHITE);
        RQUAD(idx, -535, -515, -70, -20, C_WHITE);
        RQUAD(idx, -380, -320, FLOOR_V, 620, C_RED);
        RQUAD(idx, 320, 380, FLOOR_V, 620, C_RED);
        RQUAD(idx, -380, 380, 500, 550, C_RED);
        RQUAD(idx, -480, 480, 620, 700, C_RED);
        RQUAD(idx, -500, 500, 700, 730, C_BLACK);
        break;
#endif
#if HAS_PARK
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
    case K_LOOP:
    case K_CASTLE:
    case K_PARKSIDE: {
        /* 柵の上の灯り(点滅)、柵の上に見える出し物(柵の上端より上だけ)、右の茂みと杭 */
        u8 on = (u8)(g_tick >> 3) & 1;
        RQUAD(idx, -545, -505, 520, 580, ((no + on) & 1) ? C_DIRT1 : C_ROCK0);
        if (KIND[no] == K_PARKSIDE) {
            if (no % 5 == 1) park_tent(idx, -1600, 560);
            if (no % 10 == 4) park_carousel(idx, -2300, 560 - 650, 0);
            if (no % 10 == 8 && (u16)ring_c[idx][2] < 2400) park_wheel(idx, -3000, 560);
        } else if (d & D_TIMBER) {
            if (KIND[no] == K_LOOP) park_loop(idx);
            else park_castle(idx);
        }
        if (d & D_LAMP_R) RQUAD(idx, 580, 620, FLOOR_V - 60, FLOOR_V + 120, C_WOOD);
        if (d & D_ROCK_R) rock_draw(idx, 650, 900, 130, C_ROCK0);
        break;
    }
    case K_STALLS:
        park_stalls(idx, no);
        break;
    case K_HILL:
        if (d & D_LAMP_L) RQUAD(idx, -620, -580, FLOOR_V, FLOOR_V + 160, C_WOOD);
        if (d & D_LAMP_R) RQUAD(idx, 580, 620, FLOOR_V, FLOOR_V + 160, C_WOOD);
        if (d & D_ROCK_L) rock_draw(idx, -860, -600, 160, C_ROCK0);
        if (d & D_ROCK_R) rock_draw(idx, 600, 860, 140, C_ROCK0);
        break;
#endif
#if HAS_BRIDGE
    case K_BRIDGE:
        /* 頭上の梁と床の下の横桁はリングごと(4m ごと)。左右の縦材は 8m より先だけ(手前は斜材が描かれる。縦材は手前ほど行が多く重い) */
        RQUAD(idx, -BR_U, BR_U, BR_CHD, BR_TOP, C_DIRT0);
        RQUAD(idx, -BR_U, BR_U, FLOOR_V - 60, FLOOR_V - 20, C_DIRT0);      /* 床の下の横桁 */
        if ((i8)idx - NB >= 2 || (i8)idx - NB <= -2) {
            RQUAD(idx, -BR_U - 30, -BR_U + 30, BR_GIR, BR_CHD, C_DIRT0);
            RQUAD(idx, BR_U - 30, BR_U + 30, BR_GIR, BR_CHD, C_DIRT0);
        }
        break;
    case K_ARCHBR: {
        /* 床の下の横桁、アーチから下がる吊り材(左右)、アーチの高い所では頭上の横梁 */
        i16 h = PROF[no * 12 + 5];
        RQUAD(idx, -BR_U, BR_U, FLOOR_V - 60, FLOOR_V - 20, C_DIRT0);
        if (h > FLOOR_V + 120) {
            RQUAD(idx, -BR_U - 20, -BR_U + 20, BR_GIR, h, C_DIRT0);
            RQUAD(idx, BR_U - 20, BR_U + 20, BR_GIR, h, C_DIRT0);
        }
        if (h > 900) RQUAD(idx, -BR_U, BR_U, h, h + 70, C_DIRT0);
        break;
    }
#endif
#if HAS_TOWN
    case K_TOWN:
    case K_FAIR:
        /* 前を見ているときは家の手前の妻壁、後ろを見ているときは奥の妻壁(見えない側は道側の壁に隠れる) */
        if (!view_back) {
            if (d & D_LAMP_R) house_end(idx, 1, TONE[no * 6 + 2]);
            if (d & D_LAMP_L) house_end(idx, -1, TONE[no * 6 + 4]);
        } else if (no) {
            if (d & D_ROCK_R) house_end(idx, 1, TONE[(no - 1) * 6 + 2]);
            if (d & D_ROCK_L) house_end(idx, -1, TONE[(no - 1) * 6 + 4]);
        }
        if (KIND[no] == K_FAIR) fair_draw(idx, no);
        break;
    case K_CANAL:
        /* 欄干の街灯(1 リングおき)と、ずっと下の水面の小舟 */
        if (no & 1) {
            RQUAD(idx, -500, -465, -170, 500, C_ROCK0);
            RQUAD(idx, -535, -430, 500, 580, C_YELLOW);
        }
        if (no % 3 == 1) RQUAD(idx, 900, 1600, -1050, -950, C_WOOD);
        break;
    case K_PLAZA:
        if (d & D_TIMBER) fountain_draw(idx);
        else if (no & 1) {                                /* 街灯 */
            RQUAD(idx, -920, -880, FLOOR_V, 450, C_ROCK0);
            RQUAD(idx, -950, -850, 450, 540, C_YELLOW);
            RQUAD(idx, 880, 920, FLOOR_V, 450, C_ROCK0);
            RQUAD(idx, 850, 950, 450, 540, C_YELLOW);
        }
        break;
    case K_GATE:
        /* 門塔の正面は外から見るときだけ(入口はカメラより前、出口は後ろ。中から見ると門の中の壁の上に描いてしまう) */
        if (((d & D_LAMP_L) && idx > NB) || ((d & D_LAMP_R) && idx <= NB)) gate_face(idx);
        break;
#endif
#if HAS_HOME
    case K_HOME:
        RQUAD(idx, -580, -540, -170, 20, C_WOOD);         /* 柵の杭と横木 */
        RQUAD(idx, 540, 580, -170, 20, C_WOOD);
        RQUAD(idx, -760, -540, -60, -30, C_WOOD);
        RQUAD(idx, 540, 760, -60, -30, C_WOOD);
        if (d & D_LAMP_L) farm_house(idx, -1);
        if (d & D_LAMP_R) farm_house(idx, 1);
        if (d & D_TIMBER) home_draw(idx);
        break;
    case K_PADDY:
    case K_MILL: {
        u8 mill = KIND[no] == K_MILL;
        /* あぜ(3 リングごとに田を区切る緑の線)と苗の列。水車の所は右が水車の池 */
        if (!(no % 3)) {
            RQUAD(idx, -3200, -620, -430, -405, C_GRASS);
            if (!mill) RQUAD(idx, 620, 3200, -430, -405, C_GRASS);
        }
        RQUAD(idx, -1030, -970, -430, -385, C_GRASS);
        RQUAD(idx, -1630, -1570, -430, -385, C_GRASS);
        if (!mill) {
            RQUAD(idx, 970, 1030, -430, -385, C_GRASS);
            RQUAD(idx, 1570, 1630, -430, -385, C_GRASS);
        }
        if (d & D_LAMP_L) scarecrow(idx, -1);
        if (d & D_LAMP_R) scarecrow(idx, 1);
        if (d & D_TIMBER) mill_draw(idx);
        break;
    }
#endif
    }
}

#if HAS_BRIDGE
/* ---------------------------------------------------------------------------
 * 鉄橋
 * --------------------------------------------------------------------------- */
/* 区間 a〜b の左右のトラスの斜材。上の端は投影済みの断面の頂点(右 = 2 上弦材の下、左 = 5)、下の端はリング面の点から 2D で求める。
 * 太さは横に取る(そのリングの床から上弦材までの高さの 1/32 ずつ左右へ。遠くでも 1 ブロック)。上下の端の辺が水平になるので、
 * 一般の多角形の塗り(raster)でも割り算の要る辺は 2 本だけで済む(縦の太さの四角形だと 4 本で、1 本 約 0.7ms かかっていた)。
 * 斜材は区間ごとに向きを変える(ワーレン形)。仮リング(区間の途中)では、斜材の点を区間の中の位置 vr_f で上下の間に補間する。
 * 上弦材は断面の面 1・5 なので、区間の面と一緒に塗られる */
/* 斜材の端 1 つ(truss.s): 頂点 tp_t(上弦材の下)と tp_f(床)の間の、区間の中の位置 tp_g の点を、左右に太さを付けた 2 点として
 * tp_o[0..3] に書く(tp_rev = 1 なら右の点を先に。四角形の頂点の順をそろえる) */
extern const i16 *tp_t, *tp_f;
extern i16 *tp_o;
extern u8 tp_g, tp_rev;
void truss_pt(void);
#define TRUSS_PT(t, f, g, o, rev) do { tp_t = (t); tp_f = (f); tp_g = (g); tp_o = (o); tp_rev = (rev); truss_pt(); } while (0)

static i16 tb[8];                /* 斜材の下の端: リング a の右、b の右、a の左、b の左 */
static void truss_draw(u8 a, u8 b)
{
    u8 sec, ga, gb;
    const i16 *pa, *pb;
    if (!ring_c[a][2] || !ring_c[b][2]) return;
    /* 区間の番号と、両端の区間の中の位置(0 = 区間の頭、255 = 次のリング) */
    if (a == VR) { sec = ring_no[a]; ga = vr_f; gb = ring_no[b] == sec ? 0 : 255; }
    else if (b == VR) { sec = ring_no[b]; gb = vr_f; ga = ring_no[a] == sec ? 0 : 255; }
    else { sec = ring_no[a] < ring_no[b] ? ring_no[a] : ring_no[b]; ga = ring_no[a] == sec ? 0 : 255; gb = 255 - ga; }
    if (sec & 1) { ga = 255 - ga; gb = 255 - gb; }       /* 偶数の区間は頭の下から次のリングの上へ、奇数は逆 */
    pa = ring_s[a];
    pb = ring_s[b];
    /* 上の端は頂点 2(右の上弦材の下)・5(左)、下の端はトラスの面の床の高さ(±BR_U。床は線路の幅なので頂点には無い) */
    RPT(0, a, BR_U, FLOOR_V); RPT(1, b, BR_U, FLOOR_V); RPT(2, a, -BR_U, FLOOR_V); RPT(3, b, -BR_U, FLOOR_V);
    {
        u8 k;
        for (k = 0; k < 8; k++) tb[k] = scr[k];
    }
    TRUSS_PT(pa + 4, &tb[0], ga, &scr[0], 0);
    TRUSS_PT(pb + 4, &tb[2], gb, &scr[4], 1);
    raster(4, C_DIRT0);
    TRUSS_PT(pa + 10, &tb[4], ga, &scr[0], 0);
    TRUSS_PT(pb + 10, &tb[6], gb, &scr[4], 1);
    raster(4, C_DIRT0);
}

/* 鉄橋の、仮リングから奥のリングまで 1 枚で描く細い四角形(鉄橋はまっすぐなので、区間ごとに分けなくても形が合う)。
 * 仮リングの点 (sq_ua, sq_va)・(sq_ub, sq_vb) と、リング far の同じ点を結ぶ */
static i16 sq_ua, sq_va, sq_ub, sq_vb;
static void strip_q(u8 far, u8 color)
{
    RPT(0, VR, sq_ua, sq_va);
    RPT(1, VR, sq_ub, sq_vb);
    RPT(2, far, sq_ub, sq_vb);
    RPT(3, far, sq_ua, sq_va);
    raster(4, color);
}

/* リング k0 から手前へ、見えている鉄橋のリングを探す(無ければ 0)。dir = 1 前 / -1 後ろ */
static u8 far_ring(i8 k0, i8 dir)
{
    i8 k;
    for (k = k0; k != 0; k -= dir) {
        u8 idx = (u8)(k + NB);
        if (ring_ok[idx] && ring_c[idx][2] && BRIDGEISH(KIND[ring_no[idx]])) return idx;
    }
    return 0;
}

/* 鉄橋の左右の欄干(トラスの面、床から 0.25〜0.45m の赤茶の帯。目より低いので、海の上に縁の線が通る)。見えている一番遠い
 * 鉄橋のリングまで。縁の目立つ欄干で、その外が下の海だと分かる(Virtua Racing の Bay Bridge の欄干、Stunt Car Racer の
 * 高架の縁にならう)。区間の面より先に描く */
static void handrail_draw(i8 dir)
{
    u8 far = far_ring(dir > 0 ? NF : -(NB - 1), dir);
    if (!far) return;
    sq_va = FLOOR_V + 115; sq_vb = FLOOR_V + 64;
    sq_ua = sq_ub = -BR_U;
    strip_q(far, C_DIRT0);
    sq_ua = sq_ub = BR_U;
    strip_q(far, C_DIRT0);
}

/* 鉄橋のレール 2 本を、仮リングから 12m 先のリングまで 1 枚ずつで描く(区間ごとに描くと区間の数だけ一般の多角形の塗りが要る)。
 * 区間の枕木と横桁の後、最後に描く(レールが上) */
static void bridge_rails(i8 dir)
{
    u8 far = far_ring(3 * dir, dir);
    if (!far) return;
    sq_va = sq_vb = FLOOR_V;
    sq_ua = -128; sq_ub = -102;
    strip_q(far, rail_col);
    sq_ua = 102; sq_ub = 128;
    strip_q(far, rail_col);
}
#endif

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
        if ((HAS_EXIT && kind == K_EXIT) || (HAS_LAKE && LAKEISH(kind))) {
            /* 奥は暗い岩(水面はそのまま) */
            if (dist >= 2 && c != C_WATER && c != C_WATER1) c = C_ROCK0;
        } else if ((HAS_FOREST && kind == K_HOLLOW) || (HAS_TOWN && kind == K_GATE)) {
            if (dist >= 2 && c) c = C_ROCK0;    /* 倒木・門の奥は影(出口のリングの塗らない面はそのまま) */
        } else if (HAS_CAVE && kind == K_CAVE && dist >= 2) {
            if (c == C_ROCK2) c = C_ROCK1;
            else if (c == C_ROCK1 || c == C_DIRT0) c = C_ROCK0;
            else c = C_DIRT0;
        } else if (HAS_BRIDGE && (kind == K_BRIDGE || kind == K_ARCHBR) && k == 3 && dist < 3) {
            c = 0;                              /* 鉄橋の手前 12m は床を塗らない(枕木とレールの隙間に海が見える) */
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
    /* 面の色は区間の手前のリングの表から。仮リング(カメラのいる区間の途中)を含むときは、その区間のリング(ring_no[VR])
     * (番号の小さいほうだと、前を見ているときに区間の先のリングの色になり、地形の境目で 1 区間ずれる) */
    na = (a == VR || b == VR) ? ring_no[VR] : ring_no[a < b ? a : b];
    if (DECO[na] & D_EMPTY) {
        /* 面の無い区間(遊園地の高架・空中)。高架はレールだけが見えるので遠くまで描く */
        if (HAS_PARK && KIND[na] == K_PARK) rails_draw(a, b);
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
    /* トラスの斜材は 8m まで(その先は細くて目立たない。縦材・頭上の梁・上弦材は遠くまで描く。12m までにすると 1 フレーム約 2ms 重い) */
#if HAS_BRIDGE
    if (dist < 2 && KIND[na] == K_BRIDGE && fa == 0x3F && fb == 0x3F) truss_draw(a, b);
#endif
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
    return cap_ok(r) && !(HAS_LAKE && LAKEISH(KIND[ring_no[idx]]));
}

/* リング r〜r+2 の 2 区間を 1 つの帯で描いてよいか: 3 本とも森か地底湖(断面の形がなめらかに変わる地形)。
 * 町の家並みや遊園地の建物は、リングごとに形が変わるので、途中のリングを飛ばすと遠くの屋根や建物の形が欠ける */
static u8 merge_ok(i8 r)
{
#if HAS_FOREST || HAS_LAKE
    u8 i, kd;
    for (i = 0; i < 3; i++) {
        kd = KIND[ring_no[(u8)(r + NB + i)]];
        if (kd != K_FOREST && kd != K_LAKE) return 0;
    }
    return 1;
#else
    (void)r;
    return 0;
#endif
}

/* 奥の開口部を塞ぐ色: 坑道は闇(黒)、地底湖は岩壁と同じ暗い岩の色(黒い穴に見えないように) */
static u8 cap_col(i8 r)
{
    return (HAS_LAKE && LAKEISH(KIND[ring_no[(u8)(r + NB)]])) ? C_ROCK0 : C_BLACK;
}

static void hex_draw(i8 r, u8 color)
{
    g_color = color;
    hex_fast((u8)(r + NB));
}

static u8 sky_row;                /* 地平線の行(sky_draw) */

#if HAS_BRIDGE
/* 鉄橋のまわりの海の白波。海面は目の 12m 下にあるものとして、白波の点(左右 ±15.5m、奥行き 6〜70m を回る)を投影し、
 * 1 行の短い線で描く。近くの枕木や横桁より遅く流れるので、海が橋のずっと下にあると分かる。back = 後ろを見ている */
extern u16 wave_inv[256];        /* 8192 / (24 + i)(白波の奥行き(1/4m)の逆数。truss.s。初めて使うときに作る。割り算は 1 回 約 150µs) */
extern u8 sw_m, sw_row;
void sea_dots(void);
/* 向こう岸の陸地(地平線の上の低い丘。横位置・幅・高さのブロック)。遠くの陸が見えると、海の広さと橋の高さが分かる
 * (Virtua Racing の Bay Bridge の地平線の岬にならう) */
static const u8 coast[][3] = { { 0, 15, 1 }, { 3, 7, 2 }, { 41, 23, 1 }, { 49, 10, 2 }, { 53, 4, 3 } };

/* 画面の長方形を、画面の中だけに切って塗る(灯台・港。はみ出す所は描かない) */
static void clip_rect(i16 x, i16 y, i16 w, i16 h, u8 c)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > 64) w = 64 - x;
    if (y + h > GAME_ROWS) h = GAME_ROWS - y;
    if (w <= 0 || h <= 0) return;
    g_color = c; g_rx = (u8)x; g_ry = (u8)y; g_rw = (u8)w; g_rh = (u8)h;
    rect_fill();
}

/* 沖の小島の灯台(レールの 110m の所、左 22m)。白波と同じく、目の 12m 下の海面にあるものとして投影する。
 * 紅白の塔、明滅する灯り、灯りが点いている間は左右に振れる光の帯 */
#define LH_M (110 * 4)
static void lighthouse(u16 m, u8 back)
{
    i16 dq = back ? (i16)m - LH_M : LH_M - (i16)m, inv, row, c, w, h, x;
    u8 on = (u8)(g_tick >> 4) & 1;
    if (dq < 24 || dq > 24 + 255) return;
    inv = (i16)wave_inv[dq - 24];
    row = sky_row + ((48 * inv) >> 8);
    c = (88 * inv) >> 8;
    c = back ? 32 + c : 32 - c;
    w = (40 * inv) >> 8; if (w < 3) w = 3;
    clip_rect(c - (w >> 1), row - 1, w, 2, C_ROCK1);                 /* 小島 */
    w = (8 * inv) >> 8; if (w < 1) w = 1;
    h = (56 * inv) >> 8; if (h < 3) h = 3;
    x = c - (w >> 1);
    clip_rect(x, row - 1 - h, w, h, C_WHITE);                        /* 塔 */
    clip_rect(x, row - 1 - h + (h >> 2), w, (h >> 2) + 1, C_RED);    /* 赤い帯 */
    clip_rect(x, row - 2 - h, w, 1, on ? C_YELLOW : C_ROCK0);        /* 灯り */
    if (on) clip_rect((g_tick & 32) ? x + w : x - 6, row - 2 - h, 6, 1, C_DIRT1);   /* 光の帯 */
}

/* 向こう岸の港(右の岸に倉庫とクレーン)。レールの 140m から、近づくにつれて大きくなる */
static void harbor(u16 m)
{
    u8 i, k;
    if (m < 140 * 4) return;
    k = (u8)((m - 140 * 4) >> 5);                        /* 8m ごとに 1 ずつ */
    if (k > 4) k = 4;
    for (i = 0; i < 3; i++) {
        i16 x = 40 + i * (6 + k), h = 1 + (k >> 1);
        clip_rect(x, sky_row - h, 5 + k, h, C_DIRT1);                 /* 倉庫の壁 */
        clip_rect(x, sky_row - h - 1, 5 + k, 1, C_DIRT0);             /* 屋根 */
    }
    clip_rect(60, sky_row - 4 - k, 1, 4 + k, C_DIRT0);               /* クレーンの柱と腕 */
    clip_rect(55, sky_row - 4 - k, 6, 1, C_DIRT0);
}

static void sea_waves(u16 s, u8 back)
{
    u8 i;
    u16 m = s >> 4;                                      /* 1/4m */
    if (!wave_inv[0]) { i = 0; do { wave_inv[i] = 8192 / (24 + i); } while (++i); }
    if (sky_row >= 3 && sky_row + 2 < GAME_ROWS) {
        g_rh = 2;                                        /* 地平線の近くの明るい帯(遠くの海のもや。Out Run・Virtua Racing の海にならう) */
        buf_fill_rows(C_DIRT1, sky_row);
        g_color = C_ROCK1;                               /* 向こう岸 */
        for (i = 0; i < sizeof(coast) / 3; i++) {
            g_rx = coast[i][0]; g_rw = coast[i][1]; g_rh = coast[i][2]; g_ry = sky_row - coast[i][2];
            rect_fill();
        }
    }
    /* 白波は海一面(線路の真下も)に 20 個(truss.s の sea_dots)。海は一色なので、白波が少ないと枕木の隙間の海も枕木と一緒に
     * 流れて見え、下の方にあると分からなかった(2026-10-16 ユーザーの指摘)。同じ画面の高さでは、海は枕木の約 1/20 の速さで流れる */
    sw_m = (u8)(back ? m : -m);
    sw_row = sky_row;
    sea_dots();
    /* ずっと下の海に浮かぶ小舟 2 艘(長さ 6m の帆船。小さく見えるので、橋の高さが分かる。Night Striker の海の小舟にならう) */
    for (i = 0; i < 2; i++) {
        u8 zi = (u8)(i * 101 + 40 + (back ? m : -m));
        i16 inv = (i16)wave_inv[zi];
        i16 row = sky_row + ((48 * inv) >> 8);
        i16 col = ((40 + i * 24) * inv) >> 8;            /* 左右 10m・16m */
        i16 w = (24 * inv) >> 8;                         /* 6m */
        if (w < 2) w = 2;
        if (row >= sky_row + 8 || row < 2) continue;     /* 遠くにある間だけ(近いと白い点が散って見える) */
        col = i ? 32 + col : 32 - col - w;
        if (col < 0 || col + w > 64) continue;
        g_color = C_ROCK0;                               /* 船体 */
        g_rx = (u8)col; g_ry = (u8)row; g_rw = (u8)w; g_rh = 1;
        rect_fill();
        g_color = C_WHITE;                               /* 帆 */
        g_rx = (u8)(col + (w >> 2)); g_rw = (u8)((w + 1) >> 1); g_ry = (u8)(row - 2); g_rh = 2;
        rect_fill();
    }
    if (sky_row >= 3 && sky_row + 2 < GAME_ROWS) {
        lighthouse(m, back);
        if (!back) harbor(m);
    }
}
#endif

#if HAS_HOME
/* 村の遠くの山並み(地平線の上。横位置・幅・高さのブロック) */
static const u8 mount[][3] = { { 0, 14, 2 }, { 4, 6, 4 }, { 10, 10, 3 }, { 24, 8, 2 }, { 38, 12, 3 }, { 44, 6, 5 }, { 52, 12, 2 }, { 58, 6, 3 } };
#endif
#if HAS_TOWN
/* 運河の両岸の遠くの家並み(地平線の上。横位置・幅・高さのブロック。上の 1 行は屋根) */
static const u8 bank_house[][3] = { { 0, 6, 3 }, { 6, 5, 4 }, { 11, 7, 3 }, { 18, 4, 2 }, { 42, 4, 2 }, { 46, 6, 4 }, { 52, 7, 3 }, { 59, 5, 4 } };
#endif

/* 地平線より下の地面の色。森(小川・倒木・祠も)は茂み、運河は水面、空き地・遊園地・村・田んぼは草(鉄橋では海)、ほかは道や岩 */
static u8 ground_col(u8 far)
{
    if (far == K_FOREST || far == K_STREAM || far == K_HOLLOW || far == K_SHRINE) return C_DIRT0;
    if (far == K_CANAL) return C_CYAN;
    if ((far >= K_PARK && far <= K_GLADE) || far == K_HOME || far == K_PADDY || far == K_MILL || far >= K_STALLS) return C_GRASS;
    return C_ROCK1;
}

/* 空と地面(地平線の高さで 2 色に塗り分ける)。far = 一番遠くに見える地形 */
static void sky_draw(u8 far)
{
    i16 yh = SCR_CY;
    u8 row;
    if (cam_pcos > 16) yh += muldiv(PROJ_F, cam_psin, cam_pcos);
    else if (cam_psin < 0) yh = -1;
    row = (yh < 0) ? 0 : (yh >= 48 * 16) ? 48 : (u8)(yh >> 4);
    sky_row = row;
    g_rh = row;
    buf_fill_rows(C_SKY, 0);
    g_rh = 48 - row;
    buf_fill_rows(ground_col(far), row);
#if HAS_HOME || HAS_TOWN
    if (row >= 6 && row < GAME_ROWS) {
        u8 i;
#if HAS_HOME
        if (far == K_HOME || far == K_PADDY || far == K_MILL) {
            g_color = C_ROCK0;
            for (i = 0; i < sizeof(mount) / 3; i++) {
                g_rx = mount[i][0]; g_rw = mount[i][1]; g_rh = mount[i][2]; g_ry = row - mount[i][2];
                rect_fill();
            }
        }
#endif
#if HAS_TOWN
        if (far == K_CANAL) {
            for (i = 0; i < sizeof(bank_house) / 3; i++) {
                g_rx = bank_house[i][0]; g_rw = bank_house[i][1];
                g_color = (i & 1) ? C_DIRT1 : C_ROCK2; g_rh = bank_house[i][2] - 1; g_ry = row - g_rh;
                rect_fill();
                g_color = C_DIRT0; g_rh = 1; g_ry = row - bank_house[i][2];
                rect_fill();
            }
        }
#endif
    }
#endif
}

void envm_draw(u16 s, u16 look16)
{
    u8 look = (u8)look16;
    /* look: 視線の相対ヨー(0 = 前、128 = 後ろ) */
    u8 fwd = !(look > 80 && look < 176);
    u8 back = (look > 48 && look < 208);
    i8 r, k, rn;
    u8 capped = 1, vok;
    /* 見える距離(区間の数)。振り向きの最中(前後の両方を描く数フレーム)は、前後とも 12m までにする
     * (画面が大きく回っている間なので、遠くが欠けても目立たない。1 フレームが一番重くなる所) */
    i8 lf = NF, lb = NB;
    if (fwd && back) lf = lb = TURN_K;
    /* 後ろだけを見ているとき: カメラは区間の途中にいるので、後ろのリングは前より 1 本ぶん遠くまである(前は 16〜20m 先まで、
     * 後ろは 20〜24m 後ろまで)。前と同じ距離までにそろえる */
    else if (back) lb = NB - 1;

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
        u8 open = 0, far = K_CAVE, canal = 0;
        for (r = back ? -lb : 0; r <= (fwd ? lf : 1); r++) {
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
                if (KIND[no] == K_CANAL) canal = 1;
            }
        }
        if (open) {
            /* 屋外が見える: 空と地面を塗ってから描く(奥の開口部は塞がない) */
            sky_draw(canal ? K_CANAL : far);   /* 運河が見えている間は、その先が町でも地平線の下を水面に */
#if HAS_BRIDGE
            if (BRIDGEISH(far) && fwd != back) sea_waves(s, back);
#endif
            capped = 0;
        } else {
            /* トンネルの内側からは壁が視界を全部覆うので、奥の開口部を黒く塞げば
             * バッファの全消去は要らない。レールの端で塞げないときだけ全消去する */
            if (fwd && !cap_ok(lf)) capped = 0;
            if (back && !cap_ok(-lb)) capped = 0;
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
        /* 仮リングを先に作る(鉄橋のレールを 1 枚で描けるかを、区間を描く前に決める) */
        vok = vring_make(sv);
        if (!vok) ring_late();
#if HAS_BRIDGE
        br_long = vok && BRIDGEISH(KIND[ring_no[VR]]);
#endif
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
        if (vok) {
#if HAS_BRIDGE
            if (br_long) handrail_draw(1);
#endif
            band_draw(VR, (u8)(rn + NB), 0);
            frame_draw(rn);
#if HAS_BRIDGE
            if (br_long) bridge_rails(1);
#endif
        } else {
            for (k = rn - 1; k >= 0; k--) { seg_draw(k); frame_draw(k + 1); }
        }
#if HAS_BRIDGE
        br_long = 0;
#endif
        return;
    }
    if (back && !fwd && s >= VR_D + 6) {
        u16 sv = s - VR_D;
        i8 kn = -FAR_K;
        if (capped) {
            for (k = -FAR_K; k > -lb; k--) if (!far_ok(k)) break;
            if (k == -lb) {
                for (k = -FAR_K; k > -lb; k--) hex_draw(k, C_ROCK0);
            } else {
                kn = -lb;
            }
            hex_draw(-lb, cap_col(-lb));
        } else {
            kn = -lb;
        }
        rn = ((u16)ring_m0 * 64 + 6 < sv) ? 0 : -1;
        vok = vring_make(sv);
        if (!vok) ring_late();
#if HAS_BRIDGE
        br_long = vok && BRIDGEISH(KIND[ring_no[VR]]);
#endif
        for (k = kn; k < rn; k++) {
            if (k == -lb && kn == -lb && k + 2 <= rn && merge_ok(-lb)) {
                band_draw((u8)(NB - lb), (u8)(NB - lb + 2), 3);  /* 遠い 2 区間は 1 つの帯で(前と同じ) */
                frame_draw(-lb);
                frame_draw(-lb + 1);
                k++;
                continue;
            }
            seg_draw(k); frame_draw(k);
        }
        if (vok) {
#if HAS_BRIDGE
            if (br_long) handrail_draw(-1);
#endif
            band_draw((u8)(rn + NB), VR, 0);
            frame_draw(rn);
#if HAS_BRIDGE
            if (br_long) bridge_rails(-1);
#endif
        } else {
            for (k = rn; k <= 0; k++) { seg_draw(k); frame_draw(k); }
        }
#if HAS_BRIDGE
        br_long = 0;
#endif
        return;
    }
    if (capped) {
        if (fwd) hex_draw(lf, cap_col(lf));
        if (back) hex_draw(-lb, cap_col(-lb));
    }

    /* 振り向き中(と後方の端): 前後の区間を距離の遠い順に交互に描き、カメラを含む区間はクリップ */
    for (k = lb > lf ? lb : lf; k >= 1; k--) {
        if (fwd && k < lf) { seg_draw(k); frame_draw(k + 1); }
        if (back && k <= lb) { seg_draw(-k); frame_draw(-k); }
    }
    seg_draw(0);
    frame_draw(1);
    frame_draw(0);
}
