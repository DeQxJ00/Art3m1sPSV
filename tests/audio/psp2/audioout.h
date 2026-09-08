#pragma once
#define SCE_AUDIO_OUT_PORT_TYPE_MAIN 0
#define SCE_AUDIO_OUT_MODE_STEREO 1
#define SCE_AUDIO_VOLUME_0DB 32768
#define SCE_AUDIO_VOLUME_FLAG_L_CH 1
#define SCE_AUDIO_VOLUME_FLAG_R_CH 2
int sceAudioOutOpenPort(int,int,int,int);
int sceAudioOutSetVolume(int,int,int*);
int sceAudioOutOutput(int,const void*);
int sceAudioOutReleasePort(int);
