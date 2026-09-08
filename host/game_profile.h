#pragma once
#include <stdint.h>
void host_game_profile_start(void *runtime,const char *game);
void host_game_profile_tick(void *runtime,uint64_t frame_start,uint64_t audio_us,uint64_t video_us,uint64_t runtime_us,int presented);
