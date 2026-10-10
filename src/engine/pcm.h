/* pcm.h — 誘導弾の声を、ゲームを止めずにターボ R の内蔵 PCM で鳴らす(実体は pcm.s。仕組みはそちらの冒頭) */
#ifndef PCM_H_INC
#define PCM_H_INC
#include "engine.h"

extern u8 pcm_mode;          /* 0 = ふだん / 1 = 走査線割込みで鳴らしている */
extern u8 g_dbg_fire;        /* (調べ用)1 を書くと game.c が「FIRE」を鳴らす */
void pcm_fire(const u8 *p, u16 len);   /* p は地形モジュールの中。次の垂直帰線から鳴らす(鳴っていれば頭から差し替える) */
void pcm_fire_voice(void);             /* 誘導弾の声「FIRE」(地形モジュールの voice_fire.s)を鳴らす */
void pcm_stop(void);                   /* すぐ止めて、ふだんの割込みに戻す */

#endif
