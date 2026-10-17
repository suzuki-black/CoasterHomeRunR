/* sndtick.c — 音のモジュールの 1 ティック(割り込みから 60Hz。ページ2 = 音のセグメント SEG_SND)
 *
 *   PSG ch A: 曲の PSG のパート(FM のパートにわずかにずらした音程で重ねる「デチューン」。YM2413 らしさを薄める)
 *   PSG ch B: ドラム(FM のリズムに重ねる。sndfm.s が g_drm に打った音を入れる)。スネア・シンバル・ハイハットはノイズ、
 *             バスドラムは音程が下がる短い音(「ドン」。YM2413 のバスドラムだけでは軽い)
 *   PSG ch C: 効果音(1 フレーム 4 バイト: 周期(下位, 上位)、音量(0-15、0xFF で終端)、ノイズ周期(0xFF = ノイズなし)。周期 0 = トーンなし)
 *   FM: 曲(sndfm.s)
 * 割り込みは Z80 転送の間(CPU が Z80)にも入るので、R800 専用の命令は使わない(Makefile の Z80_SAFE_C で確かめる)。
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
static const u8 se_lock[] = {           /* 誘導弾のロック: 「ピピ」 */
    0x30, 0, 13, 0xFF,  0x30, 0, 12, 0xFF,  0, 0, 0, 0xFF,  0x30, 0, 13, 0xFF,  0x30, 0, 11, 0xFF,  0, 0, 0xFF, 0 };
static const u8 se_tally[] = {          /* ステージクリアの加算: 短い「ピッ」(連打して「とぅるるる」) */
    0x50, 0, 11, 0xFF,  0x50, 0, 7, 0xFF,  0, 0, 0xFF, 0 };

static const u8 * const se_tab[SE_COUNT] = {
    0, se_heal, se_shot, se_hit, se_alarm, se_kill, se_train, se_damage, se_lock, se_tally };

extern const u8 g_se_pri[SE_COUNT];      /* 常駐部(sound.c) */
extern volatile u8 g_se_req;

/* sndfm.s */
void fm_bgm_tick(void);
void fm_play(u8 song);
void fm_stop(void);
extern const u8 *g_psg_top;              /* 曲の PSG のパート(無ければ 0)。fm_play が見出しから読む */
u8 g_drm;                                /* このティックに FM のリズムが打った音(bit3 スネア、bit1 シンバル、bit0 ハイハット) */

static const u8 *se_ptr;
static u8 se_cur;

/* ---- PSG のパート(ch A) ----
 * データ(tools/gen_music.py): 0x90|周期の上位 4 ビット, 周期の下位, 長さ = 音(デチューンは周期に入れてある)、0x80 n = 休み、
 *   0x85 n = 今の音を伸ばす、0x81 v = 音量、0x82 g = 音の終わりの g ティック前に切る、0x88 d = d ティックごとに音量を 1 下げる
 *   (0 = 下げない)、0x83 = ループで戻る所、0xFF = ループへ、0xFE = 終わり */
static const u8 *pa_p, *pa_loop;
static u8 pa_wait, pa_vol, pa_cur, pa_dec, pa_dcnt, pa_gate, pa_on;

static void psg_part(void)
{
    const u8 *p = pa_p;
    u8 c;
    if (!p) return;
    if (--pa_wait) {
        if (!pa_on) return;
        if (pa_gate && pa_wait == pa_gate && *p != 0x85) {
            pa_on = 0;
            psg_write(8, 0);
            return;
        }
        if (pa_dec && !--pa_dcnt) {
            pa_dcnt = pa_dec;
            if (pa_cur) psg_write(8, --pa_cur);
        }
        return;
    }
    for (;;) {
        c = *p++;
        if ((c & 0xF0) == 0x90) {
            psg_write(0, p[0]);
            psg_write(1, c & 0x0F);
            pa_wait = p[1];
            p += 2;
            pa_cur = pa_vol;
            pa_dcnt = pa_dec;
            psg_write(8, pa_cur);
            pa_on = 1;
            break;
        }
        if (c == 0x80) { pa_wait = *p++; pa_on = 0; psg_write(8, 0); break; }
        if (c == 0x85) { pa_wait = *p++; break; }
        if (c == 0x81) { pa_vol = *p++; continue; }
        if (c == 0x82) { pa_gate = *p++; continue; }
        if (c == 0x88) { pa_dec = *p++; continue; }
        if (c == 0x83) { pa_loop = p; continue; }
        if (c == 0xFF) { p = pa_loop ? pa_loop : g_psg_top; continue; }
        pa_p = 0;                                /* 0xFE: 終わり */
        pa_on = 0;
        psg_write(8, 0);
        return;
    }
    pa_p = p;
}

