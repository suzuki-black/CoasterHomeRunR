/* fx.c — 画面の演出(常駐部): 数字の表示、飛び散る金貨、分岐の選ばれていない側の線路
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
 *   宝が増えた  黄  金貨の印 +「+2」(満杯 FULL)  宝の山の上        上へ流れる
 *   宝が減った  赤  金貨の印 +「-1」             宝の山の上        その場で点滅 */
#define N_POP 4
static u8  pop_x[N_POP], pop_y[N_POP], pop_t[N_POP], pop_k[N_POP];
static u16 pop_v[N_POP];
u8  g_pop_hx, g_pop_hy;          /* 撃った敵の画面上の位置(game.c の fire が決める) */
#define PILE_POP_Y (TROLLEY_Y - 6)

void popup(u8 kind, u16 v)
{
    u8 i, k = 0;
    for (i = 1; i < N_POP; i++) if (pop_t[i] < pop_t[k]) k = i;
    pop_k[k] = kind; pop_v[k] = v; pop_t[k] = 16;
    pop_x[k] = (kind == POP_SCORE) ? g_pop_hx : 32;
    pop_y[k] = (kind == POP_SCORE) ? g_pop_hy : PILE_POP_Y;
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
                /* 金貨の印(黄の 2×2)を数字の左に */
                x += 2;
                if (x >= 3 && y >= 0) { g_rx = (u8)x - 3; g_ry = (u8)y + 1; g_rw = 2; g_rh = 2; g_color = C_YELLOW; rect_fill(); }
            }
            text_draw(x, y, (j == 6) ? "FULL" : &s[j], c, 1);
        }
    }
}

/* 被弾で宝の山から飛び散る金貨(画面座標 8.8 ブロック) */
#define N_COIN 6
static i16 coin_x[N_COIN], coin_y[N_COIN], coin_vx[N_COIN], coin_vy[N_COIN];
static u8  coin_t;

void coins_start(void)
{
    u8 i, r = (u8)g_tick;
    for (i = 0; i < N_COIN; i++) {
        r = r * 5 + 7;
        coin_x[i] = F88(32) + ((i16)(r & 31) - 16) * 64;
        coin_y[i] = F88(TROLLEY_Y + 2);
        coin_vx[i] = ((i16)(r & 63) - 32) * 12;
        coin_vy[i] = -F88(1.2) - (i16)(r & 7) * 24;
    }
    coin_t = 14;
}

void coins_draw(void)
{
    u8 i;
    if (!coin_t) return;
    coin_t--;
    for (i = 0; i < N_COIN; i++) {
        i16 x = coin_x[i] >> 8, y = coin_y[i] >> 8;
        coin_x[i] += coin_vx[i];
        coin_y[i] += coin_vy[i];
        coin_vy[i] += 64;
        if (x >= 0 && x < 64 && y >= 0 && y < GAME_ROWS) {
            g_rx = (u8)x; g_ry = (u8)y; g_color = (i & 1) ? C_YELLOW : C_WHITE;
            pset();
        }
    }
}

void fx_reset(void)
{
    pop_t[0] = pop_t[1] = pop_t[2] = pop_t[3] = 0;
    coin_t = 0;
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

