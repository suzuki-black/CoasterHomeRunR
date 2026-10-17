/* sound.h — サウンド(仕様書 7.8)。BGM = FM 音源(MSX-MUSIC)と PSG の ch A・B、効果音 = PSG の ch C */
#ifndef SOUND_H
#define SOUND_H
#include "engine.h"

enum { SE_NONE, SE_HEAL, SE_SHOT, SE_HIT, SE_ALARM, SE_KILL, SE_TRAIN, SE_DAMAGE, SE_LOCK, SE_TALLY, SE_COUNT };

void sound_init(void);
void se_play(u8 id);
/* 音のモジュール(セグメント SEG_SND)を読み込んだら 1 にする(それまで割り込みは音の処理を呼ばない) */
extern u8 g_snd_ok;

/* BGM: FM 音源の曲(src/sound/songs.mml を tools/gen_music.py が音のモジュールにする)。番号は songs.h の SONG_、0 = 無し */
#include "songs.h"
void bgm_play(u8 song);   /* sys.s の中継が音のモジュールを呼ぶ */
void bgm_stop(void);

#endif
