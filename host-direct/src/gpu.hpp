#pragma once
#include "shader_progress.hpp"
#include "shader_settings.hpp"
#include <psp2/gxm.h>
#include <cstdint>
#include <cstddef>
#include <string>
#include "quad_trim.hpp"
#include "builtin_effects.hpp"
#include "opaque_tiles.hpp"
#include "resource_ledger.hpp"
namespace direct {
struct Texture { SceGxmTexture descriptor{}; AllocationCharge allocation; int uid=-1; uint8_t* pixels=nullptr; unsigned w=0,h=0,stride=0; AlphaBounds alphaBounds; bool opaque=false; uint64_t contentRevision=0; OpaqueTiles opaqueTiles; };
struct FrameStats { unsigned quads=0,draws=0,uniforms=0,plainQuads=0;
    unsigned zeroAlpha=0,outside=0,empty=0,trimmed=0,opaqueQuads=0; double areaBefore=0,areaAfter=0,opaqueArea=0; };
FrameStats last_frame_stats();
bool init(); void prepare_process_exit(); void begin(bool preserveCompiler=false); void end(); void wait(); bool in_scene();
#ifdef DIRECT_DEFERRED_FINISH_PROBE
// Explicit experimental probe/candidate only. Default hosts keep end waits.
enum class WaitSite { End, Begin, Update, Destroy, Readback, Explicit, Mode, Count };
struct WaitStats { uint64_t calls[unsigned(WaitSite::Count)]{}, microseconds[unsigned(WaitSite::Count)]{}; };
bool set_deferred_finish(bool enabled); // Refuses changes inside an open scene.
WaitStats deferred_wait_stats();
#endif
Texture* texture(unsigned w,unsigned h,const uint8_t* rgba,const uint8_t* proof=nullptr,size_t proofCount=0);
// Render-thread only: private packed RGBA staging, then seal before publication.
Texture* surface_prepare(unsigned w,unsigned h);
bool surface_seal(Texture*,const uint8_t* proof,size_t proofCount);
void surface_abort(Texture*);
bool shared_surface_self_test();
bool shared_surface_allowed();
bool prepare_opacity(unsigned w,unsigned h,const uint8_t* rgba,uint8_t* proof,size_t count);
Texture* import_texture(const SceGxmTexture& t);
// Main thread, outside a scene. Four tightly packed W*H byte planes Y/U/V/A.
// Default returns completed RGBA; opt-in overlap on copied reused input returns
// pending output. Rendering/CPU views/overwrite/release must drain it first.
// Caller owns new output, existing stays owned.
Texture* video_yuva_convert(Texture* existing,unsigned w,unsigned h,const uint8_t* planes,bool overlap=false);
void video_yuva_release(); // releases conversion staging only, not published output
bool video_yuva_self_test(); // actual offscreen pixels; fail closed if unreadable
// Main-thread allocation, producer writes only; no worker GXM calls. Close
// after joining the producer and returning the synchronous conversion loan.
bool video_yuva_queue_open(unsigned w,unsigned h,uint8_t** slots,unsigned count);
void video_yuva_queue_close();
bool update(Texture*,const uint8_t*,unsigned x,unsigned y,unsigned w,unsigned h);
void destroy(Texture*);
void draw_quad(Texture*,const Vertex* vertices,unsigned blend=0,const float* clip=nullptr,
          Texture* rule=nullptr,float progress=0,float vague=1.0f/255);
void draw_builtin(Texture*,const Vertex*,size_t count,bool triangles,unsigned blend,
                  const float* clip,Texture* mask,const BuiltinEffects&);
void external_cache_root(const std::string&);
void external_shader_options(ShaderSettings);
bool external_conversion_enabled();
bool external_cache_path(const char*,char*,size_t);
void external_compiler_end();
bool external_shader_self_test();
bool bundled_shader_self_test();
unsigned external_compile(const char*,const char*,const char*);
unsigned external_register(const uint8_t*,size_t);
bool external_uniform(unsigned,const char*,unsigned,unsigned);
void external_release(unsigned);
void draw_external(Texture*,const Vertex*,size_t,bool,unsigned,const float*,Texture*,Texture*,const CustomDraw&);
bool group_filter(const EffectDraw&,Texture* mask,Texture* user,float sx,float sy);
bool group_begin(); bool group_mask_begin();
void group_end(const EffectDraw&,Texture* mask,float sx,float sy,Texture* user=nullptr);
bool group_end_cached(const EffectDraw&,float sx,float sy,unsigned slot=0,Texture* mask=nullptr);
bool draw_cached_group(unsigned slot=0);
bool retained_self_test();
bool overlay_cache_enabled();
bool overlay_end_cached(unsigned slot,const float* bounds);
Texture* capture_completed_texture();
void report_group_routes(unsigned total,unsigned flattened);
bool builtin_passthrough_enabled();
bool local_base_enabled();
void rect(float x,float y,float w,float h,uint32_t rgba);
bool readback(unsigned w,unsigned h,uint8_t* out);
Texture* white();
void menu_text(float x,float y,float size,const char* utf8,uint32_t color=0xffffffff);
void menu_prepare(const char* utf8,float size); void menu_release();
void log(const char* format,...);
}
