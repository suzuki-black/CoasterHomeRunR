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

/* 敵種ごとの定数 */
/* 敵の種類ごとの動き(仕様書 4 章)。新しい敵はここに 1 行足す。
 *   mv   動き方: WALK 歩いて寄り、届いたら張り付いて殴る(後ろに出たら走って追う) / HOVER 飛んで keep の距離に張り付く /
 *        STAND その場に立つ / KEEP ボス: keep の距離(負 = 後ろ)を保つ / SHOT 敵弾: カメラへ飛ぶ(keep = 速さ m/秒) / NONE 動かない
 *   up   立つ・飛ぶ高さ(1/16m、目の高さから)  keep 保つ・届く距離(1/4m)  gain 距離へ寄せる強さ(シフト量)
 *   sway 左右に揺れる幅(1/16m)と周期(anim のビット)  bob 上下に揺れる幅(1/256m)と周期
 *   atk  攻撃: MELEE 殴る / SHOOT 弾を撃つ / CYCLE ボス: fire, fire+gap に撃ち、3 回に 1 回は 2.5m まで突進して殴る
 *        wind〜fire の間は予備動作で点滅、fire から gap おきに burst 回、period で 1 周
 *   shot 撃つ弾、sh 弾を出す高さ(1/16m、自分の高さから) */
enum { MV_NONE, MV_WALK, MV_HOVER, MV_STAND, MV_KEEP, MV_SHOT };
enum { AT_NONE, AT_MELEE, AT_SHOOT, AT_CYCLE };
typedef struct {
    u8 hp, mv;
    i8 up, keep;
    u8 gain, sway, swaym, bob, bobm;
    u8 atk, wind, fire, period, burst, gap, shot;
    i8 sh;
} EDef;
#define EN_WALK(hp, reach)  { hp, MV_WALK, -21, reach, 0, 0, 0, 0, 0, AT_MELEE, 0, 15, 60, 1, 1, 0, 0 }
#define EN_ITEM(hp, up)     { hp, MV_NONE, up, 0, 0, 0, 0, 0, 0, AT_NONE, 0, 0, 0, 0, 1, 0, 0 }
#define EN_SHOT(hp, speed)  { hp, MV_SHOT, 0, speed, 0, 0, 0, 0, 0, AT_NONE, 0, 0, 0, 0, 1, 0, 0 }
static const EDef edef[ET_COUNT] = {
    /*                hp   mv        up   keep gain sway swm  bob bobm atk       wind fire per burst gap shot      sh */
    /* NONE     */ EN_ITEM(0, -21),
    /* E01 猿人 */ EN_WALK(1, 8),
    /* E02 蝙蝠 */ {  2, MV_HOVER, -5,    9,  3,   0,  0,  24,  8, AT_MELEE,  0, 15,  60, 1,  1, 0,        0 },
    /* E03 麒麟 */ EN_WALK(3, 14),
    /* E04 弓兵 */ {  2, MV_STAND,  28,   0,  0,   0,  0,   0,  0, AT_SHOOT, 30, 45,  80, 1,  1, ET_ARROW, 13 },
    /* E05 魔法 */ {  3, MV_HOVER, -5,   32,  3,  11, 64,  24, 16, AT_SHOOT, 40, 55, 100, 1,  1, ET_MAGIC, 10 },
    /* B01 親方 */ { 30, MV_KEEP,  -21,  14,  3,  10, 64,   0,  0, AT_MELEE,  0, 15,  45, 1,  1, 0,        0 },
    /* 宝箱     */ EN_ITEM(0, -21),
    /* 金貨袋   */ EN_ITEM(1, -21),
    /* 洞窟の主 */ { 60, MV_KEEP,  -21,  28,  2,  13, 64,   0,  0, AT_CYCLE, 30, 30,  90, 2, 30, ET_ROCK,  35 },
    /* 岩       */ EN_SHOT(1, 5),
    /* 矢       */ EN_SHOT(1, 8),
    /* 魔法弾   */ EN_SHOT(2, 3),
    /* 木箱     */ EN_ITEM(1, -23),
    /* 湖の主   */ { 40, MV_KEEP,  -21, -28,  2,  13, 64,   0,  0, AT_CYCLE, 30, 30,  90, 2, 30, ET_MAGIC, 35 },
    /* 森の番人 */ { 60, MV_KEEP,    3,  32,  2,  21, 32,  32,  8, AT_SHOOT, 25, 40,  95, 3,  8, ET_MAGIC, 16 },
    /* 山車     */ { 60, MV_KEEP,  -21, -28,  2,  13, 64,   0,  0, AT_CYCLE, 30, 30,  90, 2, 30, ET_MAGIC, 35 },
    /* 亜人の王 */ {100, MV_KEEP,  -21,  24,  2,  13, 64,   0,  0, AT_CYCLE, 30, 30,  90, 2, 30, ET_MAGIC, 35 },
    /* 分岐 L   */ EN_ITEM(1, -21),
    /* 分岐 R   */ EN_ITEM(1, -21),
};
#define IS_SIGN(t) ((t) == ET_SIGNL || (t) == ET_SIGNR)

