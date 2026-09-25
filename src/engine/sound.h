/* sound.h — PSG サウンド(仕様書 7.8)。試作版は効果音のみ(ch C) */
#ifndef SOUND_H
#define SOUND_H
#include "engine.h"

enum { SE_NONE, SE_HEAL, SE_SHOT, SE_HIT, SE_KILL, SE_TRAIN, SE_DAMAGE, SE_COUNT };

void sound_init(void);
void se_play(u8 id);
void sound_tick(void);   /* 割り込みから 60Hz で呼ばれる */

#endif
