#include "gpu.hpp"
#include "effects_bridge.hpp"
#include <cmath>
#include "video_gxm.h"
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstring>
namespace {
std::unordered_map<uint64_t,direct::Texture*> textures;
std::unordered_map<unsigned,direct::Texture*> videos;
unsigned nextVideo=1;float sx=1,sy=1;
bool requested=false;unsigned captureW=0,captureH=0;std::vector<uint8_t> capture;
direct::Texture* find(uint64_t id){auto it=textures.find(id);return it==textures.end()?nullptr:it->second;}
void image(direct::Texture* t,float w,float h,float u=1,float v=1){
    direct::Vertex verts[]={{0,0,0,0,1,1,1,1},{w,0,u,0,1,1,1,1},{0,h,0,v,1,1,1,1},{w,h,u,v,1,1,1,1}};
    direct::draw_quad(t,verts);
}
}
extern "C" {
int art3m1s_gxm_upload_texture(uint64_t id,uint32_t w,uint32_t h,const uint8_t* rgba,size_t length){
    if(length!=size_t(w)*h*4)return 0;
    auto* t=direct::texture(w,h,rgba);if(!t)return 0;
    auto* old=find(id);textures[id]=t;direct::destroy(old);return 1;
}
int art3m1s_gxm_update_texture_region(uint64_t id,uint32_t w,uint32_t h,const uint8_t* rgba,size_t length,
    uint32_t x,uint32_t y,uint32_t rw,uint32_t rh){
    auto* t=find(id);return t&&t->w==w&&t->h==h&&length==size_t(w)*h*4&&direct::update(t,rgba,x,y,rw,rh);
}
int art3m1s_gxm_upload_video_texture(uint64_t id,uint32_t w,uint32_t h,const uint8_t* rgba,size_t length){
    auto* t=find(id);if(t&&t->w==w&&t->h==h&&length==size_t(w)*h*4&&direct::update(t,rgba,0,0,w,h))return 1;
    return art3m1s_gxm_upload_texture(id,w,h,rgba,length);
}
void art3m1s_gxm_delete_texture(uint64_t id){auto it=textures.find(id);if(it!=textures.end()){direct::destroy(it->second);textures.erase(it);}}
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
    direct::Effects effect;effect.flags[0]=rule?direct::Rule:direct::Sprite;effect.flags[1]=gray;effect.flags[2]=negative;
    effect.transition[0]=progress;effect.transition[1]=vague;
    direct::draw_quad(find(id),v,blend,hasClip?clip:nullptr,mask,progress,vague,(gray||negative)?&effect:nullptr);
}
void art3m1s_gxm_draw_effect(const EffectDraw* draw){
    if(!draw)return;auto* texture=find(draw->texture);if(!texture)return;
    const auto& m=draw->transform;const auto& tint=draw->tint;
    direct::Effects e=draw->effects;
    const bool mesh=draw->mesh&&draw->meshCount;
    if(e.flags[3]){
        const float det=m[0]*m[3]-m[1]*m[2];if(!std::isfinite(det)||std::abs(det)<1.e-12f)return;
        const float dx=mesh?1:draw->quadSize[0],dy=mesh?1:draw->quadSize[1];if(!dx||!dy)return;
        e.modelX[0]=m[3]/(det*sx*dx);e.modelX[1]=-m[2]/(det*sy*dx);e.modelX[2]=(m[2]*m[5]-m[3]*m[4])/(det*dx);
        e.modelY[0]=-m[1]/(det*sx*dy);e.modelY[1]=m[0]/(det*sy*dy);e.modelY[2]=(m[1]*m[4]-m[0]*m[5])/(det*dy);
    }
    float clip[]={draw->clip[0]*sx,draw->clip[1]*sy,(draw->clip[0]+draw->clip[2])*sx,(draw->clip[1]+draw->clip[3])*sy};
    auto vertex=[&](float x,float y,float u,float v){return direct::Vertex{
        (m[0]*x+m[2]*y+m[4])*sx,(m[1]*x+m[3]*y+m[5])*sy,
        draw->uv[0]+u*draw->uv[2],draw->uv[1]+v*draw->uv[3],tint[0],tint[1],tint[2],tint[3]};};
    // Preserve the zero-uniform ordinary path. Native materials, group passes
    // and non-alpha blending use the full built-in shader only when requested.
    const bool complex=e.flags[1]||e.flags[2]||e.flags[3]||e.flags[0]>1||draw->blend>1||
        (e.flags[0]==direct::Rule&&!draw->mask);
    auto emit=[&](direct::Vertex* q){direct::draw_quad(texture,q,draw->blend,draw->hasClip?clip:nullptr,
        find(draw->mask),e.transition[0],e.transition[1],complex?&e:nullptr);};
    if(mesh){
        // A degenerate second triangle lets expanded meshes use the ordered
        // indexed batch arena without changing ordinary sprite vertex layout.
        for(size_t i=0;i+2<draw->meshCount;i+=3){direct::Vertex q[4];
            for(unsigned j=0;j<3;j++){const float* p=draw->mesh+(i+j)*4;q[j]=vertex(p[0],p[1],p[2],p[3]);}
            q[3]=q[2];emit(q);
        }
    }else{
        direct::Vertex q[]={vertex(0,0,0,0),vertex(draw->quadSize[0],0,1,0),vertex(0,draw->quadSize[1],0,1),vertex(draw->quadSize[0],draw->quadSize[1],1,1)};emit(q);
    }
}
int art3m1s_gxm_group_begin(){return direct::group_begin();}
int art3m1s_gxm_group_mask_begin(){return direct::group_mask_begin();}
void art3m1s_gxm_group_end(const EffectDraw* draw){
    if(!draw)return;
    float clip[]={draw->clip[0]*sx,draw->clip[1]*sy,(draw->clip[0]+draw->clip[2])*sx,(draw->clip[1]+draw->clip[3])*sy};
    direct::group_end(draw->effects,draw->blend,draw->hasClip?clip:nullptr,find(draw->mask),
        draw->tint[0],draw->tint[1],draw->tint[2],draw->tint[3]);
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