/* ---------------------------------------------------------------------------
 * 状態
 * --------------------------------------------------------------------------- */
enum { ST_IDLE, ST_APPROACH, ST_ATTACK, ST_PASS, ST_DEAD, ST_CHASE, ST_GRAB };

typedef struct {
    u8  type, state, hp, flash;
    u8  timer, attacks, anim, side;
    u16 s;
    u8  sub;
    i16 v;
    i16 lat, lat_t, up;
    V3  c;
} Enemy;

#define MAX_EN 14
static Enemy en[MAX_EN];
static u8 draw_order[MAX_EN];

/* 当たり判定用: 直前に描いた矩形(ブロック座標) */
typedef struct { u8 x0, y0, x1, y1; i16 z; u8 id; } HitRect;
#define MAX_HIT 48
HitRect hits[MAX_HIT];            /* geom.s の draw_model が足す */
u8 n_hits;

u16 cam_s;
static u8  cam_sub;
u16 cam_v;
static u8  hold;                 /* 1 = トロッコ停止(発進前) */
static u16 look;                 /* 視線の相対ヨー(0 前 / 0x8000 後ろ) */
u8  facing;               /* 0 前 / 1 後ろ */
static u8  turning;              /* 振り向きの残りフレーム */
u16 aim_x, aim_y;                /* 照準(ピクセル)。テストの自動操作が書き換える */
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
static u8  ctrl;                 /* 1 = 操作できる(オープニングの演出中は 0) */
static u8  spd_over;             /* 0 以外: トロッコの速度(m/秒 × 4)をスクリプトで指定 */
static u8  cap_id, cap_time;     /* 字幕の番号と残りフレーム */
static u8  warn_prev, warn_time; /* 警告の出始めと繰り返しで警告音を鳴らす */
static i8  pitch_cur;            /* カメラのピッチ(坂に合わせて少しずつ追う) */
static u16 fps_t0;               /* 計測用: 1 秒(60 ティック)の区切り */
static u8  fps_n, fps_show;      /* その間の表示の切替回数と、表示する値 */
static const Stage *stage;
u8  route;                       /* 分岐で選ばれている側(0 左 / 1 右) */
static u8  branch_open;          /* 1 = まだ選べる(分岐点の手前) */
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
    e->hp = edef[type].hp;
    e->flash = 0;
    e->timer = 0;
    e->attacks = 0;
    e->anim = i * 5;
    e->side = lat8 < 0;
    e->s = (u16)s;
    e->sub = 0;
    e->v = 0;
    e->lat = e->lat_t = (i16)lat8 << 5;       /* 1/8m → 8.8 */
    e->up = (i16)edef[type].up << 4;           /* 床に立つ・飛ぶ・高台・水面 */
    switch (edef[type].mv) {
    case MV_HOVER:
        e->state = ST_APPROACH;
        e->lat_t = e->side ? F88(-0.5) : F88(0.5);
        break;
    case MV_KEEP:
        e->state = ST_APPROACH;
        e->lat_t = 0;
        boss_alive = 1;
        break;
    case MV_WALK:
        if (dist < 0) {
            e->state = ST_CHASE;               /* 後ろから走って追いかけてくる */
            e->v = MPS(3.3);
        }
        break;
    }
    return e;
}

