#pragma once
#include <stdint.h>
struct EmRenderPacket;
#ifdef DIRECT_RECOVERED_PACKET
#include "texture_host.h"
#else
struct EmTextureHost;
#endif
namespace direct {
// Render-thread only. Registry outlives every recovery resource/packet lease.
const EmTextureHost* recovered_texture_host();
int prepare_recovered_packet(const EmRenderPacket*);
int replay_recovered_packet(const EmRenderPacket*,const float* affine,float opacity,const float* clip);
}