/* ---- ドラム(ch B) ----
 * ノイズ: スネアは強く短く、シンバルは長く、ハイハットは弱くごく短く。鳴っている音より強い音だけが上書きする。
 * バスドラム: 約 150Hz から 60Hz へ 5 ティックで下がるトーン(音量は 15 から下げる)。ch B の音量はノイズと共有なので大きいほう */
static u8 dr_vol, dr_dec, dr_np, dr_off, kk_t;
static const u8 kk_per[5][2] = { { 0xE9, 0x02 }, { 0xA0, 0x03 }, { 0x7A, 0x04 }, { 0xBE, 0x05 }, { 0x48, 0x07 } };
static const u8 kk_vol[5] = { 15, 14, 12, 10, 7 };

void sndm_play(u8 song)
{
    fm_play(song);
    pa_p = g_psg_top;
    pa_loop = 0;
    pa_wait = 1;
    pa_vol = 10;
    pa_dec = 0;
    pa_gate = 0;
    pa_on = 0;
    psg_write(8, 0);
}

void sndm_stop(void)
{
    fm_stop();
    pa_p = 0;
    pa_on = 0;
    psg_write(8, 0);
}

void sndm_tick(void)
{
    u8 req = g_se_req, mix = 0xBF, se_noise = 0, d;
    fm_bgm_tick();
    psg_part();
    if (pa_on) mix &= ~0x01;
    if (req) {
        g_se_req = 0;
        if (!se_ptr || g_se_pri[req] >= g_se_pri[se_cur]) {
            se_ptr = se_tab[req];
            se_cur = req;
        }
    }
    if (se_ptr) {
        if (se_ptr[2] == 0xFF) {
            psg_write(10, 0);
            se_ptr = 0;
            se_cur = 0;
        } else {
            if (se_ptr[0] | se_ptr[1]) {
                psg_write(4, se_ptr[0]);
                psg_write(5, se_ptr[1]);
                mix &= ~0x04;
            }
            if (se_ptr[3] != 0xFF) {
                psg_write(6, se_ptr[3]);
                mix &= ~0x20;
                se_noise = 1;
            }
            psg_write(10, se_ptr[2]);
            se_ptr += 4;
        }
    }
    /* ドラムのノイズ。ノイズの周期は効果音と共有なので、効果音が使っていないときだけ書く */
    d = g_drm;
    if (d) {
        g_drm = 0;
        if (d & 0x10) kk_t = 5;                                                        /* バスドラム */
        if ((d & 0x08) && dr_vol <= 13) { dr_vol = 13; dr_dec = 3; dr_np = 6; }        /* スネア */
        else if ((d & 0x02) && dr_vol <= 11) { dr_vol = 11; dr_dec = 1; dr_np = 2; }   /* シンバル */
        else if ((d & 0x01) && dr_vol <= 8) { dr_vol = 8; dr_dec = 4; dr_np = 1; }     /* ハイハット */
    }
    if (dr_vol || kk_t) {
        u8 v = dr_vol;
        if (dr_vol) {
            if (!se_noise) psg_write(6, dr_np);
            mix &= ~0x10;
            dr_vol = (dr_vol > dr_dec) ? dr_vol - dr_dec : 0;
        }
        if (kk_t) {
            const u8 *k = kk_per[5 - kk_t];
            psg_write(2, k[0]);
            psg_write(3, k[1]);
            mix &= ~0x02;
            if (kk_vol[5 - kk_t] > v) v = kk_vol[5 - kk_t];
            kk_t--;
        }
        psg_write(9, v);
        dr_off = 1;
    } else if (dr_off) {
        psg_write(9, 0);
        dr_off = 0;
    }
    psg_write(7, mix);
}
