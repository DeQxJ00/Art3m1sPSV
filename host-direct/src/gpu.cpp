#include "gpu.hpp"
#include "shaders.hpp"
#include "builtin_shader.hpp"
#include "readback.hpp"
#include "texture_pixels.hpp"
#include "texture_opacity.hpp"
#include "visible_clip.hpp"
#include "full_cover.hpp"
#if defined(DIRECT_VISIBLE_CLIP_CANDIDATE) || defined(DIRECT_DRAW_AUDIT)
#include <psp2/io/stat.h>
#endif
#ifdef DIRECT_DRAW_AUDIT
#include <psp2/io/fcntl.h>
#endif
#include <psp2/kernel/clib.h>
#include <psp2/io/stat.h>
#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/processmgr.h>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>

namespace direct {
namespace {
struct Memory { int uid=-1; void* p=nullptr; int usse=0; };
std::vector<Memory> allocations;
std::vector<Texture*> retired;
SceGxmContext* ctx=nullptr; SceGxmShaderPatcher* patcher=nullptr;
SceGxmRenderTarget* target=nullptr;
SceGxmVertexProgram* vp=nullptr; SceGxmFragmentProgram* fp[4][3]{};
SceGxmShaderPatcherId vid{},fid[4]{};
const SceGxmProgramParameter *effectParam[4]{},*clipParam[4]{};
struct Buffer { uint8_t* pixels=nullptr; SceGxmColorSurface surface{}; SceGxmSyncObject* sync=nullptr; } buffers[3];
unsigned front=2, back=0, vertexUsed=0; constexpr unsigned vertexCapacity=262144;
Vertex* vertices=nullptr; uint16_t* indices=nullptr;
Texture* solid=nullptr; bool active=false, completed=false;
void* contextHost=nullptr;
constexpr unsigned batchCapacity=16384; // Four vertices fit in 16-bit indices.
struct Batch {
    Texture* texture=nullptr;Texture* rule=nullptr;
    unsigned first=0,count=0,variant=0,blend=0;
    float clip[4]{},progress=0,vague=0;
} batch;
SceGxmFragmentProgram* boundProgram=nullptr;
Texture *boundImage=nullptr,*boundRule=nullptr;
FrameStats frameStats{};
uint64_t sceneStarted=0,reportAt=0,submitTotal=0,queueTotal=0,finishTotal=0;
uint64_t quadTotal=0,drawTotal=0,uniformTotal=0,plainTotal=0;unsigned reportFrames=0;
uint64_t zeroTotal=0,outsideTotal=0,emptyTotal=0,trimTotal=0;double areaBeforeTotal=0,areaAfterTotal=0;
uint64_t opaqueTotal=0;double opaqueAreaTotal=0;
#ifdef DIRECT_VISIBLE_CLIP_CANDIDATE
bool visibleClipEnabled=true;
uint64_t clipPollAt=0,clipSeen=0,clipSaved=0,clipRemaining=0,ruleSeen=0;
unsigned clipDetailCount=0;
#endif
#ifdef DIRECT_FULL_COVER_CANDIDATE
bool fullCoverEnabled=true;uint64_t coverDropped=0;
#endif
bool check(int r,const char* operation) { if(r<0) log("GXM %s failed %08x",operation,unsigned(r)); return r>=0; }
#ifdef DIRECT_DRAW_AUDIT
bool auditFrame=false;uint64_t auditPollAt=0;unsigned auditDraw=0;
#endif
Memory allocate(size_t n,int usse=0) {
    Memory m; m.usse=usse;
    const size_t aligned=(n+0x3ffff)&~size_t(0x3ffff);
    m.uid=sceKernelAllocMemBlock("art3-direct",SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,aligned,nullptr);
    if(m.uid<0) m.uid=sceKernelAllocMemBlock("art3-direct-main",SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,aligned,nullptr);
    if(m.uid<0) return m;
    sceKernelGetMemBlockBase(m.uid,&m.p);
    if(!usse && !check(sceGxmMapMemory(m.p,aligned,SCE_GXM_MEMORY_ATTRIB_RW),"MapMemory")) {
        sceKernelFreeMemBlock(m.uid);return {};
    }
    return m;
}
void release(Memory m) {
    if(m.uid<0)return;
    if(m.usse==1)sceGxmUnmapVertexUsseMemory(m.p);
    else if(m.usse==2)sceGxmUnmapFragmentUsseMemory(m.p);
    else sceGxmUnmapMemory(m.p);
    sceKernelFreeMemBlock(m.uid);
}
void* memory(size_t n,int usse=0,unsigned* offset=nullptr) {
    auto m=allocate(n,usse);if(!m.p)return nullptr;
    if(usse) {
        int r=usse==1?sceGxmMapVertexUsseMemory(m.p,(n+0x3ffff)&~size_t(0x3ffff),offset):
            sceGxmMapFragmentUsseMemory(m.p,(n+0x3ffff)&~size_t(0x3ffff),offset);
        if(r<0){sceKernelFreeMemBlock(m.uid);return nullptr;}
    }
    allocations.push_back(m);return m.p;
}
void display(const void* data) {
    SceDisplayFrameBuf frame{};frame.size=sizeof(frame);frame.base=*static_cast<void* const*>(data);
    frame.pitch=1024;frame.pixelformat=SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;frame.width=960;frame.height=544;
    sceDisplaySetFrameBuf(&frame,SCE_DISPLAY_SETBUF_NEXTFRAME);
    sceDisplayWaitVblankStart();
}
void* host_alloc(void*,unsigned n){return std::malloc(n);}
void host_free(void*,void* p){std::free(p);}
void collect() { for(auto* t:retired){release({t->uid,t->pixels,0});delete t;}retired.clear(); }
#ifdef DIRECT_DEFERRED_FINISH_PROBE
bool deferredFinish=false, gpuPending=false;
WaitStats waitStats{};
void finish_pending(WaitSite site) {
    if(!ctx||active||!gpuPending)return;
    const auto started=sceKernelGetProcessTimeWide();
    sceGxmFinish(ctx);gpuPending=false;collect();
    ++waitStats.calls[unsigned(site)];
    waitStats.microseconds[unsigned(site)]+=sceKernelGetProcessTimeWide()-started;
}
#endif
void flush_batch(){
    if(!batch.count)return;
    auto* program=fp[batch.variant][batch.blend];
    if(boundProgram!=program){sceGxmSetFragmentProgram(ctx,program);boundProgram=program;}
    if(boundImage!=batch.texture){sceGxmSetFragmentTexture(ctx,0,&batch.texture->descriptor);boundImage=batch.texture;}
    if(batch.rule&&boundRule!=batch.rule){sceGxmSetFragmentTexture(ctx,1,&batch.rule->descriptor);boundRule=batch.rule;}
    if(batch.variant){
        void* uniform=nullptr;
        if(!check(sceGxmReserveFragmentDefaultUniformBuffer(ctx,&uniform),"Uniform")){batch.count=0;return;}
        ++frameStats.uniforms;
        if(batch.variant&2){float effect[]={1,batch.progress,batch.vague,0};sceGxmSetUniformDataF(uniform,effectParam[batch.variant],0,4,effect);}
        if(batch.variant&1)sceGxmSetUniformDataF(uniform,clipParam[batch.variant],0,4,batch.clip);
    }
    sceGxmSetVertexStream(ctx,0,vertices+batch.first);
    check(sceGxmDraw(ctx,SCE_GXM_PRIMITIVE_TRIANGLES,SCE_GXM_INDEX_FORMAT_U16,indices,batch.count*6),"Draw");
    ++frameStats.draws;batch.count=0;
}
SceGxmFragmentProgram* builtinPrograms[6][11]{};
const SceGxmProgramParameter* builtinParams[6][12]{};
uint16_t* triangleIndices=nullptr;
bool builtinsReady=false,builtinsFailed=false;
bool genericBuiltinForced=false;
bool neutralSingleAllowed=false;
uint64_t builtinPollAt=0,builtinFamilyCounts[6]{},builtinSwitchUs=0,builtinGroups=0;
uint64_t coreGroupTotal=0,coreGroupFlattened=0;
bool init_builtins(){
    if(builtinsReady)return true;
    if(builtinsFailed)return false;
    builtinsFailed=true;
    const unsigned char* sources[]={builtin_f,builtin_copy_f,builtin_composite_f,builtin_color_f,builtin_single_f,builtin_single_neutral_f};
    for(unsigned family=0;family<6;family++){
    auto* program=reinterpret_cast<const SceGxmProgram*>(sources[family]);
    SceGxmShaderPatcherId id{};
    if(!check(sceGxmShaderPatcherRegisterProgram(patcher,program,&id),"RegisterBuiltin"))return false;
    const char* names[]={"flags","transition","clipRect","cornerTL","cornerTR","cornerBL","cornerBR",
        "uvRect","modelClip","wipe","modelX","modelY"};
    for(unsigned i=0;i<12;i++){
        builtinParams[family][i]=sceGxmProgramFindParameterByName(program,names[i]);
        const bool required=family==0||(family!=1&&family!=5&&i<3);
        if(required&&!builtinParams[family][i]){log("missing builtin uniform %s family=%u",names[i],family);return false;}
    }
    for(unsigned i=0;i<11;i++){
        SceGxmBlendInfo b{};b.colorMask=SCE_GXM_COLOR_MASK_ALL;
        b.colorFunc=b.alphaFunc=SCE_GXM_BLEND_FUNC_ADD;
        b.colorSrc=SCE_GXM_BLEND_FACTOR_SRC_ALPHA;b.alphaSrc=SCE_GXM_BLEND_FACTOR_ONE;
        b.colorDst=b.alphaDst=SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        switch(i){
        case 1:b.alphaSrc=SCE_GXM_BLEND_FACTOR_SRC_ALPHA;b.colorDst=b.alphaDst=SCE_GXM_BLEND_FACTOR_ONE;break;
        case 2:b.colorSrc=b.alphaSrc=SCE_GXM_BLEND_FACTOR_DST_COLOR;break;
        case 3:b.colorSrc=b.alphaSrc=SCE_GXM_BLEND_FACTOR_ONE;b.colorDst=b.alphaDst=SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;break;
        case 4:b.colorFunc=SCE_GXM_BLEND_FUNC_REVERSE_SUBTRACT;[[fallthrough]];
        case 7:b.colorDst=b.alphaDst=SCE_GXM_BLEND_FACTOR_ONE;b.alphaSrc=SCE_GXM_BLEND_FACTOR_ZERO;break;
        case 5:b.colorSrc=SCE_GXM_BLEND_FACTOR_ONE;break;
        case 6:b.colorSrc=b.alphaSrc=b.colorDst=b.alphaDst=SCE_GXM_BLEND_FACTOR_ONE;break;
        case 8:b.colorSrc=SCE_GXM_BLEND_FACTOR_DST_COLOR;b.alphaSrc=SCE_GXM_BLEND_FACTOR_ZERO;b.alphaDst=SCE_GXM_BLEND_FACTOR_ONE;break;
        case 9:b.colorSrc=SCE_GXM_BLEND_FACTOR_ONE_MINUS_DST_COLOR;b.colorDst=b.alphaDst=SCE_GXM_BLEND_FACTOR_ONE;b.alphaSrc=SCE_GXM_BLEND_FACTOR_ZERO;break;
        case 10:b.colorFunc=b.alphaFunc=SCE_GXM_BLEND_FUNC_NONE;b.colorSrc=b.alphaSrc=SCE_GXM_BLEND_FACTOR_ONE;b.colorDst=b.alphaDst=SCE_GXM_BLEND_FACTOR_ZERO;break;
        }
        if(!check(sceGxmShaderPatcherCreateFragmentProgram(patcher,id,SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
            SCE_GXM_MULTISAMPLE_NONE,&b,reinterpret_cast<const SceGxmProgram*>(sprite_v),&builtinPrograms[family][i]),"BuiltinProgram"))return false;
    }
    }
    triangleIndices=static_cast<uint16_t*>(memory(65535*sizeof(uint16_t)));
    if(!triangleIndices)return false;
    for(unsigned i=0;i<65535;i++)triangleIndices[i]=i;
    builtinsReady=true;log("[direct-builtin] additional program ready; original sprite programs unchanged");return true;
}
struct Offscreen {
    Texture* image=nullptr;
    SceGxmColorSurface surface{};
    SceGxmRenderTarget* target=nullptr;
    SceGxmSyncObject* sync=nullptr;
};
struct Group {Offscreen color,mask;bool masking=false;};
Group groups[8];unsigned groupDepth=0;
Offscreen retainedGroups[4];bool retainedValid[4]{};
float retainedBounds[4][4]{};
unsigned retainedHits=0,retainedBuilds=0;
bool retainedTesting=false,retainedAllowed=true;
bool localBaseAllowed=false;
bool overlayAllowed=false,overlayDisabled=false;
bool opacityProofAllowed=true;
Offscreen* current_offscreen(){if(!groupDepth)return nullptr;auto& g=groups[groupDepth-1];return g.masking?&g.mask:&g.color;}
bool create_offscreen(Offscreen& o){
    if(o.image)return true;
    auto m=allocate(960*544*4);if(!m.p)return false;
    Texture* t=new Texture;t->w=t->stride=960;t->h=544;t->uid=m.uid;t->pixels=static_cast<uint8_t*>(m.p);
    if(!check(sceGxmTextureInitLinear(&t->descriptor,t->pixels,SCE_GXM_TEXTURE_FORMAT_A8B8G8R8,960,544,0),"OffscreenTexture")){release(m);delete t;return false;}
    sceGxmTextureSetMinFilter(&t->descriptor,SCE_GXM_TEXTURE_FILTER_LINEAR);sceGxmTextureSetMagFilter(&t->descriptor,SCE_GXM_TEXTURE_FILTER_LINEAR);
    sceGxmTextureSetUAddrMode(&t->descriptor,SCE_GXM_TEXTURE_ADDR_CLAMP);sceGxmTextureSetVAddrMode(&t->descriptor,SCE_GXM_TEXTURE_ADDR_CLAMP);
    SceGxmRenderTargetParams p{};p.width=960;p.height=544;p.scenesPerFrame=1;p.driverMemBlock=-1;p.multisampleMode=SCE_GXM_MULTISAMPLE_NONE;
    if(!check(sceGxmCreateRenderTarget(&p,&o.target),"OffscreenTarget")){release(m);delete t;return false;}
    if(!check(sceGxmColorSurfaceInit(&o.surface,SCE_GXM_COLOR_FORMAT_A8B8G8R8,SCE_GXM_COLOR_SURFACE_LINEAR,
        SCE_GXM_COLOR_SURFACE_SCALE_NONE,SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,960,544,960,t->pixels),"OffscreenSurface")||
       !check(sceGxmSyncObjectCreate(&o.sync),"OffscreenSync")){
        sceGxmDestroyRenderTarget(o.target);o.target=nullptr;release(m);delete t;return false;
    }
    o.image=t;log("[direct-builtin] allocated reusable offscreen depth=%u",groupDepth);return true;
}
void finish_scene_for_target_change(){
    const auto started=sceKernelGetProcessTimeWide();
    flush_batch();check(sceGxmEndScene(ctx,nullptr,nullptr),"EndEffectScene");active=false;
    // Conservative producer/consumer fence. Never recycle a target or vertices
    // while the GPU may still reference them. Ordinary frames never enter here.
    sceGxmFinish(ctx);
    builtinSwitchUs+=sceKernelGetProcessTimeWide()-started;
#ifdef DIRECT_DEFERRED_FINISH_PROBE
    gpuPending=false;
#endif
}
bool resume_target(Offscreen* o){
    active=check(sceGxmBeginScene(ctx,0,o?o->target:target,nullptr,nullptr,
        o?o->sync:buffers[back].sync,o?&o->surface:&buffers[back].surface,nullptr),"BeginEffectScene");
    boundProgram=nullptr;boundImage=boundRule=nullptr;
    if(!active)return false;
    sceGxmSetViewport(ctx,480,480,272,-272,.5f,.5f);sceGxmSetCullMode(ctx,SCE_GXM_CULL_NONE);
    sceGxmSetFrontDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);sceGxmSetBackDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);sceGxmSetBackDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);
    sceGxmSetVertexProgram(ctx,vp);return true;
}
void clear_offscreen(){
    Vertex v[]={{0,0,0,0,0,0,0,0},{960,0,1,0,0,0,0,0},{0,544,0,1,0,0,0,0},{960,544,1,1,0,0,0,0}};
    draw_builtin(solid,v,4,false,10,nullptr,nullptr,{});
}
}
bool init() {
    SceGxmInitializeParams p{};p.displayQueueMaxPendingCount=2;p.displayQueueCallback=display;
    p.displayQueueCallbackDataSize=sizeof(void*);p.parameterBufferSize=SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;
    if(!check(sceGxmInitialize(&p),"Initialize"))return false;
    SceGxmContextParams c{};contextHost=std::calloc(1,SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE);
    c.hostMem=contextHost;c.hostMemSize=SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE;
    c.vdmRingBufferMemSize=SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE;c.vdmRingBufferMem=memory(c.vdmRingBufferMemSize);
    c.vertexRingBufferMemSize=SCE_GXM_DEFAULT_VERTEX_RING_BUFFER_SIZE;c.vertexRingBufferMem=memory(c.vertexRingBufferMemSize);
    c.fragmentRingBufferMemSize=SCE_GXM_DEFAULT_FRAGMENT_RING_BUFFER_SIZE;c.fragmentRingBufferMem=memory(c.fragmentRingBufferMemSize);
    c.fragmentUsseRingBufferMemSize=SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE;
    c.fragmentUsseRingBufferMem=memory(c.fragmentUsseRingBufferMemSize,2,&c.fragmentUsseRingBufferOffset);
    if(!c.hostMem||!c.vdmRingBufferMem||!c.vertexRingBufferMem||!c.fragmentRingBufferMem||!c.fragmentUsseRingBufferMem)return false;
    if(!check(sceGxmCreateContext(&c,&ctx),"CreateContext"))return false;
    SceGxmRenderTargetParams r{};r.width=960;r.height=544;r.scenesPerFrame=1;
    r.multisampleMode=SCE_GXM_MULTISAMPLE_NONE;r.driverMemBlock=-1;
    if(!check(sceGxmCreateRenderTarget(&r,&target),"CreateRenderTarget"))return false;
    for(auto& b:buffers) {
        b.pixels=static_cast<uint8_t*>(memory(1024*544*4));if(!b.pixels)return false;
        std::memset(b.pixels,0,1024*544*4);
        if(!check(sceGxmColorSurfaceInit(&b.surface,SCE_GXM_COLOR_FORMAT_A8B8G8R8,SCE_GXM_COLOR_SURFACE_LINEAR,
            SCE_GXM_COLOR_SURFACE_SCALE_NONE,SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,960,544,1024,b.pixels),"ColorSurface"))return false;
        if(!check(sceGxmSyncObjectCreate(&b.sync),"SyncObject"))return false;
    }
    SceGxmShaderPatcherParams sp{};sp.hostAllocCallback=host_alloc;sp.hostFreeCallback=host_free;
    sp.bufferMemSize=256*1024;sp.bufferMem=memory(sp.bufferMemSize);
    sp.vertexUsseMemSize=256*1024;sp.vertexUsseMem=memory(sp.vertexUsseMemSize,1,&sp.vertexUsseOffset);
    sp.fragmentUsseMemSize=256*1024;sp.fragmentUsseMem=memory(sp.fragmentUsseMemSize,2,&sp.fragmentUsseOffset);
    if(!sp.bufferMem||!sp.vertexUsseMem||!sp.fragmentUsseMem)return false;
    if(!check(sceGxmShaderPatcherCreate(&sp,&patcher),"ShaderPatcher"))return false;
    auto* v=reinterpret_cast<const SceGxmProgram*>(sprite_v);
    if(!check(sceGxmShaderPatcherRegisterProgram(patcher,v,&vid),"RegisterVertex"))return false;
    SceGxmVertexAttribute attrs[3]{};const char* names[]={"position","texcoord","tint"};
    for(unsigned i=0;i<3;i++){
        auto* param=sceGxmProgramFindParameterByName(v,names[i]);if(!param)return false;
        attrs[i].streamIndex=0;attrs[i].offset=i*8;attrs[i].format=SCE_GXM_ATTRIBUTE_FORMAT_F32;
        attrs[i].componentCount=i==2?4:2;attrs[i].regIndex=sceGxmProgramParameterGetResourceIndex(param);
    }
    SceGxmVertexStream stream{};stream.stride=sizeof(Vertex);stream.indexSource=SCE_GXM_INDEX_SOURCE_INDEX_16BIT;
    if(!check(sceGxmShaderPatcherCreateVertexProgram(patcher,vid,attrs,3,&stream,1,&vp),"VertexProgram"))return false;
    const unsigned char* fragments[]={image_f,clip_f,rule_f,ruleclip_f};
    for(unsigned variant=0;variant<4;variant++){
      auto* f=reinterpret_cast<const SceGxmProgram*>(fragments[variant]);
      if(!check(sceGxmShaderPatcherRegisterProgram(patcher,f,&fid[variant]),"RegisterFragment"))return false;
      effectParam[variant]=sceGxmProgramFindParameterByName(f,"effect");clipParam[variant]=sceGxmProgramFindParameterByName(f,"clipRect");
      if(((variant&2)&&!effectParam[variant])||((variant&1)&&!clipParam[variant]))return false;
      for(unsigned i=0;i<3;i++){
        if(i==2&&variant!=0)continue; // Only the unchanged image program is eligible.
        SceGxmBlendInfo blend{};blend.colorMask=SCE_GXM_COLOR_MASK_ALL;
        blend.colorFunc=blend.alphaFunc=SCE_GXM_BLEND_FUNC_ADD;
        blend.colorSrc=blend.alphaSrc=SCE_GXM_BLEND_FACTOR_ONE;
        blend.colorDst=blend.alphaDst=i?SCE_GXM_BLEND_FACTOR_ONE:SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        if(i==2){blend.colorFunc=blend.alphaFunc=SCE_GXM_BLEND_FUNC_NONE;
            blend.colorDst=blend.alphaDst=SCE_GXM_BLEND_FACTOR_ZERO;}
        if(!check(sceGxmShaderPatcherCreateFragmentProgram(patcher,fid[variant],SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
            SCE_GXM_MULTISAMPLE_NONE,&blend,v,&fp[variant][i]),"FragmentProgram"))return false;
      }
    }
    vertices=static_cast<Vertex*>(memory(sizeof(Vertex)*vertexCapacity));indices=static_cast<uint16_t*>(memory(batchCapacity*6*sizeof(uint16_t)));
    if(!vertices||!indices)return false;
    for(unsigned i=0;i<batchCapacity;i++){const unsigned v=i*4,j=i*6;indices[j]=v;indices[j+1]=v+1;indices[j+2]=v+2;indices[j+3]=v+2;indices[j+4]=v+1;indices[j+5]=v+3;}
    const uint8_t whitePixel[]={255,255,255,255};solid=texture(1,1,whitePixel);
    log("direct GXM opt2 initialized: alpha bounds and invisible quad culling; opt1 shaders unchanged; 3 buffers, no MSAA, no depth/stencil");return solid!=nullptr;
}
#ifdef DIRECT_DEFERRED_FINISH_PROBE
void wait(){finish_pending(WaitSite::Explicit);}
bool set_deferred_finish(bool enabled){
    if(active)return false;
    if(enabled!=deferredFinish){finish_pending(WaitSite::Mode);deferredFinish=enabled;}
    return true;
}
WaitStats deferred_wait_stats(){return waitStats;}
#else
void wait(){if(ctx&&!active)sceGxmFinish(ctx);}
#endif
bool in_scene(){return active;}
FrameStats last_frame_stats(){return frameStats;}
void report_group_routes(unsigned total,unsigned flattened){coreGroupTotal+=total;coreGroupFlattened+=flattened;}
bool builtin_passthrough_enabled(){return !genericBuiltinForced;}
bool local_base_enabled(){return localBaseAllowed;}
bool overlay_cache_enabled(){return overlayAllowed&&!overlayDisabled;}
void begin(){
    const auto builtinNow=sceKernelGetProcessTimeWide();
    if(builtinNow-builtinPollAt>=1000000){
        builtinPollAt=builtinNow;SceIoStat st{};
        overlayDisabled=sceIoGetstat("ux0:data/art3m1s-gxm/overlay-cache.off",&st)==0;
        const bool forced=sceIoGetstat("ux0:data/art3m1s-gxm/builtin-generic.on",&st)==0;
        if(forced!=genericBuiltinForced){genericBuiltinForced=forced;log("[builtin-route] generic=%d at_us=%llu",int(forced),(unsigned long long)builtinNow);}
    }
#ifdef DIRECT_DRAW_AUDIT
    auditFrame=false;const auto auditNow=sceKernelGetProcessTimeWide();
    if(auditNow-auditPollAt>=1000000){
        auditPollAt=auditNow;SceIoStat stat{};
        if(sceIoGetstat("ux0:data/art3m1s-gxm/draw-audit.once",&stat)==0&&
           sceIoRemove("ux0:data/art3m1s-gxm/draw-audit.once")==0){
            auditFrame=true;auditDraw=0;log("[draw-audit-begin] at_us=%llu",(unsigned long long)auditNow);
        }
    }
#endif
#ifdef DIRECT_VISIBLE_CLIP_CANDIDATE
    const auto clipNow=sceKernelGetProcessTimeWide();
    if(clipNow-clipPollAt>=1000000){
        clipPollAt=clipNow;SceIoStat stat{};
#ifdef DIRECT_FULL_COVER_CANDIDATE
        const bool coverOn=sceIoGetstat("ux0:data/art3m1s-gxm/full-cover.off",&stat)<0;
        if(coverOn!=fullCoverEnabled){
            fullCoverEnabled=coverOn;log("[gxm-cover-state] at_us=%llu enabled=%d; discard crossing windows",(unsigned long long)clipNow,int(coverOn));
        }
#endif
        const bool enabled=sceIoGetstat("ux0:data/art3m1s-gxm/visible-clip.off",&stat)<0;
        if(enabled!=visibleClipEnabled){
            visibleClipEnabled=enabled;
            log("[gxm-clip-state] at_us=%llu enabled=%d; discard crossing windows",(unsigned long long)clipNow,int(enabled));
        }
    }
#endif
#ifdef DIRECT_DEFERRED_FINISH_PROBE
    if(active){log("deferred probe refused nested BeginScene");return;}
    // One vertex arena: drain before resetting its cursor or writing any byte.
    finish_pending(WaitSite::Begin);
#endif
    vertexUsed=0;batch.count=0;frameStats={};boundProgram=nullptr;boundImage=boundRule=nullptr;sceneStarted=sceKernelGetProcessTimeWide();
    active=check(sceGxmBeginScene(ctx,0,target,nullptr,nullptr,buffers[back].sync,&buffers[back].surface,nullptr),"BeginScene");
    if(!active)return;sceGxmSetViewport(ctx,480,480,272,-272,0.5f,0.5f);
    sceGxmSetCullMode(ctx,SCE_GXM_CULL_NONE);
    sceGxmSetFrontDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);sceGxmSetBackDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);sceGxmSetBackDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);
    sceGxmSetVertexProgram(ctx,vp);rect(0,0,960,544,0x0c121cff);}
