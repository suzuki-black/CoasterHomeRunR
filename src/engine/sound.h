/* sound.h — サウンド(仕様書 7.8)。BGM = FM 音源(MSX-MUSIC)、効果音 = PSG の ch C */
#ifndef SOUND_H
#define SOUND_H
#include "engine.h"

enum { SE_NONE, SE_HEAL, SE_SHOT, SE_HIT, SE_ALARM, SE_KILL, SE_TRAIN, SE_DAMAGE, SE_LOCK, SE_TALLY, SE_COUNT };

void sound_init(void);
void se_play(u8 id);
void sound_tick(void);   /* 割り込みから 60Hz で呼ばれる */

/* BGM: FM 音源の曲(src/sound/songs.mml を tools/gen_music.py が音のモジュールにする)。番号は songs.h の SONG_、0 = 無し */
#include "songs.h"
void bgm_play(u8 song);
void bgm_stop(void);

#endif
