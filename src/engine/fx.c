/* fx.c — 画面の演出(常駐部): 数字の表示、飛び散る車体の破片、分岐の選ばれていない側の線路
 *
 * ゲームモジュール(ページ2)の置き場を空けるため、モジュールの状態に触らない演出はここ(常駐部)に置く。
 */
#include "game.h"
#include "hud.h"
#include "trolley.h"
#include "fx.h"

#define M(x)    ((u16)((x) * 64))
#define F88(x)  ((i16)((x) * 256))
#define FLOOR_Y F88(-1.3)

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

/* 被弾で車両から飛び散る破片(画面座標 8.8 ブロック)。車体の赤・メッキの白・黒の 3 色 */
#define N_DEBRIS 6
static i16 deb_x[N_DEBRIS], deb_y[N_DEBRIS], deb_vx[N_DEBRIS], deb_vy[N_DEBRIS];
static u8  deb_t;
static const u8 deb_col[3] = { C_RED, C_WHITE, C_BLACK };

void debris_start(void)
{
    u8 i, r = (u8)g_tick;
    for (i = 0; i < N_DEBRIS; i++) {
        r = r * 5 + 7;
        deb_x[i] = F88(32) + ((i16)(r & 31) - 16) * 64;
        deb_y[i] = F88(TROLLEY_Y + 4);
        deb_vx[i] = ((i16)(r & 63) - 32) * 12;
        deb_vy[i] = -F88(1.2) - (i16)(r & 7) * 24;
    }
    deb_t = 14;
}

void debris_draw(void)
{
    u8 i;
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
    g_alt_n = 0;
}

RailPt g_alt[ALT_N];
u8  g_alt_n;

/* 選ばれていない側の線路(分岐点から 24m): レール 2 本と枕木を、地形の上に描く */
static void alt_pt(const RailPt *p, i16 lat, V3 *o)
{
    u8 h = p->yaw >> 8;
    xform(p->x + fmul(lat, COS(h)), p->y + FLOOR_Y, p->z - fmul(lat, SIN(h)), o);
}

static void alt_quad(const RailPt *a, const RailPt *b, i16 l0, i16 l1, u8 color)
{
    V3 q[4];
    alt_pt(a, l0, &q[0]);
    alt_pt(a, l1, &q[1]);
    alt_pt(b, l1, &q[2]);
    alt_pt(b, l0, &q[3]);
    poly3(q, 4, color);
}

void alt_draw(void)
{
    u8 i;
    u16 f = rail_fork_m() << 6;
    if (!g_alt_n || cam_s + M(40) < f || cam_s > f + M(30)) return;
    for (i = g_alt_n - 1; i > 0; i--) {                /* 奥から */
        const RailPt *a = &g_alt[i - 1], *b = &g_alt[i];
        RailPt m;                                    /* 枕木: 区間の頭から 1/8 */
        m.x = a->x + ((b->x - a->x) >> 3);
        m.y = a->y + ((b->y - a->y) >> 3);
        m.z = a->z + ((b->z - a->z) >> 3);
        m.yaw = a->yaw;
        alt_quad(a, &m, -180, 180, 9);
        alt_quad(a, b, -128, -102, 10);
        alt_quad(a, b, 102, 128, 10);
    }
}