/* 数字の表示(ルール: 色・形・場所・動きで 3 種類を分ける)
 *   得点        白  数字だけ「100」            倒した敵の位置    上へ流れる
 *   宝が増えた  黄  金貨の印 +「+2」(満杯 FULL)  宝の山の上        上へ流れる
 *   宝が減った  赤  金貨の印 +「-1」             宝の山の上        その場で点滅 */
enum { POP_SCORE, POP_GAIN, POP_LOSS };
#define N_POP 4
static u8  pop_x[N_POP], pop_y[N_POP], pop_t[N_POP], pop_k[N_POP];
static u16 pop_v[N_POP];
static u8  pop_hx, pop_hy;       /* 撃った敵の画面上の位置(fire が決める) */
#define PILE_POP_Y (TROLLEY_Y - 6)

static void popup(u8 kind, u16 v)
{
    u8 i, k = 0;
    for (i = 1; i < N_POP; i++) if (pop_t[i] < pop_t[k]) k = i;
    pop_k[k] = kind; pop_v[k] = v; pop_t[k] = 16;
    pop_x[k] = (kind == POP_SCORE) ? pop_hx : 32;
    pop_y[k] = (kind == POP_SCORE) ? pop_hy : PILE_POP_Y;
}

static void popups_draw(void)
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

static void damage_player(void)
{
    u8 i, r, dmg;
    if (!life) return;
    shake = 4;
    flash_palette();
    se_play(SE_DAMAGE);
    if (g_cfg.god) return;                   /* 隠しコンフィグ: 被弾しても宝が減らない */
    dmg = (g_cfg.damage < life) ? g_cfg.damage : life;
    life -= dmg;
    popup(POP_LOSS, dmg);                    /* 宝がいくつ減ったか */
    r = (u8)g_tick;
    for (i = 0; i < N_COIN; i++) {
        r = r * 5 + 7;
        coin_x[i] = F88(32) + ((i16)(r & 31) - 16) * 64;
        coin_y[i] = F88(TROLLEY_Y + 2);
        coin_vx[i] = ((i16)(r & 63) - 32) * 12;
        coin_vy[i] = -F88(1.2) - (i16)(r & 7) * 24;
    }
    coin_t = 14;
}

