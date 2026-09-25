/* game.c — プレイヤー・敵・当たり判定・ステージスクリプト(仕様書 2〜5 章)
 *
 * ゲームロジックは描画フレームごとに 1 回(目標 30fps = 2 ティック)。処理落ちしたら
 * ロジックも遅くなる(仕様書 9 章ルール 6)。
 *
 * 位置はすべてレール上の距離 s(1/64m)で持つ。速度 v は「1/64m の 1/256」/フレーム。
 */
#include "game.h"
#include "env.h"
#include "hud.h"
#include "sound.h"
#include "trolley.h"

#define M(x)    ((u16)((x) * 64))               /* m → s 単位 */
#define MPS(x)  ((i16)((x) * 546))              /* m/秒 → v 単位(30fps) */
#define F88(x)  ((i16)((x) * 256))
#define FLOOR_Y F88(-1.3)                       /* 目の高さから見た床 */
#define FLY_Y   F88(-0.3)                       /* コウモリ人の飛ぶ高さ */

/* ---------------------------------------------------------------------------
 * 敵モデル(単位 1/16m)
 * --------------------------------------------------------------------------- */
static const MRect m_e01[] = {          /* 猿人(斧)・茶 */
    { -4,  0, -1,  8, C_BROWN, 2 }, {  1,  0,  4,  8, C_BROWN, 2 },
    { -5,  8,  5, 17, C_BROWN, 0 }, { -3, 17,  3, 23, C_BROWN, 1 },
    { -2, 19,  2, 20, C_BLACK, 1 },
    {  5,  8,  6, 21, C_BLACK, 0 }, {  6, 16, 10, 21, C_BLACK, 0 },
};
static const MRect m_e02a[] = {         /* コウモリ人・紫(翼上げ) */
    { -3,  0,  3,  8, C_PURPLE, 0 }, { -2,  8,  2, 12, C_PURPLE, 1 },
    { -2, 12, -1, 14, C_PURPLE, 1 }, {  1, 12,  2, 14, C_PURPLE, 1 },
    { -12, 5, -3, 10, C_BLACK, 0 },  {  3,  5, 12, 10, C_BLACK, 0 },
};
static const MRect m_e02b[] = {         /* コウモリ人・紫(翼下げ) */
    { -3,  0,  3,  8, C_PURPLE, 0 }, { -2,  8,  2, 12, C_PURPLE, 1 },
    { -2, 12, -1, 14, C_PURPLE, 1 }, {  1, 12,  2, 14, C_PURPLE, 1 },
    { -11, 1, -3,  5, C_BLACK, 0 },  {  3,  1, 11,  5, C_BLACK, 0 },
};
static const MRect m_b01[] = {          /* 大猿人の親方・茶+黒 */
    { -9,  0, -3, 14, C_BROWN, 2 },  {  3,  0,  9, 14, C_BROWN, 2 },
    { -11, 14, 11, 34, C_BROWN, 0 }, { -11, 22, 11, 25, C_BLACK, 0 },
    { -6, 34,  6, 45, C_BROWN, 1 },  { -4, 39, -1, 41, C_BLACK, 1 },
    {  1, 39,  4, 41, C_BLACK, 1 },  { -5, 34,  5, 36, C_BLACK, 1 },
    { 11, 14, 13, 44, C_BLACK, 0 },  { 13, 32, 22, 44, C_BLACK, 0 },
};
static const MRect m_treasure[] = {     /* 宝箱・黄 */
    { -6, 0, 6, 6, C_YELLOW, 0 }, { -6, 6, 6, 9, C_BROWN, 0 },
    { -1, 4, 1, 6, C_BLACK, 0 },
};
static const MRect m_medkit[] = {       /* 救急箱・赤+白の十字 */
    { -4, 0, 4, 7, C_RED, 0 }, { -1, 1, 1, 6, C_WHITE, 0 }, { -3, 2, 3, 5, C_WHITE, 0 },
};
#define MODEL(a) { a, sizeof(a) / sizeof(a[0]) }
static const Model models[] = {
    { 0, 0 }, MODEL(m_e01), MODEL(m_e02a), { 0, 0 }, { 0, 0 }, { 0, 0 },
    MODEL(m_b01), MODEL(m_treasure), MODEL(m_medkit),
};
static const Model model_e02b = MODEL(m_e02b);

/* 敵種ごとの定数 */
static const u8 et_hp[ET_COUNT] = { 0, 1, 2, 3, 2, 3, 30, 0, 1 };

/* ---------------------------------------------------------------------------
 * 状態
 * --------------------------------------------------------------------------- */
