#include "recovered_emote_bridge.hpp"
#include "gpu.hpp"
#include <cmath>
#ifdef DIRECT_RECOVERED_PACKET
#include "render_packet.h"
#include "texture_host.h"
#include "compressed_layout.hpp"
#include <cstdlib>
#include <limits>

namespace {
struct Resource { uint64_t token; direct::Texture* texture; Resource* next; };
Resource* resources=nullptr;
uint64_t nextToken=1;
direct::Texture* resolve(uint64_t token){
    for(auto* r=resources;r;r=r->next)if(r->token==token)return r->texture;
    return nullptr;
}
int upload(void*,const EmTextureSource* source,uint64_t* handle){
    if(handle)*handle=0;
    if(!source||!handle||!source->identity||!source->data||!source->width||!source->height||
       source->width>4096||source->height>4096||!source->logical_width||!source->logical_height||
       source->logical_width>source->width||source->logical_height>source->height)return -1;
    const bool rgba=source->format==0,bc3=source->format==1;
    if((!rgba&&!bc3)||source->layout>1||(rgba&&source->layout!=0))return -1;
    const size_t bytes=rgba?size_t(source->width)*source->height*4:
        direct::compressed_bytes(3,source->width,source->height,source->layout==1);
    if(!bytes||bytes!=source->bytes||(bc3&&!direct::compressed_texture_allowed(3)))return -1;
    if(!nextToken)return -1; // Never reuse tokens, including after uint64 wrap.
    if(direct::in_scene())return 0; // Upload admission is explicitly before BeginScene.
    auto* r=static_cast<Resource*>(std::malloc(sizeof(Resource)));
    if(!r)return 0;
    auto* t=rgba?direct::texture(source->width,source->height,source->data):
        direct::texture_compressed(source->width,source->height,3,false,source->data,source->bytes,source->layout==1);
    if(!t){std::free(r);return 0;}
    const uint64_t token=nextToken++;
    t->contentRevision=token;
    *r={token,t,resources};resources=r;*handle=token;return 1;
}
void release(void*,uint64_t token){
    Resource** link=&resources;
    while(*link&&(*link)->token!=token)link=&(*link)->next;
    if(!*link)return;
    auto* r=*link;*link=r->next;
    // Existing GPU destruction owns the in-scene retirement and pending fences.
    direct::destroy(r->texture);std::free(r);
}
const EmTextureHost callbacks={nullptr,upload,release};
struct Handles {
    uint64_t* handles=nullptr;direct::Texture** textures=nullptr;
    ~Handles(){std::free(handles);std::free(textures);}
};
int prepare(const EmRenderPacket* packet,Handles& out,bool forDraw){
    if(!packet||em_render_packet_version(packet)!=EM_RENDER_PACKET_VERSION)return -1;
    const uint32_t count=em_render_packet_texture_count(packet);
    if(count>SIZE_MAX/sizeof(uint64_t)||count>SIZE_MAX/sizeof(direct::Texture*))return -1;
    if(count){
        out.handles=static_cast<uint64_t*>(std::calloc(count,sizeof(uint64_t)));
        if(!out.handles)return 0;
        if(forDraw){out.textures=static_cast<direct::Texture**>(std::calloc(count,sizeof(direct::Texture*)));if(!out.textures)return 0;}
    }
    const int status=em_render_packet_prepare_host(packet,out.handles,count);
    if(status!=1)return status==0?0:-1;
    for(uint32_t i=0;i<count;++i){
        auto* texture=resolve(out.handles[i]);if(!texture)return -1;
        if(forDraw)out.textures[i]=texture;
    }
    return 1;
}
}
namespace direct {
const EmTextureHost* recovered_texture_host(){return &callbacks;}
int prepare_recovered_packet(const EmRenderPacket* packet){Handles handles;return prepare(packet,handles,false);}
int replay_recovered_packet(const EmRenderPacket* packet,const float* affine,float opacity,const float* clip){
    if(!in_scene())return -1;
    Handles handles;const int status=prepare(packet,handles,true);if(status!=1)return status;
    if(!std::isfinite(opacity)||opacity<0||opacity>1)return -1;
    if(opacity==1)return draw_recovered_packet(packet,handles.textures,em_render_packet_texture_count(packet),affine,1,clip)?1:-1;
    // Preserve overlap and all original material/mask operations before opacity.
    if(!group_begin())return -1;
    const bool ok=draw_recovered_packet(packet,handles.textures,em_render_packet_texture_count(packet),affine,1,clip);
    EffectDraw composite{};composite.tint[0]=composite.tint[1]=composite.tint[2]=1;
    composite.tint[3]=opacity;composite.effects.flags[0]=3;composite.blend=5;
    group_end(composite,nullptr,1,1);
    return ok?1:-1;
}
}
#else
namespace direct {
const EmTextureHost* recovered_texture_host(){return nullptr;}
int prepare_recovered_packet(const EmRenderPacket*){return -1;}
int replay_recovered_packet(const EmRenderPacket*,const float*,float,const float*){return -1;}
}
#endif
