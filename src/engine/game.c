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
#include "fx.h"
#include "pcm.h"

#define M(x)    ((u16)((x) * 64))               /* m → s 単位 */
#define MPS(x)  ((i16)((x) * 546))              /* m/秒 → v 単位(30fps) */
#define F88(x)  ((i16)((x) * 256))
/* 高さの基準はレール上の点(以前のトロッコの目の高さ)。床はその 1.3m 下 */
#define FLOOR_Y F88(-1.3)                       /* レール上の点から見た床 */
#define FLY_Y   F88(-0.3)                       /* コウモリ人の飛ぶ高さ */
#define EYE_DROP F88(0.7)                       /* 一人乗りのコースターは座席が低い(F1 のような視点)。目はレール上の点より 0.7m 下(床から 0.6m) */

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
    /* 救急箱   */ EN_ITEM(1, -21),
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
    /* 弾の箱   */ EN_ITEM(1, -21),
    /* E06 半魚 */ EN_WALK(2, 8),
    /* 大ダコ人 */ { 60, MV_KEEP,  -21, -28,  2,  13, 64,   0,  0, AT_CYCLE, 30, 30,  90, 2, 30, ET_MAGIC, 35 },
    /* 大ダコの足*/{  3, MV_HOVER, -40,  36,  3,   0,  0,  24,  8, AT_MELEE, 25, 45, 100, 1,  1, 0,        0 },
};
#define IS_SIGN(t) ((t) == ET_SIGNL || (t) == ET_SIGNR)

/* ---------------------------------------------------------------------------
 * 状態
 * --------------------------------------------------------------------------- */
enum { ST_IDLE, ST_APPROACH, ST_ATTACK, ST_PASS, ST_DEAD, ST_CHASE, ST_GRAB,
       ST_LEAP };    /* 半魚人が海から跳び上がっている(海の面だけ) */

#ifdef GAME_SEA
/* 半魚人の跳び上がり: 床の 3.2m 下(橋の横の海)から、上向きの速さ LEAP_VY(1/256m/フレーム)で跳び、毎フレーム LEAP_G ずつ落ちる。
 * 約 5.2m 上がって床の 2m 上で向きを変え、約 37 フレームで床に降りる。速さは attacks に入れておく(降りたら 0 に戻す)。
 * 低い座席からは橋の床の縁より下は見えないので、頭が床より上に出るまでは描かない(LEAP_SHOW) */
#define LEAP_Y0 (-21 * 16 - 816)
#define LEAP_VY 115
#define LEAP_G  5
#define LEAP_SHOW F88(-2.5)
#endif

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
Enemy en[MAX_EN];                /* テスト(openMSX)が build/game*.noi から番地を引いて読む */
static u8 draw_order[MAX_EN];

static u8  cam_sub;
static u8  hold;                 /* 1 = トロッコ停止(発進前) */
static u16 look;                 /* 視線の相対ヨー(0 前 / 0x8000 後ろ) */
static u8  turning;              /* 振り向きの残りフレーム */
static u8  aim_hold;
static u8  in_prev;
static u8  fire_cd;
static u8  hitmark;
static u8  shotfx;               /* 1 = このフレームに撃った(照準の真ん中に閃光を重ねる) */
static u16 voice_t;              /* ボスの Warning! の声を鳴らした時刻(g_tick)。直後の Enemy ahead/behind! で上書きしない */
static u16 fire_t;               /* 誘導弾の Fire! を鳴らした時刻(g_tick)。鳴っている間(FIRE_T)はほかの声で切らない */
#define FIRE_T 20                /* Fire! の長さ(0.31 秒)をティックで */
static u8  blink;
static u8  shake;                /* 被弾でトロッコを揺らす残りフレーム */
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

static u8  branch_open;          /* 1 = まだ選べる(分岐点の手前) */

/* ---------------------------------------------------------------------------
 * 敵
 * --------------------------------------------------------------------------- */
/* 声を鳴らす(誘導弾の Fire! がいちばん上: Fire! が鳴っている間は、ほかの声で切らない)。鳴らしたら 1 */
static u8 say(u8 voice)
{
    if ((u16)(g_tick - fire_t) < FIRE_T) return 0;
    pcm_say(voice);
    return 1;
}

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
    /* 位置は 1/64m の u16(512m を超えると i16 では負になる)。後ろへはみ出したら 0 に */
    s = (i16)dist * 64;
    if (s < 0 && cam_s < (u16)-s) s = 0;
    else s = (i16)(cam_s + (u16)s);
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
#ifdef GAME_SEA
    if (type == ET_E06) {                      /* 半魚人: 橋の横の海から跳び上がり、レールの脇に降りる */
        e->state = ST_LEAP;
        e->up = LEAP_Y0;
        e->attacks = LEAP_VY;
        e->lat_t = e->side ? -F88(0.35) : F88(0.35);
    } else if (type == ET_TENT) {              /* 大ダコ人の足: トラスの外側をつかむ */
        e->lat_t = e->side ? -F88(2.3) : F88(2.3);
    }
#endif
    return e;
}

static void damage_player(void)
{
    u8 dmg;
    if (!life) return;
    shake = 4;
    flash_palette();
    se_play(SE_DAMAGE);
    if (g_cfg.god) return;                   /* 隠しコンフィグ: 被弾しても耐久が減らない */
    dmg = (g_cfg.damage < life) ? g_cfg.damage : life;
    life -= dmg;
    popup(POP_LOSS, dmg);                    /* 耐久がいくつ減ったか */
    debris_start();                          /* 車両から車体の破片が飛び散る */
}


/* 更新中の敵の写し。敵の更新(update_enemy から呼ぶ関数)は、en[i] をポインタで触る代わりにこの写しを番地で直接触る
 * (SDCC はポインタ経由のフィールドを IX の相対で何度も読み直すので遅い。動きは同じ) */
Enemy g_ce;                          /* aimhit.s の ce_load / ce_store が写す */
#define ce g_ce
void ce_load(Enemy *e);              /* en[i] → g_ce(カメラ座標 c を除く 19 バイト。c は更新で触らない) */
void ce_store(Enemy *e);             /* g_ce → en[i] */

