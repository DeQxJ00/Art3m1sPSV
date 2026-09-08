#pragma once
#include <stdint.h>
unsigned host_gxm_video_rgba(unsigned,int,int,const uint8_t*);
void host_gxm_video_delete(unsigned);
void host_gxm_video_draw(unsigned,float,float);