static void coins_draw(void)
{
    u8 i;
    if (!coin_t) return;
    coin_t--;
    for (i = 0; i < N_COIN; i++) {
        i16 x = coin_x[i] >> 8, y = coin_y[i] >> 8;
        coin_x[i] += coin_vx[i];
        coin_y[i] += coin_vy[i];
        coin_vy[i] += 64;
        if (x >= 0 && x < 64 && y >= 0 && y < GAME_ROWS) BUF[(u8)y * 64 + (u8)x] = (i & 1) ? C_YELLOW : C_WHITE;
    }
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

/* 敵弾を撃つ(e の位置から、カメラへ向かって飛ぶ) */
static void shoot(Enemy *e, u8 type, i16 d, i16 up)
{
    Enemy *r = spawn(type, (i8)(d >> 6), (i8)(e->lat >> 5));
    if (!r) return;
    r->s = (d > 0) ? e->s - M(0.5) : e->s + M(0.5);
    r->up = up;
    r->lat = e->lat;
    r->lat_t = 0;
    r->state = ST_APPROACH;
    e->flash = 2;
}

static i16 iabs(i16 v) { return v < 0 ? -v : v; }

/* 攻撃の 1 フレーム(表 edef の wind / fire / gap / burst / period) */
static void attack(Enemy *e, const EDef *f, i16 d)
{
    u8 t = ++e->timer, k;
    if (t >= f->wind && t < f->fire) {
        e->flash = (t & 2) ? 1 : 0;                  /* 予備動作 */
    } else if (t >= f->fire) {
        k = t - f->fire;
        if (k % f->gap == 0 && k / f->gap < f->burst) {
            if (f->atk == AT_MELEE) damage_player();
            else shoot(e, f->shot, d, e->up + ((i16)f->sh << 4));
        }
    }
    if (t >= f->period) { e->timer = 0; e->attacks++; }
}

/* ボスの突進(3 回に 1 回): 3m まで寄ったら予備動作の後に殴り、また下がる */
static void lunge(Enemy *e, i16 d)
{
    if (iabs(d) > (i16)M(3)) return;
    if (e->timer < 60) e->timer = 60;
    e->timer++;
    if (e->timer < 75) e->flash = (e->timer & 2) ? 1 : 0;
    else if (e->timer == 75) damage_player();
    else if (e->timer >= 100) { e->timer = 0; e->attacks++; e->flash = 0; }
}

static void update_enemy(Enemy *e)
{
    const EDef *f = &edef[e->type];
    i16 d = (i16)(e->s - cam_s);
    i16 keep = (i16)f->keep << 4;                    /* 1/4m → 1/64m */
    e->anim++;
    if (e->flash && e->state != ST_ATTACK) e->flash--;
    if (f->sway) e->lat_t = (e->anim & f->swaym) ? ((i16)f->sway << 4) : -((i16)f->sway << 4);
    if (f->bob) e->up = ((i16)f->up << 4) + ((e->anim & f->bobm) ? f->bob : 0);

    switch (e->state) {
    case ST_DEAD:
        e->up -= 24;
        move_enemy(e);
        if (++e->timer >= 12) {
            if (IS_BOSS(e->type)) boss_alive = 0;
            e->type = ET_NONE;
        }
        return;
    case ST_GRAB:
        /* 宝箱がカメラへ飛んできて消える(オープニング) */
        e->v = (d < 0) ? (i16)cam_v + MPS(4) : (i16)cam_v - MPS(4);
        if (e->up < F88(-0.6)) e->up += 20;
        if (d > -(i16)M(0.8) && d < (i16)M(0.8)) {
            e->type = ET_NONE;
            se_play(SE_HEAL);
            return;
        }
        break;
    case ST_CHASE:
        /* 後ろから走って追ってくる(歩く敵)。2m まで近づいたら殴る */
        if (d >= -(i16)M(2)) {
            e->v = (i16)cam_v;
            attack(e, f, d);
        } else {
            /* トロッコより少し速く走って必ず追いつく(置いていかれたまま残って、やさしい進行を止めないように) */
            e->v = (i16)cam_v + MPS(1.2);
            e->flash = 0;
            e->timer = 0;
        }
        break;
    case ST_IDLE:
        if (f->mv == MV_WALK && d > 0 && d < (i16)M(14)) {
            e->state = ST_APPROACH;
            e->v = -MPS(2.5);
            e->lat_t = e->side ? F88(-0.35) : F88(0.35);
        } else if (f->mv == MV_STAND && iabs(d) > (i16)M(6) && iabs(d) < (i16)M(24)) {
            attack(e, f, d);                         /* 立ったまま撃つ(弓兵) */
        }
        break;
    case ST_APPROACH:
        switch (f->mv) {
        case MV_WALK:
            if (d > 0 && d <= keep) { e->state = ST_ATTACK; e->timer = 0; }
            break;
        case MV_HOVER:
            if (d < -keep) e->v = (i16)cam_v + MPS(3);
            else if (d > keep) e->v = (i16)cam_v - MPS(3);
            else { e->state = ST_ATTACK; e->timer = 0; }
            break;
        case MV_KEEP:
            /* ボス: keep の距離(負 = 後ろ)まで寄ってくる */
            if (keep > 0 ? d > keep : d < keep) {
                e->v = (i16)cam_v + (keep > 0 ? -MPS(2) : MPS(2));
            } else {
                e->state = ST_ATTACK; e->timer = 0; e->attacks = 0;
            }
            break;
        case MV_SHOT:
            /* 敵弾: カメラへ向かってまっすぐ飛ぶ。手前 0.8m に来たら当たる */
            {
                i16 sv = (i16)f->keep * 546;
                e->v = (d > 0) ? (i16)cam_v - sv : (i16)cam_v + sv;
            }
            if (e->up > F88(-0.3)) e->up -= 12;
            if (d < (i16)M(0.8) && d > -(i16)M(0.8)) {
                damage_player();
                e->type = ET_NONE;
                return;
            }
            break;
        }
        break;
    case ST_ATTACK:
        if (f->mv == MV_WALK) {
            e->v = (i16)cam_v;                        /* 張り付いて殴る。2 回殴ったら置いていかれる */
            attack(e, f, d);
            if (e->attacks >= 2) { e->state = ST_PASS; e->v = 0; e->flash = 0; }
            break;
        }
        {
            i16 tgt = keep;
            u8 n = 1;
            if (f->mv == MV_HOVER && d < 0) tgt = -keep;
            if (f->atk == AT_CYCLE && e->attacks % 3 == 2) tgt = (keep > 0) ? (i16)M(2.5) : -(i16)M(2.5);
            e->v = (i16)cam_v + ((tgt - d) << f->gain);
            if (e->type == ET_B04 && e->hp < 50) n = 2;          /* 亜人の王: 耐久が半分を切ると 2 倍の速さ */
            while (n--) {
                if (f->atk == AT_CYCLE && e->attacks % 3 == 2) lunge(e, d);
                else attack(e, f, d);
            }
            /* 大猿人の親方: ときどき子分の猿人を呼ぶ */
            if (e->type == ET_B01 && (e->anim & 127) == 0) spawn(ET_E01, 14, (e->anim & 128) ? 10 : -10);
        }
        break;
    }
    move_enemy(e);

    /* カメラの後方へ抜けた敵を消す(追跡型・張り付く敵・ボス・演出中の宝箱を除く) */
    if (e->state == ST_CHASE) {
        if (d < -(i16)M(24)) e->type = ET_NONE;
    } else if (f->mv != MV_HOVER && f->mv != MV_KEEP && e->type != ET_TREASURE) {
        if (d < -(i16)M(3)) e->type = ET_NONE;
    }
    if (d > (i16)M(60) || d < -(i16)M(40)) e->type = ET_NONE;
}

/* 分岐の側を決める(分岐点の手前でだけ。違う側ならレールを分岐点から作り直す)。L・R の獣人は両方消える */
static void branch_pick(u8 dir)
{
    u8 i;
    if (branch_open) {
        branch_open = 0;
        if (dir != route) {
            route = dir;
            rail_refork(stage->branch[dir], stage->tail);
        }
    }
    for (i = 0; i < MAX_EN; i++)
        if (IS_SIGN(en[i].type)) en[i].type = ET_NONE;
}

static void kill_enemy(Enemy *e)
{
    u8 t = e->type;
    if (IS_SIGN(t)) {                        /* 先に撃った側へ */
        se_play(SE_KILL);
        branch_pick(t == ET_SIGNR);
        return;
    }
    if (t == ET_MEDKIT || t == ET_CRATE) {
        u8 l0 = life;
        life += 2;
        if (life > 8) life = 8;
        popup(POP_GAIN, life - l0);
        e->type = ET_NONE;
        se_play(SE_HEAL);
        return;
    }
    if (IS_SHOT(t)) {                        /* 敵弾の撃墜: 50 点 */
        g_score += 5;
        popup(POP_SCORE, 50);
        e->type = ET_NONE;
        se_play(SE_HIT);
        return;
    }
    {
        u16 pt = IS_BOSS(t) ? edef[t].hp * 20 : edef[t].hp * 10;
        g_score += pt;
        popup(POP_SCORE, pt * 10);
    }
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
            pop_hx = (u8)((h->x0 + h->x1) >> 1);
            pop_hy = (h->y0 > 6) ? h->y0 - 6 : 0;
        }
    }
    if (best >= MAX_EN) return;
    {
        Enemy *e = &en[best];
        if (e->type == ET_NONE || e->state == ST_DEAD) return;
        hitmark = 4;
        if (g_cfg.oneshot) e->hp = 1;        /* 隠しコンフィグ: 1 発で倒せる */
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
/* 攻撃してくるもの(敵・ボス・敵弾)か */
#define IS_FOE(t) ((t) != ET_NONE && (t) != ET_TREASURE && (t) != ET_MEDKIT && (t) != ET_CRATE && !IS_SIGN(t))

/* 命令が敵を呼ぶ・警告を出す向き(1 前 / -1 後ろ / 0 なし) */
static i8 ev_side(const Event *ev)
{
    u8 c = EV_CMD(ev->cmd);
    if (c == EV_SPAWN || c == EV_BOSS) return IS_FOE(ev->a) ? (ev->b < 0 ? -1 : 1) : 0;
    if (c == EV_DIR) return ev->a == DIR_FRONT ? 1 : ev->a == DIR_BACK ? -1 : 0;
    return 0;
}

/* いま残っている敵の向き(1 前 / -1 後ろ / 0 いない) */
static i8 foe_side(void)
{
    u8 i;
    for (i = 0; i < MAX_EN; i++) {
        Enemy *e = &en[i];
        if (IS_FOE(e->type) && e->state != ST_DEAD) return ((i16)(e->s - cam_s) < 0) ? -1 : 1;
    }
    return 0;
}

static void script_update(void)
{
    static u8 held;
    if (boss_alive) return;              /* ボス戦中は進行を止める */
    if (!held) sc_tick += 2;             /* 反対側の敵を待たせている間は時刻も止める */
    held = 0;
    while (!sc_end) {
        const Event *ev = sc_ev;
        /* 通らなかった分岐の側の命令は、待たずに飛ばす */
        if ((ev->cmd & EV_ONLY_L && route != 0) || (ev->cmd & EV_ONLY_R && route != 1)) {
            sc_ev++;
            continue;
        }
        if (ev->cmd & EV_AT) {
            if ((cam_s >> 6) < ev->tick) break;
        } else if (ev->tick > sc_tick) {
            break;
        }
        if (g_cfg.easy == 1 || (g_cfg.easy == 0 && stage->gentle)) {
            /* やさしい進行: 反対側に敵が残っていれば、全滅するまで待つ */
            i8 sd = ev_side(ev), fs = foe_side();
            if (sd && fs && sd != fs) { held = 1; break; }
        }
        sc_ev++;
        switch (EV_CMD(ev->cmd)) {
        case EV_SPAWN: spawn(ev->a, ev->b, ev->c); break;
        case EV_DIR:   attack_dir = ev->a; break;
        case EV_GO:    hold = ev->a; break;
        case EV_BOSS:  spawn(ev->a, ev->b, 0); break;
        case EV_END:   sc_end = 1; break;
        case EV_TEXT:  cap_id = ev->a; cap_time = 40; break;
        case EV_SPEED: spd_over = ev->a; break;
        case EV_CTRL:  ctrl = ev->a; break;
        case EV_TURN:
            if (facing != ev->a) { facing = ev->a; turning = 8; }
            break;
        case EV_BGM:
            if (ev->a) bgm_play(stage->bgm_melody, stage->bgm_bass);
            else bgm_stop();
            break;
        case EV_GRAB: {
            u8 i;
            for (i = 0; i < MAX_EN; i++)
                if (en[i].type == ET_TREASURE) en[i].state = ST_GRAB;
            break;
        }
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

    /* 演出中は操作できない。A で演出を飛ばす */
    if (!ctrl) {
        if ((edge & IN_A) && stage->skip_tick && sc_tick < stage->skip_tick) sc_tick = stage->skip_tick - 2;
        if (turning) {
            look += 0x1000;
            if (--turning == 0) look = facing ? 0x8000 : 0;
        }
        return;
    }

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
    u16 target = hold ? 0 : (u16)(spd_over ? spd_over : rail_spd[cam_s >> 7]) * 137;
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
    /* 分岐点の 2m 手前まで来たら、選ばれている側(撃たなければ規定の側)で確定する */
    if (branch_open && cam_s + M(2) >= rail_fork_m() * 64) branch_pick(route);
}

/* ---------------------------------------------------------------------------
 * 描画
 * --------------------------------------------------------------------------- */
/* 敵モデルを描く(本体は geom.s の draw_model) */
extern const MRect *g_dm_rects;
extern u8 g_dm_n, g_dm_flash, g_dm_id, g_dm_hit, g_dm_grow;
void draw_model(const V3 *c);

static void draw_enemy(u8 id)
{
    Enemy *e = &en[id];
    const Model *md;
    if (e->c.z < NEAR_Z + 32) return;
    md = (e->type == ET_E02 && (e->anim & 4)) ? &model_e02b : &models[e->type];
    g_dm_rects = md->r;
    g_dm_n = md->n;
    /* 縁取りのルール: 敵・敵弾は白、金貨袋は黄。宝箱と分岐の獣人(文字で示す)は縁取りしない */
    if (!e->flash && e->type != ET_TREASURE && !IS_SIGN(e->type)) {
        g_dm_flash = (e->type == ET_MEDKIT || e->type == ET_CRATE) ? C_YELLOW : C_WHITE;
        g_dm_hit = 0;
        g_dm_grow = 1;
        draw_model(&e->c);
        g_dm_grow = 0;
    }
    g_dm_flash = e->flash ? C_WHITE : 0;
    g_dm_id = id;
    g_dm_hit = e->type != ET_TREASURE && e->state != ST_DEAD;
    {
        u8 h0 = n_hits;
        draw_model(&e->c);
        if (IS_SIGN(e->type) && n_hits > h0) {
            /* お腹の文字 L / R: 絵の外接矩形から位置を出し、白地に黒で大きめに描く(遠くても読めるように) */
            u8 k, x0 = 255, y0 = 255, x1 = 0, y1 = 0;
            for (k = h0; k < n_hits; k++) {
                if (hits[k].x0 < x0) x0 = hits[k].x0;
                if (hits[k].y0 < y0) y0 = hits[k].y0;
                if (hits[k].x1 > x1) x1 = hits[k].x1;
                if (hits[k].y1 > y1) y1 = hits[k].y1;
            }
            x0 = (u8)((x0 + x1) >> 1);
            y0 = (u8)(y0 + (u8)(((u16)(y1 - y0) * 115) >> 8));
            if (x0 >= 2 && x0 <= 61 && y0 >= 3 && y0 + 4 <= GAME_ROWS) {
                g_rx = x0 - 2; g_ry = y0 - 3; g_rw = 5; g_rh = 7; g_color = C_WHITE;
                rect_fill();
                text_draw((i8)x0 - 1, (i8)y0 - 2, e->type == ET_SIGNL ? "L" : "R", C_BLACK, 1);
            }
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
    /* 警告音: 出始めに鳴らし、続いている間は約 2 秒ごとに鳴らす */
    if (warn && (warn != warn_prev || !warn_time)) { se_play(SE_ALARM); warn_time = 40; }
    if (warn_time) warn_time--;
    warn_prev = warn;
    /* 振り向きの警告は文字で(矢印はルート選択の表示に使うので使わない) */
    if (warn) {
        if (blink < 8) text_center(1, warn == 1 ? "ENEMY BEHIND!" : "ENEMY AHEAD!", C_RED, 1);
    } else {
        num_draw(40, 1, g_score, 5, C_WHITE);
        if (g_score) text_draw(60, 1, "0", C_WHITE, 1);
    }
    if (++blink >= 16) blink = 0;       /* 約 15 ティック点灯 / 15 ティック消灯 */
    if (cap_id) {
        const Caption *c = &stage->captions[cap_id - 1];
        text_center(c->scale == 2 ? 14 : 18, c->text, c->color, c->scale);
        if (!--cap_time) cap_id = 0;
    }

    if (g_cfg.fps) {
        /* 計測用(隠しコンフィグの FPS、計測用 ROM では最初から): 左上に fps(直近 1 秒の表示の切替回数)と直前のフレームのティック数 */
        fps_n++;
        if ((u16)(g_tick - fps_t0) >= 60) { fps_show = fps_n; fps_n = 0; fps_t0 = g_tick; }
        text_draw(1, 8, "FPS", C_YELLOW, 1);
        num_draw(15, 8, fps_show, 2, C_YELLOW);
        text_draw(1, 14, "T", C_YELLOW, 1);
        num_draw(7, 14, g_perf_ticks, 2, C_YELLOW);
    }
    if (!ctrl) { g_spr_attr[0] = 209; g_spr_attr[4] = 209; return; }
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
    /* 坂: レールの傾きからピッチの目標(≒ atan、256 分割)を出し、1 フレーム 1 ずつ追う。
     * 後ろを向いているときは向きが逆、振り向き中はその間を cos で補間する */
    {
        u16 i = cam_s >> 7;
        i16 dy;
        i8 tgt;
        if (i + 1 >= rail_len / RAIL_STEP) i = rail_len / RAIL_STEP - 2;
        dy = (rail[i + 1].y - rail[i].y) >> 1;   /* 1m あたりの高さ変化(8.8)。点は 2m ごと */
        /* atan(x) ≒ x / (1 + 0.28x²)(急な坂でも破綻しない)。1 フレームで差の 1/4 ずつ追う */
        tgt = (i8)(((i32)dy * 41) / (256 + (((i32)dy * dy * 72) >> 16)));
        {
            i8 st = (i8)((tgt - pitch_cur) >> 2);
            if (!st) st = (tgt > pitch_cur) ? 1 : (tgt < pitch_cur) ? -1 : 0;
            pitch_cur += st;
        }
    }
    render_set_camera(p.x, p.y, p.z, yaw + look, (i8)fmul(pitch_cur, COS((u8)(look >> 8))));
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
    coins_draw();
    popups_draw();
    if (shake) shake--;
    draw_hud();
    g_perf_render = timer_read() - t0;
}

/* ---------------------------------------------------------------------------
 * 1 プレイ
 * --------------------------------------------------------------------------- */
u8 game_run(const Stage *st, u8 first)
{
    u8 i, over = 0;
    rail_begin();
    rail_add(st->track);
    route = g_cfg.route ? g_cfg.route - 1 : st->branch_def;
    branch_open = 0;
    if (st->branch[0]) {                 /* 分岐: まず規定の側(隠しコンフィグで固定なら、その側)で作っておく */
        rail_fork();
        rail_add(st->branch[route]);
        rail_add(st->tail);
        branch_open = !g_cfg.route;
    }
    env_build(st->seed, st->zones);
    set_stage_palette(st->palette);
    for (i = 0; i < MAX_EN; i++) en[i].type = ET_NONE;
    n_hits = 0;
    stage = st;
    cam_s = M(st->start_m); cam_sub = 0; cam_v = 0; hold = 1;
    if (!st->skip_tick) {                /* 演出の無いステージは走っている状態から始まる */
        hold = 0;
        cam_v = (u16)rail_spd[st->start_m >> 1] * 137;
    }
    facing = st->start_back; look = facing ? 0x8000 : 0; turning = 0;
    ctrl = 1; spd_over = 0; cap_id = 0; cap_time = 0; warn_prev = 0; warn_time = 0; pitch_cur = 0;
    aim_x = 128; aim_y = 92; aim_hold = 0;
    in_prev = 0xFF;                      /* タイトルで押したボタンを射撃と見なさない */
    fire_cd = 0; hitmark = 0;
    if (first) { life = g_cfg.life; g_score = 0; }
    attack_dir = DIR_NONE;
    boss_alive = 0;
    blink = 0;
    shake = 0; coin_t = 0;
    pop_t[0] = pop_t[1] = pop_t[2] = pop_t[3] = 0;
    sc_tick = 0; sc_ev = st->events; sc_end = 0;
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

    g_spr_attr[0] = 209;
    g_spr_attr[4] = 209;
    bgm_stop();
    if (!over) return 1;
    /* ゲームオーバー(約 4 秒) */
    g_scene = 2;
    for (i = 0; i < 120; i++) {
        buf_fill(C_BLACK, BUF);
        if (i & 16) text_center(12, "GAME", C_RED, 2);
        if (i & 16) text_center(24, "OVER", C_RED, 2);
        text_center(38, "NO TREASURE LEFT", C_YELLOW, 1);
        present();
    }
    return 0;
}