enum { ST_IDLE, ST_APPROACH, ST_ATTACK, ST_PASS, ST_DEAD };

typedef struct {
    u8  type, state, hp, flash;
    u8  timer, attacks, anim, side;
    u16 s;
    u8  sub;
    i16 v;
    i16 lat, lat_t, up;
    V3  c;
} Enemy;

#define MAX_EN 10
static Enemy en[MAX_EN];
static u8 draw_order[MAX_EN];

/* 当たり判定用: 直前に描いた矩形(ブロック座標) */
typedef struct { u8 x0, y0, x1, y1; i16 z; u8 id; } HitRect;
#define MAX_HIT 48
static HitRect hits[MAX_HIT];
static u8 n_hits;

u16 cam_s;
static u8  cam_sub;
u16 cam_v;
static u8  hold;                 /* 1 = トロッコ停止(発進前) */
static u16 look;                 /* 視線の相対ヨー(0 前 / 0x8000 後ろ) */
u8  facing;               /* 0 前 / 1 後ろ */
static u8  turning;              /* 振り向きの残りフレーム */
static u16 aim_x, aim_y;         /* 照準(ピクセル) */
static u8  aim_hold;
static u8  in_prev;
static u8  fire_cd;
static u8  hitmark;
u8  life;
u8  attack_dir;
u8  boss_alive;
static u8  blink;
static u8  shake;                /* 被弾でトロッコを揺らす残りフレーム */
u16 sc_tick;
static const Event *sc_ev;
static u8  sc_end;
u16 g_score;
u16 g_perf_logic[4];

/* ---------------------------------------------------------------------------
 * 敵
 * --------------------------------------------------------------------------- */
static Enemy *spawn(u8 type, i8 dist, i8 lat8)
{
    u8 i;
    Enemy *e;
    i16 s;
    for (i = 0; i < MAX_EN; i++) {
        e = &en[i];
        if (e->type == ET_NONE) break;
    }
    if (i == MAX_EN) return 0;
    s = (i16)cam_s + (i16)dist * 64;
    if (s < 0) s = 0;
    e->type = type;
    e->state = ST_IDLE;
    e->hp = et_hp[type];
    e->flash = 0;
    e->timer = 0;
    e->attacks = 0;
    e->anim = i * 5;
    e->side = lat8 < 0;
    e->s = (u16)s;
    e->sub = 0;
    e->v = 0;
    e->lat = e->lat_t = (i16)lat8 << 5;       /* 1/8m → 8.8 */
    e->up = FLOOR_Y;                           /* 地上の敵は床に立つ */
    if (type == ET_E02) {
        e->up = FLY_Y;
        e->state = ST_APPROACH;
        e->lat_t = e->side ? F88(-0.5) : F88(0.5);
    } else if (type == ET_B01) {
        e->state = ST_APPROACH;
        e->lat_t = 0;
        boss_alive = 1;
    }
    return e;
}

static void damage_player(void)
{
    if (!life) return;
    life--;
    shake = 4;
    flash_palette();
    se_play(SE_DAMAGE);
}

static void move_enemy(Enemy *e)
{
    i16 v = e->v;
    u16 lo = (u16)e->sub + (u8)v;
    e->sub = (u8)lo;
    e->s += (u16)((v >> 8) + (i16)(lo >> 8));
    if (e->lat < e->lat_t) {
        e->lat += 20;
        if (e->lat > e->lat_t) e->lat = e->lat_t;
    } else if (e->lat > e->lat_t) {
        e->lat -= 20;
        if (e->lat < e->lat_t) e->lat = e->lat_t;
    }
}

/* 近接攻撃の周期: 0.5 秒の予備動作(点滅)の後に 1 ダメージ、周期 period フレーム */
static void melee(Enemy *e, u8 period)
{
    e->timer++;
    if (e->timer < 15) {
        e->flash = (e->timer & 2) ? 1 : 0;
    } else if (e->timer == 15) {
        damage_player();
    } else if (e->timer >= period) {
        e->timer = 0;
        e->attacks++;
    }
}

