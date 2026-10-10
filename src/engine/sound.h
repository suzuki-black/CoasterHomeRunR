/* sound.h — PSG サウンド(仕様書 7.8)。ch A/B = BGM、ch C = 効果音 */
#ifndef SOUND_H
#define SOUND_H
#include "engine.h"

enum { SE_NONE, SE_HEAL, SE_SHOT, SE_HIT, SE_ALARM, SE_KILL, SE_TRAIN, SE_DAMAGE, SE_LOCK, SE_COUNT };

void sound_init(void);
void se_play(u8 id);
void sound_tick(void);   /* 割り込みから 60Hz で呼ばれる */

/* BGM: 1 チャンネル = (音, 長さ[ティック])の並び。音 = MIDI の音番号(24〜95)、0 = 休み、0xFF = 先頭へ戻る */
void bgm_play(const u8 *melody, const u8 *bass);
void bgm_stop(void);

#endif