static void move_enemy(void)
{
    i16 v = ce.v;
    u16 lo = (u16)ce.sub + (u8)v;
    ce.sub = (u8)lo;
    ce.s += (u16)((v >> 8) + (i16)(lo >> 8));
    if (ce.lat < ce.lat_t) {
        ce.lat += 20;
        if (ce.lat > ce.lat_t) ce.lat = ce.lat_t;
    } else if (ce.lat > ce.lat_t) {
        ce.lat -= 20;
        if (ce.lat < ce.lat_t) ce.lat = ce.lat_t;
    }
}

/* 敵弾を撃つ(e の位置から、カメラへ向かって飛ぶ) */
static void shoot(u8 type, i16 d, i16 up)
{
    Enemy *r = spawn(type, (i8)(d >> 6), (i8)(ce.lat >> 5));
    if (!r) return;
    r->s = (d > 0) ? ce.s - M(0.5) : ce.s + M(0.5);
    r->up = up;
    r->lat = ce.lat;
    r->lat_t = 0;
    r->state = ST_APPROACH;
    ce.flash = 2;
}

static i16 iabs(i16 v) { return v < 0 ? -v : v; }

/* 攻撃の 1 フレーム(表 edef の wind / fire / gap / burst / period) */
static void attack(const EDef *f, i16 d)
{
    u8 t = ++ce.timer, k;
    if (t >= f->wind && t < f->fire) {
        ce.flash = (t & 2) ? 1 : 0;                  /* 予備動作 */
    } else if (t >= f->fire) {
        k = t - f->fire;
        if (k % f->gap == 0 && k / f->gap < f->burst) {
            if (f->atk == AT_MELEE) damage_player();
            else shoot(f->shot, d, ce.up + ((i16)f->sh << 4));
        }
    }
    if (t >= f->period) { ce.timer = 0; ce.attacks++; }
}

/* ボスの突進(3 回に 1 回): 3m まで寄ったら予備動作の後に殴り、また下がる */
static void lunge(i16 d)
{
    if (iabs(d) > (i16)M(3)) return;
    if (ce.timer < 60) ce.timer = 60;
    ce.timer++;
    if (ce.timer < 75) ce.flash = (ce.timer & 2) ? 1 : 0;
    else if (ce.timer == 75) damage_player();
    else if (ce.timer >= 100) { ce.timer = 0; ce.attacks++; ce.flash = 0; }
}

#ifdef GAME_SEA
/* 大ダコ人(後ろ)は、橋の両脇の海から足を伸ばしてくる(約 3 秒ごと、同時に 2 本まで、左右交互)。
 * 足も大ダコ人と同じ後ろから(ボス戦の間は警告の向きを変えない。2026-10-15 ユーザーの指定) */
static void octo_arms(void)
{
    u8 i, n = 0;
    for (i = 0; i < MAX_EN; i++) if (en[i].type == ET_TENT && en[i].state != ST_DEAD) n++;
    if (n < 2 && (ce.anim & 63) == 32) spawn(ET_TENT, -14, (ce.anim & 64) ? 22 : -22);
}
#endif