static void update_enemy(Enemy *e)
{
    i16 d = (i16)(e->s - cam_s);
    e->anim++;
    if (e->flash && e->state != ST_ATTACK) e->flash--;

    switch (e->state) {
    case ST_DEAD:
        e->up -= 24;
        move_enemy(e);
        if (++e->timer >= 12) {
            if (e->type == ET_B01) boss_alive = 0;
            e->type = ET_NONE;
        }
        return;
    case ST_IDLE:
        if (e->type == ET_E01 && d > 0 && d < (i16)M(14)) {
            e->state = ST_APPROACH;
            e->v = -MPS(2.5);
            e->lat_t = e->side ? F88(-0.35) : F88(0.35);
        }
        break;
    case ST_APPROACH:
        if (e->type == ET_E01) {
            if (d > 0 && d <= (i16)M(2)) { e->state = ST_ATTACK; e->timer = 0; }
        } else if (e->type == ET_E02) {
            /* 後方(または前方)から追跡してきて 2.2m に張り付く */
            if (d < -(i16)M(2.2) + 0) e->v = (i16)cam_v + MPS(3);
            else if (d > (i16)M(2.2)) e->v = (i16)cam_v - MPS(3);
            else { e->state = ST_ATTACK; e->timer = 0; }
            e->up = FLY_Y + ((e->anim & 8) ? 24 : 0);
        } else if (e->type == ET_B01) {
            e->v = (i16)cam_v - MPS(2);
            if (d <= (i16)M(3.5)) { e->state = ST_ATTACK; e->timer = 0; }
        }
        break;
    case ST_ATTACK:
        if (e->type == ET_E01) {
            e->v = (i16)cam_v;
            melee(e, 60);
            if (e->attacks >= 2) { e->state = ST_PASS; e->v = 0; e->flash = 0; }
        } else if (e->type == ET_E02) {
            i16 tgt = (d < 0) ? -(i16)M(2.2) : (i16)M(2.2);
            e->v = (i16)cam_v + ((tgt - d) << 3);
            e->up = FLY_Y + ((e->anim & 8) ? 24 : 0);
            melee(e, 60);
        } else if (e->type == ET_B01) {
            e->v = (i16)cam_v + (((i16)M(3.5) - d) << 3);
            e->lat_t = (e->anim & 64) ? F88(0.6) : F88(-0.6);
            melee(e, 45);
            if ((e->anim & 127) == 0) spawn(ET_E01, 14, (e->anim & 128) ? 10 : -10);
        }
        break;
    }
    move_enemy(e);

    /* カメラの後方へ抜けた敵を消す(追跡型を除く) */
    if (e->type != ET_E02 && e->type != ET_B01) {
        if (d < -(i16)M(3)) e->type = ET_NONE;
    }
    if (d > (i16)M(60) || d < -(i16)M(40)) e->type = ET_NONE;
}

static void kill_enemy(Enemy *e)
{
    u8 t = e->type;
    if (t == ET_MEDKIT) {
        life += 2;
        if (life > 8) life = 8;
        e->type = ET_NONE;
        se_play(SE_HEAL);
        return;
    }
    g_score += (t == ET_B01) ? et_hp[t] * 20 : et_hp[t] * 10;
    e->state = ST_DEAD;
    e->timer = 0;
    e->flash = 12;
    e->v = (i16)cam_v;
    se_play(SE_KILL);
}

/* ---------------------------------------------------------------------------
 * 射撃(ヒットスキャン。照準ブロックに重なる矩形のうち最も手前)
 * --------------------------------------------------------------------------- */
static void fire(void)
{
    u8 bx = (u8)(aim_x >> 2), by = (u8)(aim_y >> 2);
    u8 i, best = 0xFF;
    i16 bz = 32767;
    se_play(SE_SHOT);
    if (trolley_covers(bx, by)) return;    /* トロッコの縁に隠れた所は撃てない */
    for (i = 0; i < n_hits; i++) {
        HitRect *h = &hits[i];
        if (bx >= h->x0 && bx < h->x1 && by >= h->y0 && by < h->y1 && h->z < bz) {
            bz = h->z;
            best = h->id;
        }
    }
    if (best >= MAX_EN) return;
    {
        Enemy *e = &en[best];
        if (e->type == ET_NONE || e->state == ST_DEAD) return;
        hitmark = 4;
        if (e->hp) e->hp--;
        if (!e->hp) {
            kill_enemy(e);
        } else {
            e->flash = 2;
            se_play(SE_HIT);
        }
    }
}

/* ---------------------------------------------------------------------------
 * スクリプト
 * --------------------------------------------------------------------------- */
static void script_update(void)
{
    if (boss_alive) return;              /* ボス戦中は進行を止める */
    sc_tick += 2;
    while (!sc_end && sc_ev->tick <= sc_tick) {
        const Event *ev = sc_ev++;
        switch (ev->cmd) {
        case EV_SPAWN: spawn(ev->a, ev->b, ev->c); break;
        case EV_DIR:   attack_dir = ev->a; break;
        case EV_GO:    hold = 0; break;
        case EV_BOSS:  spawn(ev->a, ev->b, 0); break;
        case EV_END:   sc_end = 1; break;
        }
    }
}

