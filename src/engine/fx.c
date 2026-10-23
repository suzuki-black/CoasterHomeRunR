/* fx.c — 画面の演出(常駐部): 数字の表示、飛び散る車体の破片、分岐の選ばれていない側の線路
 *
 * ゲームモジュール(ページ2)の置き場を空けるため、モジュールの状態に触らない演出はここ(常駐部)に置く。
 */
#include "game.h"
#include "hud.h"
#include "trolley.h"
#include "fx.h"

#define F88(x)  ((i16)((x) * 256))

/* 数字の表示(ルール: 色・形・場所・動きで 3 種類を分ける)
 *   得点        白  数字だけ「100」            倒した敵の位置    上へ流れる
 *   耐久が増えた  黄  十字の印 +「+2」(満杯 FULL)  車両の上   上へ流れる
 *   耐久が減った  赤  十字の印 +「-1」             車両の上   その場で点滅 */
#define N_POP 4
static u8  pop_x[N_POP], pop_y[N_POP], pop_t[N_POP], pop_k[N_POP];
static u16 pop_v[N_POP];
u8  g_pop_hx, g_pop_hy;          /* 撃った敵の画面上の位置(game.c の fire が決める) */
#define CAR_POP_Y (TROLLEY_Y - 4)

void popup(u8 kind, u16 v)
{
    u8 i, k = 0;
    for (i = 1; i < N_POP; i++) if (pop_t[i] < pop_t[k]) k = i;
    pop_k[k] = kind; pop_v[k] = v; pop_t[k] = 16;
    pop_x[k] = (kind == POP_SCORE) ? g_pop_hx : 32;
    pop_y[k] = (kind == POP_SCORE) ? g_pop_hy : CAR_POP_Y;
}

void popups_draw(void)
{
    u8 i, j, k, c;
    char s[7];
    for (i = 0; i < N_POP; i++) {
        u16 v;
        if (!pop_t[i]) continue;
        pop_t[i]--;
        k = pop_k[i];
        if (k == POP_LOSS) {
            if (pop_t[i] & 2) continue;              /* 点滅 */
        } else if ((pop_t[i] & 1) && pop_y[i] > 1) {
            pop_y[i]--;
        }
        v = pop_v[i];
        j = 6; s[6] = 0;
        do { s[--j] = (char)('0' + v % 10); v /= 10; } while (v);
        c = C_WHITE;
        if (k != POP_SCORE) {
            c = (k == POP_GAIN) ? C_YELLOW : C_RED;
            s[--j] = (k == POP_GAIN) ? '+' : '-';
            if (k == POP_GAIN && !pop_v[i]) j = 6;       /* 満杯で増えなかった */
        }
        {
            i8 x = (i8)pop_x[i] - (i8)((6 - j) * 2), y = (i8)pop_y[i];
            if (k != POP_SCORE) {
                /* 十字の印(白地に赤い十字の 3×3。救急箱の印)を数字の左に */
                x += 2;
                if (x >= 4 && y >= 0) {
                    g_rx = (u8)x - 4; g_ry = (u8)y + 1; g_rw = 3; g_rh = 3; g_color = C_WHITE; rect_fill();
                    g_rx = (u8)x - 3; g_rw = 1; g_color = C_RED; rect_fill();
                    g_rx = (u8)x - 4; g_ry = (u8)y + 2; g_rw = 3; g_rh = 1; rect_fill();
                }
            }
            text_draw(x, y, (j == 6) ? "FULL" : &s[j], c, 1);
        }
    }
}

/* 被弾で車両から飛び散る破片、倒した敵から飛び散る破片(画面座標 8.8 ブロック)。車体(敵)の色・白・黒の 3 色 */
#define N_DEBRIS 6
static i16 deb_x[N_DEBRIS], deb_y[N_DEBRIS], deb_vx[N_DEBRIS], deb_vy[N_DEBRIS];
static u8  deb_t;
static u8 deb_col[3] = { C_RED, C_WHITE, C_BLACK };

static void debris_go(i16 x, i16 y)
{
    u8 i, r = (u8)g_tick;
    for (i = 0; i < N_DEBRIS; i++) {
        r = r * 5 + 7;
        deb_x[i] = x + ((i16)(r & 31) - 16) * 64;
        deb_y[i] = y;
        deb_vx[i] = ((i16)(r & 63) - 32) * 12;
        deb_vy[i] = -F88(1.2) - (i16)(r & 7) * 24;
    }
    deb_t = 14;
}

void debris_start(void)
{
    deb_col[0] = C_RED;
    debris_go(F88(32), F88(TROLLEY_Y + 4));
}