void end(){if(!active)return;flush_batch();check(sceGxmEndScene(ctx,nullptr,nullptr),"EndScene");active=false;
#ifdef DIRECT_DEFERRED_FINISH_PROBE
    gpuPending=true;
#endif
    const uint64_t submitted=sceKernelGetProcessTimeWide();
    sceGxmPadHeartbeat(&buffers[back].surface,buffers[back].sync);void* data=buffers[back].pixels;
    if(check(sceGxmDisplayQueueAddEntry(buffers[front].sync,buffers[back].sync,&data),"Queue")) {front=back;back=(back+1)%3;completed=true;}
    const uint64_t queued=sceKernelGetProcessTimeWide();
#ifdef DIRECT_DEFERRED_FINISH_PROBE
    if(!deferredFinish)finish_pending(WaitSite::End);
#else
    sceGxmFinish(ctx);collect();
#endif
    const uint64_t finished=sceKernelGetProcessTimeWide();
    submitTotal+=submitted-sceneStarted;queueTotal+=queued-submitted;finishTotal+=finished-queued;
    quadTotal+=frameStats.quads;drawTotal+=frameStats.draws;uniformTotal+=frameStats.uniforms;plainTotal+=frameStats.plainQuads;++reportFrames;
    zeroTotal+=frameStats.zeroAlpha;outsideTotal+=frameStats.outside;emptyTotal+=frameStats.empty;trimTotal+=frameStats.trimmed;
    areaBeforeTotal+=frameStats.areaBefore;areaAfterTotal+=frameStats.areaAfter;
    opaqueTotal+=frameStats.opaqueQuads;opaqueAreaTotal+=frameStats.opaqueArea;
    if(finished-reportAt>=5000000){
        log("[builtin-perf] frames=%u generic=%d full_avg=%.3f copy_avg=%.3f composite_avg=%.3f color_avg=%.3f single_avg=%.3f groups_avg=%.3f switch_avg_us=%llu neutral_single_avg=%.3f",
            reportFrames,int(genericBuiltinForced),double(builtinFamilyCounts[0])/reportFrames,double(builtinFamilyCounts[1])/reportFrames,
            double(builtinFamilyCounts[2])/reportFrames,double(builtinFamilyCounts[3])/reportFrames,double(builtinFamilyCounts[4])/reportFrames,double(builtinGroups)/reportFrames,
            (unsigned long long)(builtinSwitchUs/reportFrames),double(builtinFamilyCounts[5])/reportFrames);
        std::memset(builtinFamilyCounts,0,sizeof(builtinFamilyCounts));builtinGroups=builtinSwitchUs=0;
        log("[builtin-groups] frames=%u requested_avg=%.3f flattened_avg=%.3f",reportFrames,double(coreGroupTotal)/reportFrames,double(coreGroupFlattened)/reportFrames);
        coreGroupTotal=coreGroupFlattened=0;
        log("[builtin-retained] frames=%u hits=%u builds=%u",reportFrames,retainedHits,retainedBuilds);
        retainedHits=retainedBuilds=0;
#ifdef DIRECT_FULL_COVER_CANDIDATE
        log("[gxm-full-cover] at_us=%llu enabled=%d frames=%u pending_quads_dropped_avg=%.3f; quads stats count before coverage",
            (unsigned long long)finished,int(fullCoverEnabled),reportFrames,double(coverDropped)/reportFrames);
        coverDropped=0;
#endif
#ifdef DIRECT_VISIBLE_CLIP_CANDIDATE
        log("[gxm-visible-clip] at_us=%llu enabled=%d frames=%u requested_avg=%.3f removed_avg=%.3f remaining_avg=%.3f rule_avg=%.3f",
            (unsigned long long)finished,int(visibleClipEnabled),reportFrames,double(clipSeen)/reportFrames,
            double(clipSaved)/reportFrames,double(clipRemaining)/reportFrames,double(ruleSeen)/reportFrames);
        clipSeen=clipSaved=clipRemaining=ruleSeen=0;clipDetailCount=0;
#endif
        log("[gxm-perf] frames=%u quads_avg=%llu draws_avg=%llu plain_quads_avg=%llu uniforms_avg=%llu submit_avg_us=%llu queue_avg_us=%llu finish_avg_us=%llu",
            reportFrames,(unsigned long long)(quadTotal/reportFrames),(unsigned long long)(drawTotal/reportFrames),(unsigned long long)(plainTotal/reportFrames),
            (unsigned long long)(uniformTotal/reportFrames),(unsigned long long)(submitTotal/reportFrames),(unsigned long long)(queueTotal/reportFrames),(unsigned long long)(finishTotal/reportFrames));
        log("[gxm-cull] frames=%u zero_avg=%llu outside_avg=%llu empty_avg=%llu trimmed_avg=%llu screen_area_before_avg=%.0f screen_area_after_avg=%.0f; overlapping bounding rectangles, not GPU fragment counts",
            reportFrames,(unsigned long long)(zeroTotal/reportFrames),(unsigned long long)(outsideTotal/reportFrames),
            (unsigned long long)(emptyTotal/reportFrames),(unsigned long long)(trimTotal/reportFrames),areaBeforeTotal/reportFrames,areaAfterTotal/reportFrames);
        zeroTotal=outsideTotal=emptyTotal=trimTotal=0;areaBeforeTotal=areaAfterTotal=0;
        log("[gxm-opaque] frames=%u quads_avg=%llu area_avg=%.0f; certified alpha=255, tint alpha=1, no clip/rule/additive",
            reportFrames,(unsigned long long)(opaqueTotal/reportFrames),opaqueAreaTotal/reportFrames);
        opaqueTotal=0;opaqueAreaTotal=0;
        reportAt=finished;submitTotal=queueTotal=finishTotal=quadTotal=drawTotal=uniformTotal=plainTotal=0;reportFrames=0;
    }
}
Texture* texture(unsigned w,unsigned h,const uint8_t* rgba){
    if(!w||!h||w>4096||h>4096||!rgba)return nullptr;
    const auto started=sceKernelGetProcessTimeWide();
    auto* t=new Texture;t->w=w;t->h=h;t->stride=(w+7)&~7u;
    auto m=allocate(size_t(t->stride)*h*4);if(!m.p){delete t;return nullptr;}
    const auto allocated=sceKernelGetProcessTimeWide();
    t->uid=m.uid;t->pixels=static_cast<uint8_t*>(m.p);
    const auto cleared=sceKernelGetProcessTimeWide();
    initialize_texture_pixels(t->pixels,t->stride,rgba,w,h,sceClibMemcpy,sceClibMemset);
    const auto copied=sceKernelGetProcessTimeWide();
    t->alphaBounds.include(rgba,w,0,0,w,h);
    const auto scanned=sceKernelGetProcessTimeWide();
    // Keep upload scans bounded. The retained final group is separately known
    // opaque from its shader output contract, without scanning source images.
    t->opaque=certify_texture_opacity(rgba,size_t(w)*h);
    if(opacityProofAllowed&&!t->opaque&&w>=960&&h>=540)t->opaqueTiles.build(rgba,w,h);
    const auto certified=sceKernelGetProcessTimeWide();
    if(certified-started>=8000||size_t(w)*h>=512*512)log("[gxm-upload] size=%ux%u alloc_us=%llu clear_us=%llu copy_us=%llu bounds_us=%llu opacity_us=%llu opaque=%d scene=%d",
        w,h,(unsigned long long)(allocated-started),(unsigned long long)(cleared-allocated),
        (unsigned long long)(copied-cleared),(unsigned long long)(scanned-copied),
        (unsigned long long)(certified-scanned),int(t->opaque),int(active));
    if(!check(sceGxmTextureInitLinear(&t->descriptor,t->pixels,SCE_GXM_TEXTURE_FORMAT_A8B8G8R8,w,h,0),"Texture")){release(m);delete t;return nullptr;}
    sceGxmTextureSetMinFilter(&t->descriptor,SCE_GXM_TEXTURE_FILTER_LINEAR);sceGxmTextureSetMagFilter(&t->descriptor,SCE_GXM_TEXTURE_FILTER_LINEAR);
    sceGxmTextureSetUAddrMode(&t->descriptor,SCE_GXM_TEXTURE_ADDR_CLAMP);sceGxmTextureSetVAddrMode(&t->descriptor,SCE_GXM_TEXTURE_ADDR_CLAMP);return t;
}
Texture* import_texture(const SceGxmTexture& d){auto* t=new Texture;t->descriptor=d;t->w=sceGxmTextureGetWidth(&d);t->h=sceGxmTextureGetHeight(&d);return t;}
bool update(Texture* t,const uint8_t* rgba,unsigned x,unsigned y,unsigned w,unsigned h){
    if(active||!t||!t->pixels||!rgba||x>=t->w||y>=t->h||w>t->w-x||h>t->h-y)return false;
#ifdef DIRECT_DEFERRED_FINISH_PROBE
    finish_pending(WaitSite::Update);
#endif
    // Default end waits, or the experimental guard above, protect these writes.
    update_texture_pixels(t->pixels,t->stride,rgba,t->w,x,y,w,h,sceClibMemcpy);
    t->alphaBounds.include(rgba,t->w,x,y,w,h);
    t->opaque=updated_opacity(t->opaque,rgba,t->w,t->h,x,y,w,h);
    t->opaqueTiles.clear(); // Dynamic writes invalidate the upload-time proof.
    return true;
}
void destroy(Texture* t){if(!t)return;if(active)retired.push_back(t);else {
#ifdef DIRECT_DEFERRED_FINISH_PROBE
    // Also guard imported descriptors: the caller can release its AVFrame
    // immediately after destroying the wrapper, recycling external GPU memory.
    finish_pending(WaitSite::Destroy);
#endif
    release({t->uid,t->pixels,0});delete t;}}
