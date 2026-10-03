/* sound.c — PSG 効果音ドライバ(仕様書 7.8)
 *
 * ch A/B は BGM、ch C は効果音専用。効果音は 1 フレーム 4 バイト:
 *   周期(下位, 上位)、音量(0-15、0xFF で終端)、ノイズ周期(0xFF = ノイズなし)。周期 0 = トーンなし。
 * 優先度: 被弾 > 体当たり(予備) > 敵撃破 > 警告 > 射撃・命中 > 救急箱。低い音は高い音を上書きしない。
 * PSG は割り込み側だけが書く。本体は se_play で予約するだけ。
 * R#7 の bit7=1(ポート B 出力)・bit6=0(ポート A 入力)はジョイスティックのために常に保つ。
 */
#include "sound.h"

static const u8 se_heal[] = {
    0x60, 0, 12, 0xFF,  0x60, 0, 12, 0xFF,  0x48, 0, 12, 0xFF,  0x48, 0, 12, 0xFF,
    0x36, 0, 12, 0xFF,  0x36, 0, 10, 0xFF,  0x36, 0, 6, 0xFF,  0, 0, 0xFF, 0 };
static const u8 se_shot[] = {
    0, 0, 15, 4,  0, 0, 13, 6,  0, 0, 10, 9,  0, 0, 6, 12,  0, 0, 0xFF, 0 };
static const u8 se_hit[] = {
    0x80, 0, 13, 0xFF,  0xA0, 0, 11, 0xFF,  0, 0, 0xFF, 0 };
static const u8 se_alarm[] = {          /* 警告: 高い音 2 発 */
    0x38, 0, 13, 0xFF,  0x38, 0, 13, 0xFF,  0x38, 0, 12, 0xFF,  0x38, 0, 11, 0xFF,  0x38, 0, 0, 0xFF,
    0x38, 0, 0, 0xFF,   0x2C, 0, 13, 0xFF,  0x2C, 0, 13, 0xFF,  0x2C, 0, 12, 0xFF,  0x2C, 0, 10, 0xFF,
    0x2C, 0, 6, 0xFF,   0, 0, 0xFF, 0 };
static const u8 se_kill[] = {
    0x00, 1, 14, 20,  0x40, 1, 13, 22,  0x80, 1, 12, 24,  0xC0, 1, 11, 26,
    0x00, 2, 10, 28,  0x40, 2, 8, 30,  0x80, 2, 6, 31,  0xC0, 2, 3, 31,  0, 0, 0xFF, 0 };
static const u8 se_train[] = {
    0x00, 8, 15, 31,  0x00, 8, 15, 31,  0x00, 9, 14, 31,  0x00, 9, 13, 31,
    0x00, 10, 12, 31,  0x00, 10, 10, 31,  0x00, 11, 8, 31,  0x00, 11, 5, 31,  0, 0, 0xFF, 0 };
static const u8 se_damage[] = {
    0x00, 4, 15, 31,  0x40, 4, 15, 28,  0x80, 4, 14, 31,  0xC0, 4, 13, 28,
    0x00, 5, 12, 31,  0x40, 5, 10, 28,  0x80, 5, 8, 31,  0xC0, 5, 5, 28,  0, 0, 0xFF, 0 };

static const u8 * const se_tab[SE_COUNT] = {
    0, se_heal, se_shot, se_hit, se_alarm, se_kill, se_train, se_damage };
/* 優先度(大きいほど強い) */
static const u8 se_pri[SE_COUNT] = { 0, 1, 2, 2, 3, 4, 5, 6 };

/* ---------------------------------------------------------------------------
 * BGM(ch A = メロディ、ch B = ベース)
 *   音の頭で音量を上げ、ティックごとに少しずつ下げる(減衰)。最後の 2 ティックは切って刻みを出す
 * --------------------------------------------------------------------------- */
extern const u16 psg_period[72];         /* MIDI 24〜95 */

typedef struct {
    const u8 *top, *p;
    u8 wait, len, vol, peak, reg;
} BgmCh;
static BgmCh bgm[2];
static volatile u8 bgm_on;

void bgm_play(const u8 *melody, const u8 *bass)
{
    bgm_on = 0;
    bgm[0].top = bgm[0].p = melody; bgm[0].wait = 1; bgm[0].peak = 12; bgm[0].reg = 0; bgm[0].vol = 0;
    bgm[1].top = bgm[1].p = bass;   bgm[1].wait = 1; bgm[1].peak = 11; bgm[1].reg = 2; bgm[1].vol = 0;
    bgm_on = 1;
}

void bgm_stop(void)
{
    bgm_on = 0;
    psg_write(8, 0);
    psg_write(9, 0);
}

static void bgm_tick(void)
{
    u8 c;
    for (c = 0; c < 2; c++) {
        BgmCh *b = &bgm[c];
        if (--b->wait == 0) {
            u8 note = b->p[0];
            if (note == 0xFF) { b->p = b->top; note = b->p[0]; }
            b->wait = b->len = b->p[1];
            b->p += 2;
            if (note) {
                u16 per = psg_period[note - 24];
                psg_write(b->reg, (u8)per);
                psg_write(b->reg + 1, (u8)(per >> 8));
                b->vol = b->peak;
            } else {
                b->vol = 0;
            }
        } else {
            if (b->wait <= 2 && b->len > 4) b->vol = 0;          /* 音の終わりを切る */
            else if (b->vol > b->peak - 4 && !(b->wait & 3)) b->vol--;   /* ゆるい減衰 */
        }
        psg_write(8 + c, b->vol);
    }
}

static volatile u8 se_req;
static const u8 *se_ptr;
static u8 se_cur;

void sound_init(void)
{
    u8 r;
    for (r = 0; r < 14; r++) psg_write(r, 0);
    psg_write(7, 0xBF & ~0x03);  /* ch A/B のトーン(BGM)。bit7=1 bit6=0 */
    se_req = 0; se_ptr = 0; se_cur = 0;
    bgm_on = 0;
}

void se_play(u8 id)
{
    u8 pend = se_req;
    if (pend && se_pri[pend] > se_pri[id]) return;
    se_req = id;
}

void sound_tick(void)
{
    u8 req = se_req;
    if (bgm_on) bgm_tick();
    if (req) {
        se_req = 0;
        if (!se_ptr || se_pri[req] >= se_pri[se_cur]) {
            se_ptr = se_tab[req];
            se_cur = req;
        }
    }
    if (se_ptr) {
        u8 mix = 0xBF & ~0x03;          /* A/B トーンは BGM 用に開けておく */
        if (se_ptr[2] == 0xFF) {
            psg_write(10, 0);
            psg_write(7, mix);
            se_ptr = 0;
            se_cur = 0;
            return;
        }
        if (se_ptr[0] | se_ptr[1]) {
            psg_write(4, se_ptr[0]);
            psg_write(5, se_ptr[1]);
            mix &= ~0x04;
        }
        if (se_ptr[3] != 0xFF) {
            psg_write(6, se_ptr[3]);
            mix &= ~0x20;
        }
        psg_write(7, mix);
        psg_write(10, se_ptr[2]);
        se_ptr += 4;
    }
}
