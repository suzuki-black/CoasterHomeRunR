/* sound.c — サウンドの常駐部(仕様書 7.8): 効果音の予約と初期化だけ
 *
 * 鳴らす処理(効果音・ドラムのノイズ・PSG のパート・FM の曲)は、音のモジュール(セグメント SEG_SND の 0x8000-)にある
 * (sndtick.c・sndfm.s。割り込みはページ2を SEG_SND にしてから 0x8000 を呼ぶ。曲を鳴らす・止めるのは sys.s の中継)。
 * 優先度: ボス撃破 > 被弾 > 体当たり(予備) > 敵撃破 > 警告 > 射撃・命中・ロック > 救急箱。低い音は高い音を上書きしない。
 * PSG は割り込み側だけが書く。本体は se_play で予約するだけ。
 */
#include "sound.h"

/* 優先度(大きいほど強い)。ロックは射撃と同じ。ボスの撃破は一番強い(ほかの音に消されない)。音のモジュールも見る */
const u8 g_se_pri[SE_COUNT] = { 0, 1, 2, 2, 3, 4, 5, 6, 2, 1, 7 };
volatile u8 g_se_req;
u8 g_snd_ok;

extern u8 g_fmw_n;                       /* sys.s の fm_write の待ちのループの回数 */

void sound_init(void)
{
    u8 r;
    g_fmw_n = 50;                        /* R800 で値を書いた後 約 28µs(sys.s の FMW_WAIT) */
    for (r = 0; r < 14; r++) psg_write(r, 0);
    psg_write(7, 0xBF);          /* トーン・ノイズとも止める(鳴らすときだけ開ける)。bit7=1(ポート B 出力)bit6=0(ポート A 入力) */
    g_se_req = 0;
    bgm_stop();
}

void se_play(u8 id)
{
    u8 pend = g_se_req;
    if (pend && g_se_pri[pend] > g_se_pri[id]) return;
    g_se_req = id;
}