Texture* white(){return solid;}
void draw_quad(Texture* t,const Vertex* src,unsigned blend,const float* clip,Texture* rule,float progress,float vague){
    if(!active||!t)return;
    // All CPU transforms stay in cached stack memory. CDRAM is written once;
    // reading it back just to divide x/y caused unnecessary bus transactions.
    Vertex prepared[4];std::memcpy(prepared,src,sizeof(prepared));bool trimmed=false;
    frameStats.areaBefore+=screen_area(src);
    switch(trim_quad(prepared,t->alphaBounds,t->w,t->h,trimmed)){
      case QuadResult::ZeroAlpha:++frameStats.zeroAlpha;return;
      case QuadResult::Outside:++frameStats.outside;return;
      case QuadResult::Empty:++frameStats.empty;return;
      default:break;
    }
    if(trimmed)++frameStats.trimmed;src=prepared;frameStats.areaAfter+=screen_area(src);
    // A clip enclosing all four corners is redundant even for affine rotations.
    // Screen bounds are already enforced by the render target rasterizer.
    bool clipped=false;
    if(clip)for(unsigned i=0;i<4;i++)if(!(src[i].x>=clip[0]&&src[i].y>=clip[1]&&src[i].x<=clip[2]&&src[i].y<=clip[3]))clipped=true;
#ifdef DIRECT_VISIBLE_CLIP_CANDIDATE
    if(clipped){
        ++clipSeen;
        const bool redundant=clip_redundant_on_target(src,clip);
        if(clipDetailCount<3){
            log("[gxm-clip-detail] tex=%ux%u clip=%.3f,%.3f,%.3f,%.3f q=%.3f,%.3f;%.3f,%.3f;%.3f,%.3f;%.3f,%.3f redundant=%d rule=%d",
                t->w,t->h,clip[0],clip[1],clip[2],clip[3],src[0].x,src[0].y,src[1].x,src[1].y,
                src[2].x,src[2].y,src[3].x,src[3].y,int(redundant),int(rule!=nullptr));
            ++clipDetailCount;
        }
        if(visibleClipEnabled&&redundant){clipped=false;++clipSaved;}
        if(clipped)++clipRemaining;
    }
    if(rule)++ruleSeen;
#endif
    unsigned variant=(rule?2:0)|(clipped?1:0);blend=blend==1?1:0;
#ifdef DIRECT_DRAW_AUDIT
    if(auditFrame&&auditDraw<256){
        log("[draw-audit] n=%u tex=%p size=%ux%u bounds=%u,%u,%u,%u known=%d opaque=%d area=%.1f blend=%u variant=%u tint=%.3f,%.3f,%.3f,%.3f xy=%.3f,%.3f;%.3f,%.3f;%.3f,%.3f;%.3f,%.3f uv=%.5f,%.5f;%.5f,%.5f;%.5f,%.5f;%.5f,%.5f",
            auditDraw++,static_cast<void*>(t),t->w,t->h,t->alphaBounds.left,t->alphaBounds.top,t->alphaBounds.right,t->alphaBounds.bottom,
            int(t->alphaBounds.known),int(t->opaque),screen_area(src),blend,variant,src[0].r,src[0].g,src[0].b,src[0].a,
            src[0].x,src[0].y,src[1].x,src[1].y,src[2].x,src[2].y,src[3].x,src[3].y,
            src[0].u,src[0].v,src[1].u,src[1].v,src[2].u,src[2].v,src[3].u,src[3].v);
    }
#endif
    const bool visibleOpaque=t->opaque||(!variant&&t->opaqueTiles.covers_visible_quad(src));
    if(may_disable_blending(visibleOpaque,src,blend,variant)){
        blend=2;++frameStats.opaqueQuads;frameStats.opaqueArea+=screen_area(src);
    }
#ifdef DIRECT_FULL_COVER_CANDIDATE
    if(fullCoverEnabled&&batch.count&&vertexUsed+4<=vertexCapacity&&covers_target_opaque(src,visibleOpaque,blend,variant)){
        // These commands have not reached sceGxmDraw. Do not rewind or overwrite
        // any vertex storage: earlier GPU submissions retain their lifetime.
        coverDropped+=batch.count;batch.count=0;
    }
#endif
    const bool compatible=batch.count&&batch.texture==t&&batch.rule==rule&&batch.variant==variant&&batch.blend==blend&&
        (!clipped||std::memcmp(batch.clip,clip,sizeof(batch.clip))==0)&&(!rule||(batch.progress==progress&&batch.vague==vague));
    if(!compatible||batch.count==batchCapacity)flush_batch();
    if(vertexUsed+4>vertexCapacity){log("direct vertex arena exhausted; frame refused");return;}
    if(!batch.count){batch.first=vertexUsed;batch.texture=t;batch.rule=rule;batch.variant=variant;batch.blend=blend;batch.progress=progress;batch.vague=vague;
        if(clipped)std::memcpy(batch.clip,clip,sizeof(batch.clip));}
    for(auto& vertex:prepared){vertex.x=vertex.x/480-1;vertex.y=1-vertex.y/272;}
    std::memcpy(vertices+vertexUsed,prepared,sizeof(prepared));vertexUsed+=4;
    ++batch.count;++frameStats.quads;if(!variant)++frameStats.plainQuads;
}
void draw_builtin(Texture* t,const Vertex* src,size_t count,bool triangles,unsigned blend,
    const float* clip,Texture* mask,const BuiltinEffects& e){
    if(!active||!t||!src||blend>10||!count||(triangles?(count%3!=0):(count!=4)))return;
    if(!init_builtins())return;
    flush_batch();
    boundProgram=nullptr;boundImage=boundRule=nullptr;
    const bool clipGiven=clip!=nullptr;
    const float fullClip[]={0,0,960,544};if(!clip)clip=fullClip;
    float transition[4];std::memcpy(transition,e.transition,sizeof(transition));
    if(e.flags[0]!=4)transition[3]=mask?1.f:0.f;
    const float* values[]={e.flags,transition,clip,e.corners,e.corners+4,e.corners+8,e.corners+12,
        e.uvRect,e.modelClip,e.wipe,e.modelX,e.modelY};
    // 0=full E-mote, 1=copy/clear, 2=FBO composite, 3=filtered sprite.
    const bool copyOnly=!clipGiven&&((blend==10&&e.flags[0]==0&&e.flags[1]==0&&e.flags[2]==0)||e.flags[0]==5);
    bool neutralSingle=neutralSingleAllowed&&!genericBuiltinForced&&e.flags[0]==4&&e.flags[1]==0&&e.flags[2]==0&&e.flags[3]==0
        &&e.transition[2]==1&&!mask&&clip[0]<=0&&clip[1]<=0&&clip[2]>=960&&clip[3]>=544;
    for(unsigned i=0;i<4;++i)neutralSingle=neutralSingle&&e.corners[i]==1;
    for(size_t i=0;i<count;++i)neutralSingle=neutralSingle&&src[i].r==1&&src[i].g==1&&src[i].b==1&&src[i].a==1;
    const unsigned family=neutralSingle?5:((genericBuiltinForced&&e.flags[0]!=4)||e.flags[3]!=0?0:(copyOnly?1:(e.flags[0]==4?4:((e.flags[0]==2||e.flags[0]==3)?2:3))));
    ++builtinFamilyCounts[family];
    sceGxmSetFragmentProgram(ctx,builtinPrograms[family][blend]);
    sceGxmSetFragmentTexture(ctx,0,&t->descriptor);
    sceGxmSetFragmentTexture(ctx,1,&(mask?mask:solid)->descriptor);
    if(family!=1&&family!=5){
        void* uniform=nullptr;
        if(!check(sceGxmReserveFragmentDefaultUniformBuffer(ctx,&uniform),"BuiltinUniform"))return;
        for(unsigned i=0;i<12;i++)if(builtinParams[family][i])sceGxmSetUniformDataF(uniform,builtinParams[family][i],0,4,values[i]);
        ++frameStats.uniforms;
    }
    // The append-only frame arena is shared with ordinary draws. Never reset
    // it at an offscreen boundary: a parent can still refer to earlier vertices.
    size_t offset=0;
    while(offset<count){
        const unsigned n=unsigned(std::min<size_t>(count-offset,65535));
        if(vertexUsed+n>vertexCapacity){log("builtin vertex arena exhausted; draw refused");break;}
        for(unsigned i=0;i<n;i++){
            Vertex v=src[offset+i];v.x=v.x/480-1;v.y=1-v.y/272;
            std::memcpy(vertices+vertexUsed+i,&v,sizeof(v));
        }
        sceGxmSetVertexStream(ctx,0,vertices+vertexUsed);
        check(sceGxmDraw(ctx,SCE_GXM_PRIMITIVE_TRIANGLES,SCE_GXM_INDEX_FORMAT_U16,
            triangles?triangleIndices:indices,triangles?n:6),"BuiltinDraw");
        vertexUsed+=n;offset+=n;++frameStats.draws;
    }
    frameStats.quads+=triangles?unsigned(count/3):1;
    // An immediate effect draw invalidates ALL cached bindings.
    boundProgram=nullptr;boundImage=boundRule=nullptr;
}
bool group_begin(){
    if(!active||groupDepth>=8||!init_builtins())return false;
    auto& g=groups[groupDepth];if(!create_offscreen(g.color))return false;
    // These group surfaces are read at the same screen pixel coordinates.
    // Linear filtering adds edge bleed from UV interpolation precision.
    sceGxmTextureSetMinFilter(&g.color.image->descriptor,SCE_GXM_TEXTURE_FILTER_POINT);
    sceGxmTextureSetMagFilter(&g.color.image->descriptor,SCE_GXM_TEXTURE_FILTER_POINT);
    auto* parent=current_offscreen();finish_scene_for_target_change();
    g.masking=false;
    if(!resume_target(&g.color)){resume_target(parent);return false;}
    ++groupDepth;++builtinGroups;clear_offscreen();return true;
}
bool group_mask_begin(){
    if(!active||!groupDepth)return false;
    auto& g=groups[groupDepth-1];if(g.masking||!create_offscreen(g.mask))return false;
    sceGxmTextureSetMinFilter(&g.mask.image->descriptor,SCE_GXM_TEXTURE_FILTER_POINT);
    sceGxmTextureSetMagFilter(&g.mask.image->descriptor,SCE_GXM_TEXTURE_FILTER_POINT);
    finish_scene_for_target_change();
    if(!resume_target(&g.mask)){resume_target(&g.color);return false;}
    g.masking=true;clear_offscreen();return true;
}
void group_end(const EffectDraw& d,Texture* mask,float sx,float sy){
    if(!active||!groupDepth)return;
    auto& g=groups[groupDepth-1];finish_scene_for_target_change();--groupDepth;
    if(!resume_target(current_offscreen()))return;
    float clip[]={d.clip[0]*sx,d.clip[1]*sy,(d.clip[0]+d.clip[2])*sx,(d.clip[1]+d.clip[3])*sy};
    const float* c=d.tint;
    Vertex v[]={{0,0,0,0,c[0],c[1],c[2],c[3]},{960,0,1,0,c[0],c[1],c[2],c[3]},
        {0,544,0,1,c[0],c[1],c[2],c[3]},{960,544,1,1,c[0],c[1],c[2],c[3]}};
    draw_builtin(g.color.image,v,4,false,d.blend,d.hasClip?clip:nullptr,g.masking?g.mask.image:mask,d.effects);
}
bool draw_cached_group(unsigned slot){
    if(slot>=4)return false;auto& retainedGroup=retainedGroups[slot];
    if(!retainedAllowed||!active||groupDepth||!retainedValid[slot]||!retainedGroup.image)return false;
    const auto* b=retainedBounds[slot];
    Vertex q[]={{b[0],b[1],b[0]/960,b[1]/544,1,1,1,1},{b[2],b[1],b[2]/960,b[1]/544,1,1,1,1},
        {b[0],b[3],b[0]/960,b[3]/544,1,1,1,1},{b[2],b[3],b[2]/960,b[3]/544,1,1,1,1}};
    if(retainedGroup.image->opaque)draw_quad(retainedGroup.image,q);
    else{BuiltinEffects e;e.flags[0]=5;draw_builtin(retainedGroup.image,q,4,false,5,nullptr,nullptr,e);}
    ++retainedHits;return true;
}
bool overlay_end_cached(unsigned slot,const float* bounds){
    if(!active||groupDepth!=1||slot>=4)return false;
    finish_scene_for_target_change();--groupDepth;
    // Transfer the completed premultiplied target; no second filtering pass.
    std::swap(groups[0].color,retainedGroups[slot]);
    auto* t=retainedGroups[slot].image;t->opaque=false;
    sceGxmTextureSetMinFilter(&t->descriptor,SCE_GXM_TEXTURE_FILTER_POINT);
    sceGxmTextureSetMagFilter(&t->descriptor,SCE_GXM_TEXTURE_FILTER_POINT);
    auto* b=retainedBounds[slot];
    b[0]=std::clamp(std::floor(bounds[0])-1,0.f,960.f);b[1]=std::clamp(std::floor(bounds[1])-1,0.f,544.f);
    b[2]=std::clamp(std::ceil(bounds[0]+bounds[2])+1,0.f,960.f);b[3]=std::clamp(std::ceil(bounds[1]+bounds[3])+1,0.f,544.f);
    retainedValid[slot]=true;
    if(!resume_target(nullptr))return false;
    ++retainedBuilds;const bool ok=draw_cached_group(slot);if(ok)--retainedHits;
    return ok;
}
bool group_end_cached(const EffectDraw& d,float sx,float sy,unsigned slot,Texture* mask){
    // Core requests normal-blend root groups. Preserve clip and texture mask.
    // Bake the final group effect once, then use the original plain sprite path.
    if(!active||groupDepth!=1)return false;
    if(!retainedAllowed||slot>=4){group_end(d,mask,sx,sy);return false;}
    auto& retainedGroup=retainedGroups[slot];retainedValid[slot]=false;
    if(!create_offscreen(retainedGroup)){group_end(d,mask,sx,sy);return false;}
    sceGxmTextureSetMinFilter(&retainedGroup.image->descriptor,SCE_GXM_TEXTURE_FILTER_POINT);
    sceGxmTextureSetMagFilter(&retainedGroup.image->descriptor,SCE_GXM_TEXTURE_FILTER_POINT);
    auto& g=groups[0];finish_scene_for_target_change();--groupDepth;
    if(retainedTesting){const auto* p=g.color.image->pixels+(290*960+450)*4;
        const auto* b=g.color.image->pixels+(493*960+50)*4;
        log("[retained-source] top=%u,%u,%u,%u bottom=%u,%u,%u,%u",p[0],p[1],p[2],p[3],b[0],b[1],b[2],b[3]);}
    if(!resume_target(&retainedGroup)){
        if(resume_target(nullptr)){
            ++groupDepth;group_end(d,mask,sx,sy);
        }
        return false;
    }
    const float* c=d.tint;
    Vertex q[]={{0,0,0,0,c[0],c[1],c[2],c[3]},{960,0,1,0,c[0],c[1],c[2],c[3]},
        {0,544,0,1,c[0],c[1],c[2],c[3]},{960,544,1,1,c[0],c[1],c[2],c[3]}};
    const auto before=frameStats.draws;
    // Store the complete premultiplied RGBA result, including transparent pixels.
    float clip[]={d.clip[0]*sx,d.clip[1]*sy,(d.clip[0]+d.clip[2])*sx,(d.clip[1]+d.clip[3])*sy};
    draw_builtin(g.color.image,q,4,false,10,d.hasClip?clip:nullptr,mask,d.effects);
    const bool written=frameStats.draws>before;
    finish_scene_for_target_change();
    if(retainedTesting){const auto* p=retainedGroup.image->pixels+(290*960+450)*4;
        const auto* b=retainedGroup.image->pixels+(493*960+50)*4;
        log("[retained-baked] top=%u,%u,%u,%u bottom=%u,%u,%u,%u",p[0],p[1],p[2],p[3],b[0],b[1],b[2],b[3]);}
    if(!resume_target(nullptr))return false;
    const bool fullClip=!d.hasClip||(clip[0]<=0&&clip[1]<=0&&clip[2]>=960&&clip[3]>=544);
    auto* bounds=retainedBounds[slot];
    // The baked composite is zero-alpha outside its clip. Keep a texel guard
    // so bilinear edge coverage is preserved while skipping the empty screen.
    bounds[0]=fullClip?0:std::clamp(std::floor(clip[0])-1,0.f,960.f);
    bounds[1]=fullClip?0:std::clamp(std::floor(clip[1])-1,0.f,544.f);
    bounds[2]=fullClip?960:std::clamp(std::ceil(clip[2])+1,0.f,960.f);
    bounds[3]=fullClip?544:std::clamp(std::ceil(clip[3])+1,0.f,544.f);
    retainedValid[slot]=written;retainedGroup.image->opaque=written&&!mask&&fullClip&&d.effects.transition[2]==1&&d.tint[3]==1;
    if(written){++retainedBuilds;draw_cached_group(slot);--retainedHits;}
    return written;
}
bool retained_self_test(){
    // Validate the actual ARM alpha-check path, including every vector lane,
    // short tails, row strides and RGB values that must not affect opacity.
    std::vector<uint8_t> proofPixels(71*67*4);
    for(size_t i=0;i<proofPixels.size();++i)proofPixels[i]=(i%4==3)?255:uint8_t(i*37);
    bool proofOK=true;
    for(unsigned w: {1u,3u,4u,7u,63u,64u,65u})for(unsigned h: {1u,3u,64u}){
        auto* source=proofPixels.data()+4;
        proofOK=proofOK&&opaque_tile_pixels(source,71,w,h);
        for(unsigned lane=0;lane<8;++lane){
            unsigned x=std::min(w-1,lane),y=(lane&1)?h-1:0;
            auto* alpha=source+(y*71+x)*4+3;*alpha=254;
            proofOK=proofOK&&!opaque_tile_pixels(source,71,w,h);*alpha=255;
        }
    }
    log("[opaque-tiles-self-test] vector lanes, tails and strides ok=%d",int(proofOK));
    if(!proofOK){opacityProofAllowed=false;retainedAllowed=false;return false;}
    // Exercise the same initialized display state as a game reached through
    // the launcher. Log intermediate pixels only in this optional startup probe.
    retainedTesting=true;
    for(unsigned i=0;i<2;++i){begin();rect(0,0,960,544,0x000000ff);end();wait();}
    const uint8_t rgba[]={200,100,50,128};auto* t=texture(1,1,rgba);
    if(!t)return false;
    const uint8_t maskRGBA[]={255,255,255,128};auto* testMask=texture(1,1,maskRGBA);
    if(!testMask){destroy(t);return false;}
    std::vector<uint8_t> pixels(960*544*4);bool ok=true,passed=true;
    for(unsigned pass=0;pass<4;++pass){
        begin();rect(0,0,960,544,0x204060ff);
        ok=group_begin();
        if(ok){
            Vertex q[]={{400,240,0,0,1,1,1,1},{500,240,1,0,1,1,1,1},
                {400,340,0,1,1,1,1,1},{500,340,1,1,1,1,1,1}};
            draw_quad(t,q);
            EffectDraw d{};d.tint[0]=d.tint[1]=d.tint[2]=d.tint[3]=1;
            d.effects.flags[0]=3;d.effects.flags[1]=1;d.effects.flags[2]=pass==1;
            d.effects.transition[2]=pass==2?0:1;d.blend=5;
            if(pass==3){d.hasClip=1;d.clip[0]=440;d.clip[1]=280;d.clip[2]=20;d.clip[3]=20;}
            ok=group_end_cached(d,1,1,pass,pass==3?testMask:nullptr);
        }
        end();
        for(unsigned repeat=0;repeat<4;++repeat){
            if(repeat){begin();rect(0,0,960,544,0x204060ff);ok=draw_cached_group(pass);end();}
            wait();ok=ok&&readback(960,544,pixels.data());
            const auto* inside=pixels.data()+(290*960+450)*4;
            const auto* outside=pixels.data()+(400*960+400)*4;
            const auto* bottom=pixels.data()+(493*960+50)*4;
            log("[retained-display] bottom=%u,%u,%u,%u",bottom[0],bottom[1],bottom[2],bottom[3]);
            const int transparentExpected[]={78,94,110},backgroundRGB[]={32,64,96};
            for(unsigned c=0;c<3;++c){
                const int expected=pass>=2?transparentExpected[c]:(pass==1?131:124);
                const int background=pass>=2?backgroundRGB[c]:(pass==1?255:0);
                ok=ok&&std::abs(int(inside[c])-expected)<=2&&std::abs(int(outside[c])-background)<=2;
            }
            ok=ok&&inside[3]==255&&outside[3]==255;
            if(pass==3){const auto* clipped=pixels.data()+(270*960+430)*4;
                for(unsigned c=0;c<3;++c)ok=ok&&clipped[c]==backgroundRGB[c];}
            log("[retained-self-test] pass=%u repeat=%u pixel=%u,%u,%u,%u outside=%u,%u,%u,%u ok=%d",
                pass,repeat,inside[0],inside[1],inside[2],inside[3],outside[0],outside[1],outside[2],outside[3],int(ok));
            passed=passed&&ok;
        }
    }
    if(passed){
        begin();ok=draw_cached_group(0);end();wait();ok=ok&&readback(960,544,pixels.data());
        const auto* p=pixels.data()+(290*960+450)*4;
        for(unsigned c=0;c<3;++c)ok=ok&&std::abs(int(p[c])-124)<=2;
        log("[retained-self-test] earlier slot survives later builds ok=%d",int(ok));
        passed=passed&&ok;
    }
    // Compare the local opaque-base correction against the original offscreen
    // result on hardware, including fractional edges and transparent texels.
    bool localOK=true;neutralSingleAllowed=true;
    std::vector<uint8_t> reference(pixels.size());
    for(unsigned alpha:{0u,128u,254u,255u})for(float edge:{448.f,448.25f}){
        uint8_t source[]={200,100,50,uint8_t(alpha)};auto* test=texture(1,1,source);
        if(!test){localOK=false;continue;}
        Vertex q[]={{0,0,0,0,1,1,1,1},{960,0,1,0,1,1,1,1},
            {0,544,0,1,1,1,1,1},{960,544,1,1,1,1,1,1}};
        const float clip[]={edge,288.25f,edge+2.0f,290.265f};
        begin();rect(0,0,960,544,0x204060ff);
        bool refOK=group_begin();
        if(refOK){
            rect(clip[0],clip[1],clip[2]-clip[0],clip[3]-clip[1],0x000000ff);draw_quad(test,q);
            EffectDraw d{};d.tint[0]=d.tint[1]=d.tint[2]=d.tint[3]=1;d.effects.flags[0]=3;
            d.effects.transition[2]=1;d.blend=5;group_end(d,nullptr,1,1);
        }
        end();wait();refOK=refOK&&readback(960,544,reference.data());
        begin();rect(0,0,960,544,0x204060ff);
        BuiltinEffects e;e.flags[0]=4;e.transition[2]=1;
        e.corners[0]=e.corners[1]=e.corners[2]=e.corners[3]=1;
        draw_builtin(test,q,4,false,5,nullptr,nullptr,e);
        rect(clip[0],clip[1],clip[2]-clip[0],clip[3]-clip[1],0x000000ff);
        Vertex correction[4];for(unsigned i=0;i<4;++i){
            float x=clip[(i%2)?2:0],y=clip[(i/2)?3:1];correction[i]={x,y,x/960,y/544,1,1,1,1};
        }
        draw_quad(test,correction,0,clip);end();wait();bool same=refOK&&readback(960,544,pixels.data());
        unsigned maxDelta=0,worstX=0,worstY=0,worstC=0;
        for(unsigned y=280;y<304;++y)for(unsigned x=440;x<608;++x)for(unsigned c=0;c<4;++c){
            const size_t i=(y*960+x)*4+c;
            const auto delta=unsigned(std::abs(int(pixels[i])-int(reference[i])));
            if(delta>maxDelta){maxDelta=delta;worstX=x;worstY=y;worstC=c;}
        }
        same=same&&maxDelta<=1;localOK=localOK&&same;
        log("[local-base-self-test] alpha=%u edge=%.2f max_delta=%u ok=%d",alpha,edge,maxDelta,int(same));
        const size_t worst=(worstY*960+worstX)*4+worstC;
        if(!same)log("[local-base-difference] x=%u y=%u channel=%u reference=%u candidate=%u",
            worstX,worstY,worstC,reference[worst],pixels[worst]);
        destroy(test);
    }
    bool overlayOK=true;
    for(uint32_t bg: {0x17395bffu,0xe0b070ffu,0xffffffffu}){
        auto overlayDraw=[&](){
            rect(50.25f,380.25f,860,140,0xe6e6e6bbu);
            for(int i=0;i<12;++i){float x=100.f+i*43;
                rect(x+1,410,28,35,0x10101080);rect(x,409,25,32,0xffe080e0);}
        };
        begin();rect(0,0,960,544,bg);overlayDraw();end();wait();
        bool good=readback(960,544,reference.data());
        begin();rect(0,0,960,544,bg);const bool opened=group_begin();
        if(opened){overlayDraw();float bounds[]={50,380,861,141};good=overlay_end_cached(3,bounds)&&good;}
        else good=false;
        end();wait();good=readback(960,544,pixels.data())&&good;
        // Compare the overlay and surrounding untouched pixels; top-left device
        // performance plugins can change digits between the reference and candidate frames.
        unsigned delta=0;size_t worst=0;for(size_t i=size_t(360)*960*4;i<size_t(540)*960*4;++i){unsigned v=unsigned(std::abs(int(pixels[i])-int(reference[i])));if(v>delta){delta=v;worst=i;}}
        if(delta>1)log("[overlay-difference] x=%u y=%u c=%u ref=%u got=%u",unsigned(worst/4%960),unsigned(worst/4/960),unsigned(worst%4),reference[worst],pixels[worst]);
        good=good&&delta<=1;overlayOK=overlayOK&&good;
        log("[overlay-self-test] background=%08x max_delta=%u ok=%d",bg,delta,int(good));
    }
    overlayAllowed=overlayOK;
    localBaseAllowed=localOK; // This candidate stays disabled until hardware proof passes.
    neutralSingleAllowed=localOK;
    wait();destroy(t);destroy(testMask);for(auto& valid:retainedValid)valid=false;retainedHits=retainedBuilds=0;retainedTesting=false;
    retainedAllowed=passed;return passed;
}
Texture* capture_completed_texture(){
    if(!active||!completed||!init_builtins())return nullptr;
    Offscreen copy;if(!create_offscreen(copy))return nullptr;
    auto* parent=current_offscreen();finish_scene_for_target_change();
    const bool opened=resume_target(&copy);
    if(opened){
        Texture source;source.w=source.stride=1024;source.h=544;
        sceGxmTextureInitLinear(&source.descriptor,buffers[front].pixels,SCE_GXM_TEXTURE_FORMAT_A8B8G8R8,1024,544,0);
        sceGxmTextureSetMinFilter(&source.descriptor,SCE_GXM_TEXTURE_FILTER_POINT);sceGxmTextureSetMagFilter(&source.descriptor,SCE_GXM_TEXTURE_FILTER_POINT);
        const float u=960.f/1024;
        Vertex q[]={{0,0,0,0,1,1,1,1},{960,0,u,0,1,1,1,1},{0,544,0,1,1,1,1,1},{960,544,u,1,1,1,1,1}};
        draw_builtin(&source,q,4,false,10,nullptr,nullptr,{});finish_scene_for_target_change();
    }
    const bool restored=resume_target(parent);
    sceGxmDestroyRenderTarget(copy.target);sceGxmSyncObjectDestroy(copy.sync);
    if(!opened||!restored){destroy(copy.image);return nullptr;}
    return copy.image; // An owned copy, never an alias of a recycled display buffer.
}
void rect(float x,float y,float w,float h,uint32_t c){
    const float r=(c>>24)/255.0f,g=((c>>16)&255)/255.0f,b=((c>>8)&255)/255.0f,a=(c&255)/255.0f;
    Vertex v[]={{x,y,0,0,r,g,b,a},{x+w,y,1,0,r,g,b,a},{x,y+h,0,1,r,g,b,a},{x+w,y+h,1,1,r,g,b,a}};draw_quad(solid,v);
}
bool readback(unsigned w,unsigned h,uint8_t* out){if(active||!completed||!out||!w||!h)return false;
#ifdef DIRECT_DEFERRED_FINISH_PROBE
    finish_pending(WaitSite::Readback);
#endif
    copy_completed_frame(buffers[front].pixels,w,h,out);return true;
}
void prepare_process_exit(){
    // This context lives for the entire process. The caller exits immediately
    // with sceKernelExitProcess; the OS then reclaims its context, rings and
    // display buffers together. Keep the current display buffer valid until
    // that point. This is NOT an in-process renderer shutdown/reinit API.
    // Vita3K's Vulkan DestroyContext joins its GPU wait thread while that thread
    // still waits for the global shutdown flag, so explicit context destruction
    // before process exit also deadlocks on the tested emulator build.
    wait();sceGxmDisplayQueueFinish();collect();log("GPU and display drained for process exit");
}
}