void debris_burst(u8 x, u8 y, u8 c)
{
    deb_col[0] = c;
    debris_go((i16)x << 8, (i16)y << 8);
}

/* ボスを倒したときの爆発(2026-10-23 ユーザー: 「効果音とシンクロして謎の爆発の演出」)。効果音 SE_BOSS の「ばごん」(12 ティックごとに
 * 5 回)に合わせて、ボスのまわりの 5 か所で順に爆発させる。1 つの爆発は 16 ティック: 白い芯が縮み、8 つの火の玉の輪が広がりながら
 * 白 → 黄 → 橙 → 赤に変わる。爆発の始めに破片を飛ばし、画面を揺らす(g_fx_shake。render.c がカメラの揺れに足す)。
 * 描くのは常駐部なので、ゲームモジュールの置き場を使わない(game.c は boom_start を呼ぶだけ) */
#define BOOM_N 5
#define BOOM_STEP 12                     /* sndtick.c の se_boss の「ばごん」1 つの長さ(ティック) */
static const i8 boom_ofs[BOOM_N][2] = { { 0, 4 }, { -9, -2 }, { 8, 1 }, { -5, 10 }, { 6, 9 } };
static u8 boom_x, boom_y, boom_k, boom_on;
static u16 boom_t0;
u8 g_fx_shake;

void boom_start(u8 x, u8 y)
{
    boom_x = x; boom_y = y;
    boom_t0 = g_tick;
    boom_k = 0;
    boom_on = 1;
}

static void boom_blast(i16 cx, i16 cy, u8 s)
{
    u8 k, c = (s < 4) ? C_WHITE : (s < 8) ? C_YELLOW : (s < 12) ? C_ORANGE : C_RED;
    i16 r = 3 + s + (s >> 1), core = 6 - (s >> 1);
    u8 sz = 4 - (s >> 2);                    /* 火の玉の大きさ(4 → 1 ブロック) */
    for (k = 0; k < 8; k++) {                /* 火の玉の輪(少しずつ回りながら広がる) */
        u8 a = (u8)(k * 32 + s * 4);
        i16 x = cx + fmul(r, COS(a)), y = cy + fmul(r, SIN(a));
        if (x < 2 || x > 60 || y < 2 || y >= GAME_ROWS - 4) continue;
        g_rx = (u8)x - 2; g_ry = (u8)y - 2; g_rw = g_rh = sz; g_color = c;
        rect_fill();
    }
    if (core > 0 && cx >= core && cx + core < 64 && cy >= core && cy + core < GAME_ROWS) {   /* 白い芯 */
        g_rx = (u8)(cx - core); g_ry = (u8)(cy - core); g_rw = g_rh = (u8)(core * 2); g_color = C_WHITE;
        rect_fill();
    }
}

static void boom_draw(void)
{
    u16 t = g_tick - boom_t0;
    u8 k;
    if (g_fx_shake) g_fx_shake--;
    if (!boom_on) return;
    if (t >= BOOM_N * BOOM_STEP + 16) { boom_on = 0; return; }
    while (boom_k < BOOM_N && t >= (u16)boom_k * BOOM_STEP) {   /* 爆発の始め: 破片と揺れ */
        debris_burst((u8)(boom_x + boom_ofs[boom_k][0]), (u8)(boom_y + boom_ofs[boom_k][1]), (boom_k & 1) ? C_ORANGE : C_YELLOW);
        g_fx_shake = 4;
        boom_k++;
    }
    for (k = 0; k < BOOM_N; k++) {
        u16 s = t - (u16)k * BOOM_STEP;
        if (t < (u16)k * BOOM_STEP || s >= 16) continue;
        boom_blast((i16)boom_x + boom_ofs[k][0], (i16)boom_y + boom_ofs[k][1], (u8)s);
    }
}

void debris_draw(void)
{
    u8 i;
    boom_draw();                             /* ボスの爆発(破片より奥に) */
    if (!deb_t) return;
    deb_t--;
    for (i = 0; i < N_DEBRIS; i++) {
        i16 x = deb_x[i] >> 8, y = deb_y[i] >> 8;
        deb_x[i] += deb_vx[i];
        deb_y[i] += deb_vy[i];
        deb_vy[i] += 64;
        if (x >= 0 && x < 64 && y >= 0 && y < GAME_ROWS) {
            g_rx = (u8)x; g_ry = (u8)y; g_color = deb_col[i % 3];
            pset();
        }
    }
}

void fx_reset(void)
{
    pop_t[0] = pop_t[1] = pop_t[2] = pop_t[3] = 0;
    deb_t = 0;
    boom_on = 0;
    g_fx_shake = 0;
    g_alt_n = 0;
}

RailPt g_alt[ALT_N];
u8  g_alt_n;