static void update_ce(void)
{
    const EDef *f = &edef[ce.type];
    i16 d = (i16)(ce.s - cam_s);
    i16 keep = (i16)f->keep << 4;                    /* 1/4m → 1/64m */
    ce.anim++;
    if (ce.flash && ce.state != ST_ATTACK) ce.flash--;
    if (f->sway) ce.lat_t = (ce.anim & f->swaym) ? ((i16)f->sway << 4) : -((i16)f->sway << 4);
    if (f->bob) ce.up = ((i16)f->up << 4) + ((ce.anim & f->bobm) ? f->bob : 0);

    switch (ce.state) {
    case ST_DEAD:
        ce.up -= 24;
        move_enemy();
        if (++ce.timer >= 12) {
            if (IS_BOSS(ce.type)) boss_alive = 0;
#ifdef GAME_SEA
            if (ce.type == ET_B07) {             /* 大ダコ人が倒れたら、伸ばしていた足も海へ沈む */
                u8 i;
                for (i = 0; i < MAX_EN; i++)
                    if (en[i].type == ET_TENT && en[i].state != ST_DEAD) {
                        en[i].state = ST_DEAD; en[i].timer = 0; en[i].v = (i16)cam_v;
                    }
            }
#endif
            ce.type = ET_NONE;
        }
        return;
#ifdef GAME_SEA
    case ST_LEAP:
        /* 海から跳び上がる(前の半魚人はその場で、後ろの半魚人は乗り物について)。床に降りたら歩く敵(後ろなら追いかける) */
        ce.v = (d < 0) ? (i16)cam_v : 0;
        ce.up += (i8)ce.attacks;
        ce.attacks -= LEAP_G;
        if ((i8)ce.attacks < 0 && ce.up <= ((i16)f->up << 4)) {
            ce.up = (i16)f->up << 4;
            ce.attacks = 0;
            ce.state = (d < 0) ? ST_CHASE : ST_IDLE;
        }
        break;
#endif
    case ST_GRAB:
        /* 宝箱がカメラへ飛んできて消える(オープニング) */
        ce.v = (d < 0) ? (i16)cam_v + MPS(4) : (i16)cam_v - MPS(4);
        if (ce.up < F88(-0.6)) ce.up += 20;
        if (d > -(i16)M(0.8) && d < (i16)M(0.8)) {
            ce.type = ET_NONE;
            se_play(SE_HEAL);
            return;
        }
        break;
    case ST_CHASE:
        /* 後ろから走って追ってくる(歩く敵)。2m まで近づいたら殴る */
        if (d >= -(i16)M(2)) {
            ce.v = (i16)cam_v;
            attack(f, d);
        } else {
            /* トロッコより少し速く走って必ず追いつく(置いていかれたまま残って、やさしい進行を止めないように) */
            ce.v = (i16)cam_v + MPS(1.2);
            ce.flash = 0;
            ce.timer = 0;
        }
        break;
    case ST_IDLE:
        if (f->mv == MV_WALK && d > 0 && d < (i16)M(14)) {
            ce.state = ST_APPROACH;
            ce.v = -MPS(2.5);
            ce.lat_t = ce.side ? F88(-0.35) : F88(0.35);
        } else if (f->mv == MV_STAND && iabs(d) > (i16)M(6) && iabs(d) < (i16)M(24)) {
            attack(f, d);                         /* 立ったまま撃つ(弓兵) */
        }
        break;
    case ST_APPROACH:
        switch (f->mv) {
        case MV_WALK:
            if (d > 0 && d <= keep) { ce.state = ST_ATTACK; ce.timer = 0; }
            break;
        case MV_HOVER:
            if (d < -keep) ce.v = (i16)cam_v + MPS(3);
            else if (d > keep) ce.v = (i16)cam_v - MPS(3);
            else { ce.state = ST_ATTACK; ce.timer = 0; }
            break;
        case MV_KEEP:
            /* ボス: keep の距離(負 = 後ろ)まで寄ってくる */
            if (keep > 0 ? d > keep : d < keep) {
                ce.v = (i16)cam_v + (keep > 0 ? -MPS(2) : MPS(2));
            } else {
                ce.state = ST_ATTACK; ce.timer = 0; ce.attacks = 0;
            }
            break;
        case MV_SHOT:
            /* 敵弾: カメラへ向かってまっすぐ飛ぶ。手前 0.8m に来たら当たる */
            {
                i16 sv = (i16)f->keep * 546;
                ce.v = (d > 0) ? (i16)cam_v - sv : (i16)cam_v + sv;
            }
            if (ce.up > F88(-0.3)) ce.up -= 12;
            if (d < (i16)M(0.8) && d > -(i16)M(0.8)) {
                damage_player();
                ce.type = ET_NONE;
                return;
            }
            break;
        }
        break;
    case ST_ATTACK:
        if (f->mv == MV_WALK) {
            ce.v = (i16)cam_v;                        /* 張り付いて殴る。2 回殴ったら置いていかれる */
            attack(f, d);
            if (ce.attacks >= 2) { ce.state = ST_PASS; ce.v = 0; ce.flash = 0; }
            break;
        }
        {
            i16 tgt = keep;
            u8 n = 1;
            if (f->mv == MV_HOVER && d < 0) tgt = -keep;
            if (f->atk == AT_CYCLE && ce.attacks % 3 == 2) tgt = (keep > 0) ? (i16)M(2.5) : -(i16)M(2.5);
            ce.v = (i16)cam_v + ((tgt - d) << f->gain);
            if (ce.type == ET_B04 && ce.hp < 50) n = 2;          /* 亜人の王: 耐久が半分を切ると 2 倍の速さ */
            while (n--) {
                if (f->atk == AT_CYCLE && ce.attacks % 3 == 2) lunge(d);
                else attack(f, d);
            }
            /* 大猿人の親方: ときどき子分の猿人を呼ぶ */
            if (ce.type == ET_B01 && (ce.anim & 127) == 0) spawn(ET_E01, 14, (ce.anim & 128) ? 10 : -10);
#ifdef GAME_SEA
            if (ce.type == ET_B07) octo_arms();
#endif
        }
        break;
    }
    move_enemy();

    /* カメラの後方へ抜けた敵を消す(追跡型・張り付く敵・ボス・演出中の宝箱を除く) */
#ifdef GAME_SEA
    if (ce.state == ST_CHASE || ce.state == ST_LEAP) {
#else
    if (ce.state == ST_CHASE) {
#endif
        if (d < -(i16)M(24)) ce.type = ET_NONE;
    } else if (f->mv != MV_HOVER && f->mv != MV_KEEP && ce.type != ET_TREASURE) {
        if (d < -(i16)M(3)) ce.type = ET_NONE;
    }
    if (d > (i16)M(60) || d < -(i16)M(40)) ce.type = ET_NONE;
}

static void update_enemy(Enemy *e)
{
    ce_load(e);
    update_ce();
    ce_store(e);
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
            g_alt_n = rail_sample(stage->branch[dir ^ 1], g_alt, ALT_DRAW_N);
        }
    }
    for (i = 0; i < MAX_EN; i++)
        if (IS_SIGN(en[i].type)) en[i].type = ET_NONE;
}

/* 誘導弾(アフターバーナー II にならう)の状態。中身は fire の後ろ */
#define LOCK_MAX  4             /* 同時にロックできる数 */
#define MIS_START 3             /* ステージの始めの弾数 */
#define MIS_MAX   5             /* 持てる弾数 */
#define MIS_T     8             /* 撃ってから当たるまでのフレーム数 */
#define MIS_BOSS  5             /* ボスに与える傷(ふつうの弾の数) */
static u8 lock_n, lock_id[LOCK_MAX];            /* ロックした敵(古い順) */
static u8 fly_n, fly_id[LOCK_MAX], fly_t[LOCK_MAX], fly_x[LOCK_MAX], fly_y[LOCK_MAX];   /* 飛んでいる誘導弾(位置はブロック) */
static u8 box_x0[MAX_EN], box_y0[MAX_EN], box_x1[MAX_EN], box_y1[MAX_EN];             /* このフレームに描いた敵の外枠(0xFF = 無い) */
static u8 box_n, box_id[LOCK_MAX * 2];          /* 外枠を書いた敵(mis_draw が使い終わったら 0xFF に戻す) */