/* ---------------------------------------------------------------------------
 * 1 フレームのロジック
 * --------------------------------------------------------------------------- */
static void player_update(void)
{
    u8 in = input_raw();
    u8 edge = in & ~in_prev;
    u8 step;
    in_prev = in;

    /* 照準: 2 ピクセル/ティック、8 ティック押し続けたら 4 ピクセル/ティック(1 フレーム = 2 ティック) */
    if (in & (IN_UP | IN_DOWN | IN_LEFT | IN_RIGHT)) {
        if (aim_hold < 4) aim_hold++;
    } else {
        aim_hold = 0;
    }
    step = (aim_hold >= 4) ? 8 : 4;
    if (in & IN_LEFT)  aim_x = (aim_x > step) ? aim_x - step : 0;
    if (in & IN_RIGHT) aim_x = (aim_x + step < 255) ? aim_x + step : 255;
    if (in & IN_UP)    aim_y = (aim_y > step) ? aim_y - step : 0;
    if (in & IN_DOWN)  aim_y = (aim_y + step < GAME_ROWS * 4 - 1) ? aim_y + step : GAME_ROWS * 4 - 1;

    /* 振り向き: B ボタンでトグル(押した瞬間のみ)。8 フレームで 180 度、回転中は射撃不可 */
    if (turning) {
        look += 0x1000;
        if (--turning == 0) look = facing ? 0x8000 : 0;
    } else if (edge & IN_B) {
        facing ^= 1;
        turning = 8;
    }

    if (fire_cd) fire_cd--;
    if ((in & IN_A) && !fire_cd && !turning) {
        fire();
        fire_cd = 3;                     /* 6 ティック */
    }
    if (hitmark) hitmark--;
}

static void trolley_update(void)
{
    u16 target = hold ? 0 : (u16)rail_spd[cam_s >> 6] * 137;
    u16 t;
    if (cam_v < target) {
        cam_v += 48;
        if (cam_v > target) cam_v = target;
    } else if (cam_v > target) {
        cam_v = (cam_v > target + 48) ? cam_v - 48 : target;
    }
    t = (u16)cam_sub + (cam_v & 0xFF);
    cam_sub = (u8)t;
    cam_s += (cam_v >> 8) + (t >> 8);
    if (cam_s >= rail_s_max()) { cam_s = rail_s_max(); cam_v = 0; }
}

/* ---------------------------------------------------------------------------
 * 描画
 * --------------------------------------------------------------------------- */
static void draw_enemy(u8 id)
{
    Enemy *e = &en[id];
    const Model *md;
    const MRect *r;
    i16 sx, sy, k;
    u8 i, hit_ok;
    if (e->c.z < NEAR_Z + 32) return;
    md = (e->type == ET_E02 && (e->anim & 4)) ? &model_e02b : &models[e->type];
    if (!project(&e->c, &sx, &sy)) return;
    k = muldiv(8192, 256, e->c.z);           /* 1/16m あたりの画面量(12.4)の 8.8 */
    hit_ok = e->type != ET_TREASURE && e->state != ST_DEAD;
    for (i = 0, r = md->r; i < md->n; i++, r++) {
        if (!rect_clip(sx + fmul(r->x0, k), sy - fmul(r->y1, k),
                       sx + fmul(r->x1, k), sy - fmul(r->y0, k))) continue;
        g_color = e->flash ? C_WHITE : r->color;
        rect_fill();
        if (hit_ok && n_hits < MAX_HIT) {
            HitRect *h = &hits[n_hits++];
            h->x0 = g_rx; h->y0 = g_ry;
            h->x1 = g_rx + g_rw; h->y1 = g_ry + g_rh;
            h->z = e->c.z;
            h->id = id;
        }
    }
}

static void draw_enemies(void)
{
    u8 i, j, n = 0, t;
    for (i = 0; i < MAX_EN; i++) {
        Enemy *e = &en[i];
        if (e->type == ET_NONE) continue;
        rail_cam(e->s, e->lat, e->up, &e->c);
        if (e->c.z < NEAR_Z + 32) continue;
        draw_order[n++] = i;
    }
    /* 奥から(z の大きい順) */
    for (i = 1; i < n; i++) {
        t = draw_order[i];
        for (j = i; j > 0 && en[draw_order[j - 1]].c.z < en[t].c.z; j--)
            draw_order[j] = draw_order[j - 1];
        draw_order[j] = t;
    }
    n_hits = 0;
    for (i = 0; i < n; i++) draw_enemy(draw_order[i]);
}

