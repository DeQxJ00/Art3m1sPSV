#pragma once
#include <psp2/gxm.h>
#include <cstdint>
#include <cstddef>
#include "effects.hpp"
#include "quad_trim.hpp"
namespace direct {
struct Texture { SceGxmTexture descriptor{}; int uid=-1; uint8_t* pixels=nullptr; unsigned w=0,h=0,stride=0; AlphaBounds alphaBounds; };
struct FrameStats { unsigned quads=0,draws=0,uniforms=0,plainQuads=0;
    unsigned zeroAlpha=0,outside=0,empty=0,trimmed=0; double areaBefore=0,areaAfter=0; };
FrameStats last_frame_stats();
bool init(); void prepare_process_exit(); void begin(); void end(); void wait(); bool in_scene();
Texture* texture(unsigned w,unsigned h,const uint8_t* rgba);
Texture* import_texture(const SceGxmTexture& t);
bool update(Texture*,const uint8_t*,unsigned x,unsigned y,unsigned w,unsigned h);
void destroy(Texture*);
void draw_quad(Texture*,const Vertex* vertices,unsigned blend=0,const float* clip=nullptr,
          Texture* rule=nullptr,float progress=0,float vague=1.0f/255,const Effects* effects=nullptr);
// Temporary group targets are allocated lazily and reused by nesting depth.
bool group_begin(); bool group_mask_begin();
void group_end(const Effects&,unsigned blend,const float* clip,Texture* mask,
               float r,float g,float b,float alpha);
void rect(float x,float y,float w,float h,uint32_t rgba);
bool readback(unsigned w,unsigned h,uint8_t* out);
Texture* white();
void menu_text(float x,float y,float size,const char* utf8,uint32_t color=0xffffffff);
void menu_prepare(const char* utf8,float size); void menu_release();
void log(const char* format,...);
}