static void kill_enemy(Enemy *e)
{
    u8 t = e->type;
    if (IS_SIGN(t)) {                        /* 先に撃った側へ */
        se_play(SE_KILL);
        branch_pick(t == ET_SIGNR);
        return;
    }
    if (t == ET_MISBOX) {                    /* 誘導弾の箱: 1 発増える */
        if (mis_left < MIS_MAX) mis_left++;
        e->type = ET_NONE;
        se_play(SE_HEAL);
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
/* 照準ブロックに重なる矩形のうち最も手前の、撃てる敵の番号(いなければ 0xFF)。*hi = その矩形の hits の番号。
 * トロッコの縁に隠れた所は撃てない。照準の色(hud)と射撃(fire)で同じ判定を使う */
u8 aim_scan(u8 bx, u8 by);   /* aimhit.s: 照準ブロックに重なる最も手前の矩形の hits の番号(無ければ 0xFF) */
/* 照準のブロックの下の矩形(aim_scan の結果、0xFF = 無い)。当たり判定の表と乗り物の絵は描くときにしか変わらないので、
 * 描いてから次に描くまでは、同じブロックなら使い回す(照準の色・ロック・射撃で 1 フレームに 2〜3 回呼ぶ)。render で as_bx = 0xFF */
static u8 as_bx, as_by, as_i;          /* ステージの始めに as_bx = 0xFF */

static u8 aim_target(u8 *hi)
{
    u8 bx = (u8)(aim_x >> 2), by = (u8)(aim_y >> 2);
    u8 i, best;
    if (bx != as_bx || by != as_by) {
        as_bx = bx; as_by = by;
        as_i = trolley_covers(bx, by) ? 0xFF : aim_scan(bx, by);
    }
    i = as_i;
    if (i == 0xFF) return 0xFF;
    *hi = i;
    best = hits[i].id;
    if (best >= MAX_EN) return 0xFF;
    if (en[best].type == ET_NONE || en[best].state == ST_DEAD) return 0xFF;
    return best;
}

static void fire(void)
{
    u8 hi, best;
    se_play(SE_SHOT);
    shotfx = 1;
    best = aim_target(&hi);
    if (best == 0xFF) return;
    {
        Enemy *e = &en[best];
        HitRect *h = &hits[hi];
        g_pop_hx = (u8)((h->x0 + h->x1) >> 1);
        g_pop_hy = (h->y0 > 6) ? h->y0 - 6 : 0;
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
 * 誘導弾(アフターバーナー II にならう): 照準が通った敵を最大 4 体までロックし(「ピピ」)、A を押しながら B を押すたびに、
 * いちばん古いロックへ 1 発撃つ(「FIRE」。内蔵 PCM、pcm.s)。弾はステージの始めに 3 発、最大 5 発、残りより多くはロックしない。
 * 誘導弾の箱を撃つと 1 発増える。ロックの印と飛んでいく弾は描画バッファに描く(スプライトは常駐部とページ0に空きが無い)
 * --------------------------------------------------------------------------- */
/* 攻撃してくるもの(敵・ボス・敵弾)か */
#define IS_FOE(t) ((t) != ET_NONE && (t) != ET_TREASURE && (t) != ET_MEDKIT && (t) != ET_CRATE && (t) != ET_MISBOX && !IS_SIGN(t))

static u8 mis_targeted(u8 id)
{
    u8 i;
    for (i = 0; i < lock_n; i++) if (lock_id[i] == id) return 1;
    for (i = 0; i < fly_n; i++) if (fly_id[i] == id) return 1;
    return 0;
}

/* 照準の下の敵をロックする(player_update から。当たり判定は前のフレームの絵) */
static void mis_lock(void)
{
    u8 hi, t;
    if (lock_n >= LOCK_MAX || lock_n >= mis_left) return;
    t = aim_target(&hi);
    if (t == 0xFF || !IS_FOE(en[t].type) || IS_SHOT(en[t].type) || mis_targeted(t)) return;
    lock_id[lock_n++] = t;
    se_play(SE_LOCK);
}

/* A を押しながら B: いちばん古いロックへ 1 発 */
static void mis_fire(void)
{
    u8 i;
    if (!lock_n || !mis_left || fly_n >= LOCK_MAX) return;
    fly_id[fly_n] = lock_id[0];
    fly_t[fly_n] = 0;
    fly_x[fly_n] = 32;                   /* 乗り物から */
    fly_y[fly_n] = TROLLEY_Y;
    fly_n++;
    for (i = 1; i < lock_n; i++) lock_id[i - 1] = lock_id[i];
    lock_n--;
    mis_left--;
    pcm_say(VOICE_FIRE);                 /* Fire! はほかの声を止めて鳴らす */
    fire_t = g_tick;
}

/* 長方形を画面(64 × GAME_ROWS)に切り詰めて塗る */
static void mis_rect(i8 x, i8 y, u8 w, u8 h, u8 color)
{
    i8 x1 = x + (i8)w, y1 = y + (i8)h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > 64) x1 = 64;
    if (y1 > GAME_ROWS) y1 = GAME_ROWS;
    if (x >= x1 || y >= y1) return;
    g_rx = (u8)x; g_ry = (u8)y; g_rw = (u8)(x1 - x); g_rh = (u8)(y1 - y); g_color = color;
    rect_fill();
}

/* 当たった: ふつうの敵は倒れ、ボスは MIS_BOSS 発ぶんの傷 */
static void mis_hit(u8 id)
{
    Enemy *e = &en[id];
    g_pop_hx = (u8)((box_x0[id] + box_x1[id]) >> 1);
    g_pop_hy = (box_y0[id] > 6) ? box_y0[id] - 6 : 0;
    if (IS_BOSS(e->type)) e->hp = (e->hp > MIS_BOSS) ? e->hp - MIS_BOSS : 0;
    else e->hp = 0;
    if (!e->hp) kill_enemy(e);
    else { e->flash = 2; se_play(SE_HIT); }
}

/* 描いたあと(draw_hud から): 敵の外枠をまとめ、ロックを確かめて印を描き、誘導弾を進めて描き、残りの弾を描く */
static void mis_draw(void)
{
    u8 i;
    if (lock_n || fly_n) {
        /* 外枠は draw_enemy が、ロック中・飛行中の敵だけ描いた直後に作る */
        /* ロック: 倒れた・画面に映っていない敵は外す。残りに赤い枠 */
        for (i = 0; i < lock_n; ) {
            u8 id = lock_id[i];
            if (en[id].type == ET_NONE || en[id].state == ST_DEAD || box_x0[id] == 0xFF) {
                u8 j;
                for (j = i + 1; j < lock_n; j++) lock_id[j - 1] = lock_id[j];
                lock_n--;
                continue;
            }
            {
                i8 x0 = (i8)box_x0[id] - 2, y0 = (i8)box_y0[id] - 2;
                u8 w = (u8)(box_x1[id] - box_x0[id] + 4), hh = (u8)(box_y1[id] - box_y0[id] + 4);
                mis_rect(x0, y0, w, 1, C_RED);
                mis_rect(x0, y0 + (i8)hh - 1, w, 1, C_RED);
                mis_rect(x0, y0, 1, hh, C_RED);
                mis_rect(x0 + (i8)w - 1, y0, 1, hh, C_RED);
            }
            i++;
        }
        /* 誘導弾: 毎フレーム、残りのフレーム数で割った分だけ敵の今の位置へ寄せる(MIS_T フレームでちょうど当たる) */
        for (i = 0; i < fly_n; ) {
            u8 id = fly_id[i];
            if (en[id].type == ET_NONE || en[id].state == ST_DEAD || box_x0[id] == 0xFF) {
                u8 j;
                for (j = i + 1; j < fly_n; j++) {
                    fly_id[j - 1] = fly_id[j]; fly_t[j - 1] = fly_t[j]; fly_x[j - 1] = fly_x[j]; fly_y[j - 1] = fly_y[j];
                }
                fly_n--;
                continue;
            }
            {
                u8 k = MIS_T - fly_t[i];
                i16 tx = (box_x0[id] + box_x1[id]) >> 1, ty = (box_y0[id] + box_y1[id]) >> 1;
                fly_x[i] = (u8)(fly_x[i] + (tx - fly_x[i]) / k);
                fly_y[i] = (u8)(fly_y[i] + (ty - fly_y[i]) / k);
                mis_rect((i8)fly_x[i] - 1, (i8)fly_y[i] - 1, 2, 2, C_YELLOW);
                if (++fly_t[i] >= MIS_T) {
                    u8 j;
                    mis_hit(id);
                    for (j = i + 1; j < fly_n; j++) {
                        fly_id[j - 1] = fly_id[j]; fly_t[j - 1] = fly_t[j]; fly_x[j - 1] = fly_x[j]; fly_y[j - 1] = fly_y[j];
                    }
                    fly_n--;
                    continue;
                }
            }
            i++;
        }
    }
    for (i = 0; i < box_n; i++) box_x0[box_id[i]] = 0xFF;
    box_n = 0;
    /* 残りの誘導弾(左上) */
    for (i = 0; i < mis_left; i++) mis_rect((i8)(1 + i * 3), 1, 2, 4, C_YELLOW);
}

/* ---------------------------------------------------------------------------
 * スクリプト
 * --------------------------------------------------------------------------- */
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
        case EV_BOSS:
            /* ボスを出したら、倒すまで次の命令へ進まない(同じフレームで続きの「クリア」などを実行しない)。
             * 警告の向きはボスの出る側にする(ステージのスクリプトが向きを出し忘れても、後ろのボスに気づけるように) */
            spawn(ev->a, ev->b, 0);
            attack_dir = (ev->b < 0) ? DIR_BACK : DIR_FRONT;
            return;
        case EV_END:   sc_end = 1; break;
        case EV_TEXT:  cap_id = ev->a; cap_time = 40; break;
        case EV_SPEED: spd_over = ev->a; break;
        case EV_CTRL:  ctrl = ev->a; break;
        case EV_TURN:
            if (facing != ev->a) { facing = ev->a; turning = 8; }
            break;
        case EV_BGM:
            bgm_play(ev->a);                 /* 0 なら止まる */
            break;
        case EV_FX:
            if (ev->a) se_play(ev->a);
            shake = (u8)ev->b;
            break;
        case EV_VOICE: if (say(ev->a)) voice_t = g_tick; break;
        case EV_PAL:
            set_stage_palette(ev->a ? stage->palette2 : stage->palette);
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
        if ((edge & IN_A) && stage->skip_tick && sc_tick < stage->skip_tick) {
            sc_tick = stage->skip_tick - 2;
            if (cam_s < M(stage->skip_m)) cam_s = M(stage->skip_m);   /* 位置で進む演出も飛ばす */
        }
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

    /* 誘導弾: 照準の下の敵をロックし、A を押しながら B で撃つ */
    mis_lock();
    /* 振り向き: B ボタンでトグル(押した瞬間のみ)。8 フレームで 180 度、回転中は射撃不可。A を押している間の B は誘導弾 */
    if (turning) {
        look += 0x1000;
        if (--turning == 0) look = facing ? 0x8000 : 0;
    } else if (edge & IN_B) {
        if (in & IN_A) {
            mis_fire();
        } else {
            facing ^= 1;
            turning = 8;
        }
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
/* 選ばれていない側の線路(分岐点から 24m): レール 2 本と枕木を、地形の上に描く(g_alt は常駐部の fx.c。描く処理は常駐部に入らないのでここ) */
/* 変換と投影を済ませた点(隣の区間・同じ区間の四角形どうしで使い回す)。in = 0 なら手前に無い(投影していない) */
typedef struct { V3 v; i16 s[2]; u8 in; } AltP;

/* 点を求めるのは aimhit.s: alt_cen はレールの点の中心をカメラ空間へ、alt_mk_a は中心 g_al_cp(g_al_cp2 があれば
 * そこへ 1/8 進んだ点)から横位置の並び g_al_lat(g_al_n 個)の点を求めて投影し、o に並べる(向きはレールの点 p のヨー) */
extern const i16 *g_al_lat;
extern u8 g_al_n, g_al_dv;
extern i16 g_al_fy;
extern const V3 *g_al_cp, *g_al_cp2;
void alt_cen(const RailPt *p, V3 *o);
void alt_mk_a(const RailPt *p, AltP *o);
static void alt_mkn(const RailPt *p, const i16 *lat, u8 n, AltP *o)
{
    g_al_lat = lat;
    g_al_n = n;
    alt_mk_a(p, o);
}

/* 四角形 g_aq[0]→[1]→[2]→[3] を色 A で: 4 点とも手前なら投影済みの座標で塗り、そうでなければ頂点のポインタのまま poly3p で
 * 切って塗る(手前で切る poly3 と同じ順・同じ値になる)。aimhit.s の alt_qa(C で書くと 1 回 約 480 サイクルかかっていた) */
extern const AltP *g_aq[4];
void alt_qa(u8 color);
#define alt_q(p0, p1, p2, p3, color) do { g_aq[0] = (p0); g_aq[1] = (p1); g_aq[2] = (p2); g_aq[3] = (p3); alt_qa(color); } while (0)

/* 枕木の端(±180)とレールの両端(±128、±102)。区間ごとに、手前の点(a)の 6 つと枕木の奥の端 2 つだけを新しく求め、
 * 奥の点(b)のレールの 4 つは前の区間で求めたものを使う(1 フレーム 132 点 → 約 86 点)。
 * 選ばれていない側の線路はすぐ視界の外へ曲がっていくので、レールの点ごとに中心(横 0)だけを先に変換し、区間の両端の中心が
 * 同じ側(画面の左の外・右の外・後ろ)に余裕 ALT_MARGIN をもって出ていれば、その区間は点を求めずに飛ばす。
 * 枕木の端は中心から横に 180(0.7m)なので、カメラ空間の x と z はそれぞれ 180 までしか動かない。区間の途中の点(枕木の奥の端、
 * 手前で切った点)は両端の点の間にあるので、同じ側に出る。だから飛ばした区間は、描いても 1 ブロックも塗らない */
#define ALT_MARGIN 400
/* 枕木は、区間の頭の中心がカメラから 8m より近い区間だけ描く(遠くの枕木は 1〜2 ブロックの点にしかならない。
 * 分岐を過ぎた直後で 1 フレーム約 5ms 軽くなる。絵の違いは 1 枚あたり最大 2 ブロック) */
#define ALT_TIE_Z 2048
static const i16 ALT_LAT[6] = { -180, 180, -128, -102, 102, 128 };
static AltP alt_p0[6], alt_p1[6];
static u8 alt_side[ALT_N];
static V3 alt_cv[ALT_N];           /* レールの点の中心(カメラ空間) */

void alt_draw(void)
{
    u8 i, sa, sb, bok;
    AltP *pa = alt_p0, *pb = alt_p1, *pt;
    AltP mm[2];
    const RailPt *rp;
    const V3 *cv;
    u16 f = rail_fork_m() << 6;
    if (!g_alt_n || cam_s + M(40) < f || cam_s > f + M(30)) return;
    g_al_fy = FLOOR_Y;
    g_al_dv = 0;                                     /* カメラが変わったので、横の向きを求め直させる */
    for (i = 0, rp = g_alt, cv = alt_cv; i < g_alt_n; i++, rp++, cv++) {
        alt_cen(rp, (V3 *)cv);
        alt_side[i] = (cv->z < -ALT_MARGIN) ? 1 : (cv->x + cv->z < -ALT_MARGIN) ? 2 : (cv->x - cv->z > ALT_MARGIN) ? 3 : 0;
    }
    bok = 0;                                         /* pb(奥の点のレールの 4 つ)を求めてあるか */
    for (i = g_alt_n - 1, rp = &g_alt[i], cv = &alt_cv[i]; i > 0; i--, rp--, cv--) {   /* 奥から。区間は rp - 1 〜 rp */
        sa = alt_side[i - 1];
        sb = alt_side[i];
        if (sa && sa == sb) { bok = 0; continue; }     /* 区間ごと画面の外 */
        g_al_cp2 = 0;
        if (!bok) { g_al_cp = cv; alt_mkn(rp, &ALT_LAT[2], 4, &pb[2]); }
        g_al_cp = cv - 1;
        if ((cv - 1)->z < ALT_TIE_Z) {
            alt_mkn(rp - 1, ALT_LAT, 6, pa);
            g_al_cp2 = cv;                           /* 枕木: 区間の頭から 1/8 の奥の端(-180、180) */
            alt_mkn(rp - 1, ALT_LAT, 2, mm);
            alt_q(&pa[0], &pa[1], &mm[1], &mm[0], 9);
        } else {
            alt_mkn(rp - 1, &ALT_LAT[2], 4, &pa[2]);   /* レールの 4 つだけ */
        }
        alt_q(&pa[2], &pa[3], &pb[3], &pb[2], 10);
        alt_q(&pa[4], &pa[5], &pb[5], &pb[4], 10);
        pt = pa; pa = pb; pb = pt;
        bok = 1;
    }
}

/* 敵モデルを描く(本体は geom.s の draw_model) */
extern const MRect *g_dm_rects;
extern u8 g_dm_n, g_dm_flash, g_dm_id, g_dm_hit, g_dm_grow, g_dm_thin;
void draw_model(const V3 *c);

#define FAR_EN_Z 3000     /* 遠くの敵(約 12m より先) */
static void draw_enemy(u8 id)
{
    Enemy *e = &en[id];
    const Model *md;
    if (e->c.z < NEAR_Z + 32) return;
    md = (e->type == ET_E02 && (e->anim & 4)) ? &model_e02b : &models[e->type];
#ifdef GAME_SEA
    if (e->type == ET_TENT && e->c.x < 0) md = &model_tentl;   /* 画面の左の足は先を右(レールの側)へ丸める */
    if (e->state == ST_LEAP && e->up < LEAP_SHOW) return;   /* 跳び上がった半魚人は、頭が床より上に出てから */
#endif
    g_dm_rects = md->r;
    g_dm_n = md->n;
    /* 遠くの敵は、画面で半ブロックに満たない部品(四捨五入で出たり消えたりする細い部品)を描かない(geom.s)。
     * アイテム(救急箱・誘導弾の箱・宝箱)は、遠くでも十字や弾の色で見分けられるように省かない */
    g_dm_thin = (e->c.z > FAR_EN_Z && e->type != ET_MEDKIT && e->type != ET_CRATE && e->type != ET_MISBOX &&
                 e->type != ET_TREASURE) ? (u8)((u16)(e->c.z - 1) >> 10) : 0;
    /* 縁取りのルール: 敵・敵弾は白、救急箱と誘導弾の箱は黄。宝箱と分岐の獣人(文字で示す)は縁取りしない */
    if (!e->flash && e->type != ET_TREASURE && !IS_SIGN(e->type)) {
        g_dm_flash = (e->type == ET_MEDKIT || e->type == ET_CRATE || e->type == ET_MISBOX) ? C_YELLOW : C_WHITE;
        g_dm_hit = 0;
        g_dm_grow = 1;
        draw_model(&e->c);
        g_dm_grow = 0;
    }
    g_dm_flash = e->flash ? C_WHITE : 0;
    g_dm_id = id;
    g_dm_hit = e->type != ET_TREASURE && e->state != ST_DEAD;
    {
        u8 h0 = n_hits, sign = IS_SIGN(e->type);
        draw_model(&e->c);
        if (n_hits > h0 && (sign || ((lock_n || fly_n) && box_n < LOCK_MAX * 2 && mis_targeted(id)))) {
            /* 絵の外接矩形: 分岐の獣人のお腹の文字と、誘導弾(ロックの枠・弾の行き先)に使う */
            u8 k, x0 = 255, y0 = 255, x1 = 0, y1 = 0;
            for (k = h0; k < n_hits; k++) {
                if (hits[k].x0 < x0) x0 = hits[k].x0;
                if (hits[k].y0 < y0) y0 = hits[k].y0;
                if (hits[k].x1 > x1) x1 = hits[k].x1;
                if (hits[k].y1 > y1) y1 = hits[k].y1;
            }
            if (!sign) {
                box_x0[id] = x0; box_y0[id] = y0; box_x1[id] = x1; box_y1[id] = y1;
                box_id[box_n++] = id;
                return;
            }
            /* お腹の文字 L / R: 白地に黒で大きめに描く(遠くても読めるように) */
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
    /* 警告が出始めたら英語の声も(2 秒ごとのくり返しの警告音では鳴らさない) */
    /* ボス戦の間は鳴らさない(向きの声はボスの前に言い切る。ボス戦では振り向くたびに鳴って、声の間は重くなる) */
    if (warn && warn != warn_prev && !boss_alive && (u16)(g_tick - voice_t) > 45) say(warn == 1 ? VOICE_BEHIND : VOICE_AHEAD);
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
    mis_draw();
    if (!ctrl) { g_spr_attr[0] = 209; g_spr_attr[4] = 209; return; }
    {
        u8 hi;
        /* 撃てる敵に重なっていれば赤 */
        set_sprite(0, (i16)aim_x - 16, (i16)aim_y - 16, 0, aim_target(&hi) != 0xFF ? C_RED : C_WHITE);
    }
    /* 当たったら当たりの印、当たらなくても撃ったフレームは閃光(ふつうの弾は描かないので、撃ったことを照準で見せる) */
    if (hitmark) set_sprite(1, (i16)aim_x - 16, (i16)aim_y - 16, 4, C_YELLOW);
    else if (shotfx) set_sprite(1, (i16)aim_x - 16, (i16)aim_y - 16, 8, C_YELLOW);
    else g_spr_attr[4] = 209;
    shotfx = 0;
}

static void render(void)
{
    V3 p;
    u16 yaw;
    u16 t0;
    timer_reset();
    t0 = timer_read();
    as_bx = 0xFF;                        /* 当たり判定の表を作り直す */
    rail_at(cam_s, &p, &yaw);
    /* 坂: レールの傾きからピッチの目標(≒ atan、256 分割)を出し、1 フレーム 1 ずつ追う。
     * 後ろを向いているときは向きが逆、振り向き中はその間を cos で補間する */
    {
        u16 i = cam_s >> 7;
        i16 dy;
        i8 tgt;
        if (i + 1 >= rail_len / RAIL_STEP) i = rail_len / RAIL_STEP - 2;
        dy = (rail[i + 1].y - rail[i].y) >> 1;   /* 1m あたりの高さ変化(8.8)。点は 2m ごと */
        /* atan(x) ≒ x / (1 + 0.28x²)(急な坂でも破綻しない)。1 フレームで差の 1/4 ずつ追う。
         * dy だけで決まるので、前のフレームと同じ dy なら計算し直さない(32bit の割り算は R800 でも数千サイクル) */
        {
            static i16 last_dy;
            static i8 last_tgt;
            static u8 tgt_ok;                /* モジュールの変数は 0 で始まる(初期値つきは使えない) */
            if (!tgt_ok || dy != last_dy) {
                tgt_ok = 1;
                last_dy = dy;
                last_tgt = (i8)(((i32)dy * 41) / (256 + (((i32)dy * dy * 72) >> 16)));
            }
            tgt = last_tgt;
        }
        {
            i8 st = (i8)((tgt - pitch_cur) >> 2);
            if (!st) st = (tgt > pitch_cur) ? 1 : (tgt < pitch_cur) ? -1 : 0;
            pitch_cur += st;
        }
    }
    render_set_camera(p.x, p.y - EYE_DROP, p.z, yaw + look, (i8)fmul(pitch_cur, COS((u8)(look >> 8))));
    env_draw(cam_s, look >> 8);
    g_perf_env = timer_read() - t0;
    alt_draw();
    draw_enemies();
    /* コースターの車両。耐久が減るほど壊れた絵になる。振り向き中は横へ滑らせて一度画面外へ出し、反対側から戻す。
     * 見る向きが 90 度を越えたら(画面外に出ている間に)後ろを向いたときの絵に替える */
    trolley_set(life, (u8)((look >> 8) + 0x40) >= 0x80);
    {
        i16 slide = turning ? fmul(SIN((u8)(look >> 8)), 72) : 0;   /* 90 度で 72 ブロック = 画面外 */
        if (slide > 64) slide = 64; else if (slide < -64) slide = -64;
        trolley_draw((i8)slide, (shake & 2) ? 1 : 0);
    }
    debris_draw();
    popups_draw();
    if (shake) shake--;
    draw_hud();
    g_perf_render = timer_read() - t0;
}

/* ---------------------------------------------------------------------------
 * 1 プレイ
 * --------------------------------------------------------------------------- */
/* ---------------------------------------------------------------------------
 * ステージクリアの加算(悪魔城ドラキュラでボスを倒したときのように、数えながら足す)。ゲーム画面の上に重ねる。
 *   ボーナス(表示で 5,000 × ステージ。1 面より 2 面、2 面より 3 面が高い)を「ピッ」を連打しながら得点へ移し、
 *   ライフを 1 つずつ +2、誘導弾を 1 発ずつ +5(どちらも満タンで止める)。時間はティック(60Hz)で数える
 * --------------------------------------------------------------------------- */
#define TALLY_BONUS 500                  /* 1 ステージあたりのボーナス(g_score の単位 = 表示の 1/10) */
static void clear_tally(void)
{
    u16 bonus = (u16)g_stage * TALLY_BONUS, t0 = g_tick, t, step = (u16)g_stage * 20, next = 0;
    u8 phase = 0, add_life = 2, add_mis = 5;
    ctrl = 0;
    hold = 1;                            /* 乗り物はゆっくり止まる */
    attack_dir = DIR_NONE;
    cap_id = 0;
    lock_n = 0;
    bgm_play(SONG_CLEAR);
    for (;;) {
        t = g_tick - t0;
        if (t >= next) {
            switch (phase) {
            case 0:                      /* STAGE CLEAR だけ見せる */
                if (t >= 40) phase = 1;
                break;
            case 1:                      /* ボーナスを得点へ */
                if (bonus) {
                    u16 d = bonus < step ? bonus : step;
                    bonus -= d;
                    g_score += d;
                    se_play(SE_TALLY);
                    next = t + 1;
                } else {
                    phase = 2;
                    next = t + 20;
                }
                break;
            case 2:                      /* ライフ +2 */
                if (add_life && life < 8) {
                    life++;
                    add_life--;
                    se_play(SE_HEAL);
                    next = t + 10;
                } else {
                    phase = 3;
                    next = t + 15;
                }
                break;
            case 3:                      /* 誘導弾 +5 */
                if (add_mis && mis_left < MIS_MAX) {
                    mis_left++;
                    add_mis--;
                    se_play(SE_LOCK);
                    next = t + 8;
                } else {
                    phase = 4;
                }
                break;
            default:                     /* クリアの曲(約 7.5 秒)が終わるまで見せる */
                if (t >= 460) return;
            }
        }
        trolley_update();
        render();
        g_rx = 8; g_ry = 7; g_rw = 48; g_rh = 30; g_color = C_BLACK;   /* 文字の後ろの黒い板(背景に埋もれないように) */
        rect_fill();
        text_center(10, "STAGE CLEAR", C_YELLOW, 1);
        if (phase >= 1) {                /* 残りのボーナス(表示の単位。末尾の 0 は文字で) */
            text_draw(12, 18, "BONUS", C_WHITE, 1);
            num_draw(34, 18, bonus, 5, C_WHITE);
            if (bonus) text_draw(54, 18, "0", C_WHITE, 1);
        }
        if (phase >= 2) text_draw(12, 24, "LIFE +2", C_WHITE, 1);
        if (phase >= 3) text_draw(12, 30, "MISSILE +5", C_WHITE, 1);
        present();
    }
}

u8 game_run(const Stage *st, u8 first)
{
    u8 i, over = 0;
    fx_reset();                          /* 演出と、分岐の選ばれていない側の線路を消す(この後の分岐の準備で入れ直す) */
    rail_begin();
    rail_add(st->track);
    route = g_cfg.route ? g_cfg.route - 1 : st->branch_def;
    branch_open = 0;
    if (st->branch[0]) {                 /* 分岐: まず規定の側(隠しコンフィグで固定なら、その側)で作っておく */
        rail_fork();
        rail_add(st->branch[route]);
        rail_add(st->tail);
        branch_open = !g_cfg.route;
        g_alt_n = rail_sample(st->branch[route ^ 1], g_alt, ALT_DRAW_N);
    }
    {   /* 地形の区間と乱数の種を常駐部へ写してから、地形モジュールで作る(モジュールからはこのステージのデータが見えない) */
        const u8 *z = st->zones;
        u8 i = 1;
        g_env_zones[0] = *z++;
        do {
            g_env_zones[i++] = z[0];
            g_env_zones[i++] = z[1];
            z += 2;
        } while (z[-2] != 0xFF && i < ENV_ZONES_MAX - 1);
        g_env_seed = st->seed;
    }
    env_build();
    set_stage_palette(st->palette);
    for (i = 0; i < MAX_EN; i++) en[i].type = ET_NONE;
    n_hits = 0;
    as_bx = 0xFF;
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
    fire_cd = 0; hitmark = 0; shotfx = 0; voice_t = g_tick - 100; fire_t = g_tick - 100;
    lock_n = 0; fly_n = 0; box_n = 0;
    for (i = 0; i < MAX_EN; i++) box_x0[i] = 0xFF;
    if (first) { life = g_cfg.life; g_score = 0; mis_left = MIS_START; }   /* 誘導弾もライフと同じく持ち越す */
    attack_dir = DIR_NONE;
    boss_alive = 0;
    blink = 0;
    shake = 0;
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
        if (g_dbg_fire) { g_dbg_fire = 0; pcm_say(VOICE_FIRE); }   /* (調べ用)「FIRE」を鳴らす */
        render();
        present();
        if (!life) { over = 1; break; }
        if (sc_end && !boss_alive) break;
    }

    if (!over) clear_tally();
    g_spr_attr[0] = 209;
    g_spr_attr[4] = 209;
    bgm_stop();
    if (!over) return 1;
    /* ゲームオーバー(約 4 秒) */
    g_scene = 2;
    for (i = 0; i < 120; i++) {
        buf_fill(C_BLACK);
        if (i & 16) text_center(12, "GAME", C_RED, 2);
        if (i & 16) text_center(24, "OVER", C_RED, 2);
        text_center(38, "COASTER BROKEN", C_YELLOW, 1);
        present();
    }
    return 0;
}