static void set_sprite(u8 n, i16 x, i16 y, u8 pat, u8 color)
{
    u8 *a = &g_spr_attr[n * 4];
    if (y < -16 || y >= 192) { a[0] = 209; return; }
    if (x < 0) { x += 32; color |= 0x80; }   /* EC: 32 ピクセル左へ */
    a[0] = (u8)(y - 1);
    if (a[0] == 208) a[0] = 207;
    a[1] = (u8)x;
    a[2] = pat;
    a[3] = color;
}

static void draw_hud(void)
{
    u8 warn = 0;
    hud_life(life);
    if (attack_dir == DIR_BACK && !facing) warn = 1;
    if (attack_dir == DIR_FRONT && facing) warn = 2;
    if (warn) {
        if (blink < 8) text_center(1, warn == 1 ? "ENEMY BEHIND!" : "ENEMY AHEAD!", C_RED, 1);
    } else {
        num_draw(40, 1, g_score, 5, C_WHITE);
        if (g_score) text_draw(60, 1, "0", C_WHITE, 1);
    }
    if (++blink >= 16) blink = 0;       /* 約 15 ティック点灯 / 15 ティック消灯 */

    set_sprite(0, (i16)aim_x - 16, (i16)aim_y - 16, 0, C_WHITE);
    if (hitmark) set_sprite(1, (i16)aim_x - 16, (i16)aim_y - 16, 4, C_YELLOW);
    else g_spr_attr[4] = 209;
}

static void render(void)
{
    V3 p;
    u16 yaw;
    u16 t0;
    timer_reset();
    t0 = timer_read();
    rail_at(cam_s, &p, &yaw);
    render_set_camera(p.x, p.y, p.z, yaw + look);
    env_draw(cam_s, (u8)(look >> 8));
    g_perf_env = timer_read() - t0;
    draw_enemies();
    /* トロッコの縁。振り向き中は横へ滑らせて一度画面外へ出し、反対側から戻す */
    trolley_set_life(life);
    {
        i16 slide = turning ? fmul(SIN((u8)(look >> 8)), 72) : 0;   /* 90 度で 72 ブロック = 画面外 */
        if (slide > 64) slide = 64; else if (slide < -64) slide = -64;
        trolley_draw((i8)slide, (shake & 2) ? 1 : 0);
    }
    if (shake) shake--;
    draw_hud();
    g_perf_render = timer_read() - t0;
}

/* ---------------------------------------------------------------------------
 * 1 プレイ
 * --------------------------------------------------------------------------- */
u8 game_run(const Stage *st)
{
    u8 i, over = 0;
    rail_build(st->track);
    set_stage_palette(st->palette);
    for (i = 0; i < MAX_EN; i++) en[i].type = ET_NONE;
    n_hits = 0;
    cam_s = 0; cam_sub = 0; cam_v = 0; hold = 1;
    look = 0; facing = 0; turning = 0;
    aim_x = 128; aim_y = 92; aim_hold = 0;
    in_prev = 0xFF;                      /* タイトルで押したボタンを射撃と見なさない */
    fire_cd = 0; hitmark = 0;
    life = 8;
    attack_dir = DIR_NONE;
    boss_alive = 0;
    blink = 0;
    shake = 0;
    sc_tick = 0; sc_ev = st->events; sc_end = 0;
    g_score = 0;
    g_scene = 1;

    for (;;) {
        u16 tl;
        timer_reset();
        tl = timer_read();
        player_update();
        g_perf_logic[0] = timer_read() - tl;
        trolley_update();
        g_perf_logic[1] = timer_read() - tl;
        script_update();
        g_perf_logic[2] = timer_read() - tl;
        for (i = 0; i < MAX_EN; i++) {
            if (en[i].type != ET_NONE) update_enemy(&en[i]);
            }
        g_perf_logic[3] = timer_read() - tl;
        render();
        present();
        if (!life) { over = 1; break; }
        if (sc_end && !boss_alive) break;
    }

    /* 結果表示(約 4 秒) */
    g_scene = over ? 2 : 3;
    g_spr_attr[0] = 209;
    g_spr_attr[4] = 209;
    for (i = 0; i < 120; i++) {
        buf_fill(C_BLACK, BUF);
        if (over) {
            if (i & 16) text_center(12, "GAME", C_RED, 2);
            if (i & 16) text_center(24, "OVER", C_RED, 2);
        } else {
            text_center(8, "CAVE CLEAR", C_YELLOW, 1);
            text_center(18, "SCORE", C_WHITE, 1);
            num_draw(18, 26, g_score, 5, C_WHITE);
            if (g_score) text_draw(38, 26, "0", C_WHITE, 1);
            text_center(36, "TO BE CONTINUED", C_CYAN, 1);
        }
        present();
    }
    return !over;
}
