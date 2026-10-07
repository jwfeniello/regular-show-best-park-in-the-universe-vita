#pragma once
#include <stdbool.h>
void park_audio_init(void);
void park_audio_command(int op,const char *path,int id,bool loop,float gain);
int park_audio_effect(const char *path,bool loop,float gain);
void park_audio_volume(bool music,float value);
float park_audio_get_volume(bool music);
bool park_audio_playing(void);
enum { AUDIO_MUSIC=1,AUDIO_MUSIC_STOP,AUDIO_MUSIC_PAUSE,AUDIO_MUSIC_RESUME,AUDIO_REWIND,AUDIO_PRELOAD,AUDIO_EFFECT,AUDIO_STOP,AUDIO_PAUSE,AUDIO_RESUME,AUDIO_STOP_ALL,AUDIO_PAUSE_ALL,AUDIO_RESUME_ALL };
