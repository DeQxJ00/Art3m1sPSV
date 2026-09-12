#include "gpu.hpp"
#include "video_gxm.h"
#include "video_convert.h"
#include <psp2/io/stat.h>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstring>
namespace {
std::unordered_map<uint64_t,direct::Texture*> textures;
std::unordered_map<unsigned,direct::Texture*> videos;
unsigned nextVideo=1;float sx=1,sy=1;
uint64_t textureRevision=1;
bool requested=false;unsigned captureW=0,captureH=0;std::vector<uint8_t> capture;
direct::Texture* find(uint64_t id){auto it=textures.find(id);return it==textures.end()?nullptr:it->second;}
// Synchronous adoption of an actual completed, packed RGBA texture. The core
// validates the ordinary video layer and updates its IDs/revisions without
// retaining/scanning video pixels. The matching upload callback transfers
// ownership instead of copying these already-GPU-resident RGBA bytes again.
direct::Texture* nativeVideoPending=nullptr;
uint64_t nativeVideoId=0;
bool nativeVideoValid=false,nativeVideoConsumed=false;
std::vector<uint8_t> videoFallback,videoFallbackAlpha;
bool videoOverlap=false;
void image(direct::Texture* t,float w,float h,float u=1,float v=1){
    direct::Vertex verts[]={{0,0,0,0,1,1,1,1},{w,0,u,0,1,1,1,1},{0,h,0,v,1,1,1,1},{w,h,u,v,1,1,1,1}};
    direct::draw_quad(t,verts);
}
}
extern "C" {
int art3m1s_runtime_upload_video_layer_frame(void*,const char*,unsigned,unsigned,const uint8_t*,size_t);
int art3m1s_runtime_upload_video_layer_shared_frame(void*,const char*,unsigned,unsigned,const uint8_t*,size_t);
int host_gxm_video_yuva_available(){
    SceIoStat overlapStat{};
    videoOverlap=sceIoGetstat("ux0:data/art3m1s-gxm/video-yuva-overlap.on",&overlapStat)==0;
    direct::log("[video-yuva-overlap] enabled=%d; copied input only, waits before rendering or CPU readback",int(videoOverlap));
    SceIoStat st{};const int r=sceIoGetstat("ux0:data/art3m1s-gxm/video-yuva.off",&st);
    const bool enabled=unsigned(r)==0x80010002u&&direct::video_yuva_self_test();
    direct::log("[video-yuva-mode] enabled=%d flag_result=%08x; selected at video open",int(enabled),unsigned(r));
    return enabled;
}
int host_gxm_video_yuva_queue_open(unsigned w,unsigned h,uint8_t** slots,unsigned count){
    if(videoOverlap){direct::log("[video-yuva-queue] mapped=0 private staging required for overlap experiment");return 0;}
    SceIoStat st{};const int r=sceIoGetstat("ux0:data/art3m1s-gxm/video-yuva-queue.off",&st);
    if(unsigned(r)!=0x80010002u){direct::log("[video-yuva-queue] mapped=0 flag_result=%08x",unsigned(r));return 0;}
    return direct::video_yuva_queue_open(w,h,slots,count);
}
void host_gxm_video_yuva_queue_close(){direct::video_yuva_queue_close();}
int host_gxm_video_yuva_upload(void* runtime,const char* layer,unsigned w,unsigned h,const uint8_t* planes){
    if(!runtime||!layer||!planes||nativeVideoPending||direct::in_scene())return 0;
    auto* previous=nativeVideoValid?find(nativeVideoId):nullptr;
    auto* output=direct::video_yuva_convert(previous,w,h,planes,videoOverlap);
    if(!output){
        // Allocation/format/shader failures keep working through the exact CPU
        // reference path. Scratch belongs to main, never the decode worker.
        size_t n=size_t(w)*h;videoFallback.resize(n*4);videoFallbackAlpha.resize(n);
        for(unsigned y=0;y<h;y++)host_video_gray_row(planes+3*n+size_t(y)*w,videoFallbackAlpha.data()+size_t(y)*w,w);
        for(unsigned y=0;y<h;y++)host_video_yuv444_row(planes+size_t(y)*w,planes+n+size_t(y)*w,
            planes+2*n+size_t(y)*w,videoFallbackAlpha.data()+size_t(y)*w,videoFallback.data()+size_t(y)*w*4,w);
        return art3m1s_runtime_upload_video_layer_frame(runtime,layer,w,h,videoFallback.data(),n*4);
    }
    nativeVideoPending=output;nativeVideoConsumed=false;
    const int result=art3m1s_runtime_upload_video_layer_shared_frame(runtime,layer,w,h,output->pixels,size_t(w)*h*4);
    const bool consumed=nativeVideoConsumed;nativeVideoPending=nullptr;
    if(!consumed&&output!=previous)direct::destroy(output);
    return result>0&&consumed;
}
void host_gxm_video_yuva_close(){
    nativeVideoValid=false;nativeVideoId=0;
    direct::video_yuva_release();
    direct::video_yuva_queue_close();
    std::vector<uint8_t>().swap(videoFallback);std::vector<uint8_t>().swap(videoFallbackAlpha);
}
size_t art3m1s_runtime_reclaim_video_gpu_cache(void*,size_t);
size_t host_video_reclaim_gpu_cache(void* runtime,size_t requested){
    if(!runtime||direct::in_scene())return 0;
    direct::wait(); // All submitted GPU readers must finish before cache release.
    return art3m1s_runtime_reclaim_video_gpu_cache(runtime,requested);
}
uintptr_t art3m1s_gxm_surface_prepare(uint32_t w,uint32_t h,uint8_t** pixels,size_t* capacity){
    if(!pixels||!capacity)return 0;*pixels=nullptr;*capacity=0;if(!direct::shared_surface_allowed())return 0;
    auto* t=direct::surface_prepare(w,h);if(!t)return 0;
    *pixels=t->pixels;*capacity=size_t(t->stride)*h*4;return reinterpret_cast<uintptr_t>(t);
}
void art3m1s_gxm_surface_abort(uintptr_t handle){direct::surface_abort(reinterpret_cast<direct::Texture*>(handle));}
int art3m1s_gxm_surface_publish(uintptr_t handle,uint64_t id,const uint8_t* proof,size_t count){
    auto* t=reinterpret_cast<direct::Texture*>(handle);
    if(!direct::surface_seal(t,proof,count))return 0; // caller still owns staging on failure
    t->contentRevision=++textureRevision;
    auto* old=find(id);textures[id]=t;direct::destroy(old);return 1;
}
const uint8_t* art3m1s_gxm_surface_view(uint64_t id,size_t* stride){
    auto* t=find(id);if(!direct::shared_surface_allowed()||!t||!t->pixels||!stride)return nullptr;
    if(nativeVideoValid&&id==nativeVideoId)direct::wait();
    *stride=t->stride;return t->pixels; // borrowed for this call; video writes drained above
}
int art3m1s_gxm_prepare_opacity(uint32_t w,uint32_t h,const uint8_t* rgba,size_t length,uint8_t* cells,size_t count){
    if(uint64_t(w)*h*4!=length)return 0;
    return direct::prepare_opacity(w,h,rgba,cells,count);
}
int art3m1s_gxm_upload_texture_proof(uint64_t id,uint32_t w,uint32_t h,const uint8_t* rgba,size_t length,const uint8_t* cells,size_t count){
    ++textureRevision;
    if(!rgba||uint64_t(w)*h*4!=length)return 0;
    auto* t=direct::texture(w,h,rgba,cells,count);if(!t)return 0;
    t->contentRevision=textureRevision;
    auto* old=find(id);textures[id]=t;direct::destroy(old);return 1;
}
int art3m1s_gxm_upload_texture(uint64_t id,uint32_t w,uint32_t h,const uint8_t* rgba,size_t length){
    ++textureRevision;
    if(length!=size_t(w)*h*4)return 0;
    auto* t=direct::texture(w,h,rgba);if(!t)return 0;
    t->contentRevision=textureRevision;
    auto* old=find(id);textures[id]=t;direct::destroy(old);return 1;
}
int art3m1s_gxm_update_texture_region(uint64_t id,uint32_t w,uint32_t h,const uint8_t* rgba,size_t length,
    uint32_t x,uint32_t y,uint32_t rw,uint32_t rh){
    ++textureRevision;
    auto* t=find(id);if(t)t->contentRevision=textureRevision;
    return t&&t->w==w&&t->h==h&&length==size_t(w)*h*4&&direct::update(t,rgba,x,y,rw,rh);
}
int art3m1s_gxm_upload_video_texture(uint64_t id,uint32_t w,uint32_t h,const uint8_t* rgba,size_t length){
    ++textureRevision;
    if(nativeVideoPending&&nativeVideoPending->pixels==rgba&&nativeVideoPending->w==w&&
       nativeVideoPending->h==h&&nativeVideoPending->stride==w&&length==size_t(w)*h*4){
        auto* old=find(id);auto* t=nativeVideoPending;t->contentRevision=textureRevision;
        textures[id]=t;if(old!=t)direct::destroy(old);
        nativeVideoId=id;nativeVideoValid=nativeVideoConsumed=true;return 1;
    }
    auto* t=find(id);if(t)t->contentRevision=textureRevision;
    if(t&&t->w==w&&t->h==h&&length==size_t(w)*h*4&&direct::update(t,rgba,0,0,w,h))return 1;
    return art3m1s_gxm_upload_texture(id,w,h,rgba,length);
}
void art3m1s_gxm_delete_texture(uint64_t id){++textureRevision;auto it=textures.find(id);if(it!=textures.end()){direct::destroy(it->second);textures.erase(it);}}
void art3m1s_gxm_frame_begin(uint32_t w,uint32_t h){sx=960.0f/std::max(w,1u);sy=544.0f/std::max(h,1u);direct::rect(0,0,960,544,0x000000ff);}
void art3m1s_gxm_frame_end(){}
void art3m1s_gxm_draw_texture(uint64_t id,uint32_t,uint32_t,
    float a,float b,float c,float d,float tx,float ty,float w,float h,float ux,float uy,float uw,float uh,
    float alpha,uint32_t blend,float r,float g,float blue,int gray,int negative,uint64_t rule,float progress,float vague,
    int hasClip,float cx,float cy,float cw,float ch){
    if(w<=0||h<=0||uw==0||uh==0)return;
    float xs[]={0,w,0,w},ys[]={0,0,h,h};direct::Vertex v[4];
    for(unsigned i=0;i<4;i++)v[i]={(a*xs[i]+c*ys[i]+tx)*sx,(b*xs[i]+d*ys[i]+ty)*sy,
        ux+(i%2?uw:0),uy+(i/2?uh:0),r,g,blue,std::clamp(alpha,0.0f,1.0f)};
    float clip[]={cx*sx,cy*sy,(cx+cw)*sx,(cy+ch)*sy};
    auto* mask=find(rule);
    if(rule&&!mask)for(auto& vertex:v)vertex.a*=std::clamp(1.0f-progress,0.0f,1.0f);
    if(gray||negative||blend>1){
        direct::BuiltinEffects e;e.flags[0]=mask?1:0;e.flags[1]=gray!=0;e.flags[2]=negative!=0;
        e.transition[0]=progress;e.transition[1]=vague;
        direct::draw_builtin(find(id),v,4,false,blend,hasClip?clip:nullptr,mask,e);
    }else direct::draw_quad(find(id),v,blend,hasClip?clip:nullptr,mask,progress,vague);
}
void art3m1s_gxm_draw_effect(const direct::EffectDraw* draw){
    if(!draw)return;const auto& d=*draw;
    if(d.quad[0]<=0||d.quad[1]<=0)return;
    const auto* m=d.transform;const auto* c=d.tint;
    float clip[]={d.clip[0]*sx,d.clip[1]*sy,(d.clip[0]+d.clip[2])*sx,(d.clip[1]+d.clip[3])*sy};
    auto vertex=[&](float x,float y,float u,float v){return direct::Vertex{
        (m[0]*x+m[2]*y+m[4])*sx,(m[1]*x+m[3]*y+m[5])*sy,
        d.uv[0]+u*d.uv[2],d.uv[1]+v*d.uv[3],c[0],c[1],c[2],c[3]};};
    auto e=d.effects;
    static uint32_t loggedEffects=0;
    const unsigned effectKey=(unsigned(e.flags[0])&3)|(e.flags[1]!=0?4:0)|(e.flags[2]!=0?8:0)|(e.flags[3]!=0?16:0);
    if(effectKey&&!(loggedEffects&(1u<<effectKey))){
        loggedEffects|=1u<<effectKey;direct::log("[direct-effect] kind=%.0f gray=%.0f negative=%.0f emote=%.0f blend=%u mask=%llu",
            e.flags[0],e.flags[1],e.flags[2],e.flags[3],d.blend,(unsigned long long)d.mask);
    }
    const float det=m[0]*m[3]-m[1]*m[2];
    if(e.flags[3]!=0){
        if(!std::isfinite(det)||std::abs(det)<1e-12f)return;
        e.modelX[0]=m[3]/(det*sx*d.quad[0]);e.modelX[1]=-m[2]/(det*sy*d.quad[0]);
        e.modelX[2]=(m[2]*m[5]-m[3]*m[4])/(det*d.quad[0]);
        e.modelY[0]=-m[1]/(det*sx*d.quad[1]);e.modelY[1]=m[0]/(det*sy*d.quad[1]);
        e.modelY[2]=(m[1]*m[4]-m[0]*m[5])/(det*d.quad[1]);
    }
    auto* mask=find(d.mask);auto* image=find(d.texture);
    if(d.mesh&&d.meshCount){
        std::vector<direct::Vertex> mesh;mesh.reserve(d.meshCount);
        for(size_t i=0;i<d.meshCount;i++)mesh.push_back(vertex(d.mesh[i][0],d.mesh[i][1],d.mesh[i][2],d.mesh[i][3]));
        direct::draw_builtin(image,mesh.data(),mesh.size(),true,d.blend,d.hasClip?clip:nullptr,mask,e);
    }else{
        direct::Vertex v[]={vertex(0,0,0,0),vertex(d.quad[0],0,1,0),vertex(0,d.quad[1],0,1),vertex(d.quad[0],d.quad[1],1,1)};
        // Preserve the verified ordinary sprite fast path, including trimming,
        // batch merging and opaque draws. Effects never force it to an FBO.
        if(e.flags[0]==0&&e.flags[1]==0&&e.flags[2]==0&&e.flags[3]==0&&d.blend==0)
            direct::draw_quad(image,v,0,d.hasClip?clip:nullptr);
        else if(e.flags[0]==1&&e.flags[1]==0&&e.flags[2]==0&&e.flags[3]==0&&d.blend==0&&mask)
            direct::draw_quad(image,v,0,d.hasClip?clip:nullptr,mask,e.transition[0],e.transition[1]);
        else direct::draw_builtin(image,v,4,false,d.blend,d.hasClip?clip:nullptr,mask,e);
    }
}
int art3m1s_gxm_group_begin(){return direct::group_begin();}
uint64_t art3m1s_gxm_texture_revision(){return textureRevision;}
uint64_t art3m1s_gxm_texture_content_revision(uint64_t id){auto* t=find(id);return t?t->contentRevision:0;}
int art3m1s_gxm_overlay_cache_enabled(){return direct::overlay_cache_enabled();}
int art3m1s_gxm_overlay_end_cached(uint32_t slot,const float* b){float bounds[]={b[0]*sx,b[1]*sy,b[2]*sx,b[3]*sy};return direct::overlay_end_cached(slot,bounds);}
int art3m1s_gxm_draw_cached_group(uint32_t slot){return direct::draw_cached_group(slot);}
int art3m1s_gxm_group_end_cached(const direct::EffectDraw* draw,uint32_t slot){return draw&&direct::group_end_cached(*draw,sx,sy,slot,find(draw->mask));}
int art3m1s_gxm_texture_is_opaque(uint64_t id){auto* t=find(id);return t&&t->opaque;}
int art3m1s_gxm_texture_is_empty(uint64_t id){
    auto* t=find(id);return t&&t->alphaBounds.known&&t->alphaBounds.right==0&&t->alphaBounds.bottom==0;
}
int art3m1s_gxm_texture_region_is_opaque(uint64_t id,float u0,float v0,float u1,float v1){
    auto* t=find(id);return t&&(t->opaque||t->opaqueTiles.covers(u0,v0,u1,v1));
}
void art3m1s_gxm_report_groups(uint32_t total,uint32_t flattened){direct::report_group_routes(total,flattened);}
int art3m1s_gxm_group_passthrough_enabled(){return direct::builtin_passthrough_enabled();}
int art3m1s_gxm_local_base_enabled(){return direct::local_base_enabled();}
int art3m1s_gxm_group_mask_begin(){return direct::group_mask_begin();}
void art3m1s_gxm_group_end(const direct::EffectDraw* draw){if(draw){
    static unsigned uncachedSamples=0;static float lastShape[6]={};
    const float shape[]={draw->effects.flags[0],float(draw->hasClip),draw->clip[0],draw->clip[1],draw->clip[2],draw->clip[3]};
    if(uncachedSamples<32&&(!uncachedSamples||std::memcmp(lastShape,shape,sizeof(shape)))){
        ++uncachedSamples;std::memcpy(lastShape,shape,sizeof(shape));
        direct::log("[uncached-group] kind=%.0f alpha=%.3f opaque=%.0f mask=%llu hasClip=%u clip=%.1f,%.1f,%.1f,%.1f gray=%.0f negative=%.0f",
        draw->effects.flags[0],draw->tint[3],draw->effects.transition[2],(unsigned long long)draw->mask,
        draw->hasClip,draw->clip[0],draw->clip[1],draw->clip[2],draw->clip[3],draw->effects.flags[1],draw->effects.flags[2]);}
    static uint32_t loggedGroups=0;
    const unsigned key=(unsigned(draw->effects.flags[0])&3)|(draw->effects.flags[1]!=0?4:0)|(draw->effects.flags[2]!=0?8:0);
    if(!(loggedGroups&(1u<<key))){loggedGroups|=1u<<key;direct::log("[direct-group] kind=%.0f gray=%.0f negative=%.0f opacity=%.3f opaque=%.0f rgb=%.3f,%.3f,%.3f mask=%llu clip=%u",
        draw->effects.flags[0],draw->effects.flags[1],draw->effects.flags[2],draw->tint[3],draw->effects.transition[2],
        draw->tint[0],draw->tint[1],draw->tint[2],(unsigned long long)draw->mask,draw->hasClip);}
    direct::group_end(*draw,find(draw->mask),sx,sy);
}}
int art3m1s_gxm_capture_previous_texture(uint64_t id,uint32_t w,uint32_t h){
    ++textureRevision;
    if(!w||!h)return 0;
    auto* copied=direct::capture_completed_texture();if(!copied)return 0;
    copied->contentRevision=textureRevision;
    auto* old=find(id);textures[id]=copied;direct::destroy(old);return 1;
}
int art3m1s_gxm_read_completed_frame(uint32_t w,uint32_t h,uint8_t* out,size_t length){return length==size_t(w)*h*4&&direct::readback(w,h,out);}
int art3m1s_gxm_capture_previous(uint32_t w,uint32_t h,uint8_t* out,size_t length){
    if(!out||!w||!h||length!=size_t(w)*h*4)return 0;
    if(w==captureW&&h==captureH&&capture.size()==length){std::memcpy(out,capture.data(),length);capture.clear();return 1;}
    captureW=w;captureH=h;requested=true;return 0;
}
void art3m1s_gxm_finish_host_frame(){if(requested){capture.resize(size_t(captureW)*captureH*4);
    if(!direct::readback(captureW,captureH,capture.data()))capture.clear();requested=false;}}
void art3m1s_gxm_cancel_capture(){requested=false;capture.clear();}
void art3m1s_gxm_reset_readback(){art3m1s_gxm_cancel_capture();captureW=captureH=0;}
void art3m1s_gxm_wait_idle(){direct::wait();}
void art3m1s_gxm_font_update_phase(int){}
void art3m1s_gxm_set_font_trace(int){}
void art3m1s_gxm_report_font_trace(){}
int art3m1s_gxm_release_menu_fonts(){direct::menu_release();return 1;}
void host_gxm_video_wait(){direct::wait();}
unsigned host_gxm_video_rgba(unsigned id,int w,int h,const uint8_t* data){
    if(w<=0||h<=0)return 0;auto it=videos.find(id);
    if(it!=videos.end()) {auto* t=it->second;if(t->w==unsigned(w)&&t->h==unsigned(h)&&direct::update(t,data,0,0,w,h))return id;}
    auto* t=direct::texture(w,h,data);if(!t)return 0;
    if(!id)id=nextVideo++;if(it!=videos.end())direct::destroy(it->second);videos[id]=t;return id;
}
unsigned host_gxm_video_import(SceGxmTexture* t){if(!t)return 0;unsigned id=nextVideo++;videos[id]=direct::import_texture(*t);return id;}
void host_gxm_video_delete(unsigned id){auto it=videos.find(id);if(it!=videos.end()){direct::destroy(it->second);videos.erase(it);}}
void host_gxm_video_draw(unsigned id,float u,float v){auto it=videos.find(id);if(it!=videos.end())image(it->second,960,544,u,v);}
}
