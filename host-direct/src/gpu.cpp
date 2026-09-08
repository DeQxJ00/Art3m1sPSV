#include "gpu.hpp"
#include "shaders.hpp"
#include "readback.hpp"
#include "texture_pixels.hpp"
#include <psp2/kernel/clib.h>
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
SceGxmVertexProgram* vp=nullptr; SceGxmFragmentProgram* fp[4][2]{};
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
bool check(int r,const char* operation) { if(r<0) log("GXM %s failed %08x",operation,unsigned(r)); return r>=0; }
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
      for(unsigned i=0;i<2;i++){
        SceGxmBlendInfo blend{};blend.colorMask=SCE_GXM_COLOR_MASK_ALL;
        blend.colorFunc=blend.alphaFunc=SCE_GXM_BLEND_FUNC_ADD;
        blend.colorSrc=blend.alphaSrc=SCE_GXM_BLEND_FACTOR_ONE;
        blend.colorDst=blend.alphaDst=i?SCE_GXM_BLEND_FACTOR_ONE:SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
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
void wait(){if(ctx&&!active)sceGxmFinish(ctx);}
bool in_scene(){return active;}
FrameStats last_frame_stats(){return frameStats;}
void begin(){vertexUsed=0;batch.count=0;frameStats={};boundProgram=nullptr;boundImage=boundRule=nullptr;sceneStarted=sceKernelGetProcessTimeWide();
    active=check(sceGxmBeginScene(ctx,0,target,nullptr,nullptr,buffers[back].sync,&buffers[back].surface,nullptr),"BeginScene");
    if(!active)return;sceGxmSetViewport(ctx,480,480,272,-272,0.5f,0.5f);
    sceGxmSetCullMode(ctx,SCE_GXM_CULL_NONE);
    sceGxmSetFrontDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);sceGxmSetBackDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);sceGxmSetBackDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);
    sceGxmSetVertexProgram(ctx,vp);rect(0,0,960,544,0x0c121cff);}
void end(){if(!active)return;flush_batch();check(sceGxmEndScene(ctx,nullptr,nullptr),"EndScene");active=false;
    const uint64_t submitted=sceKernelGetProcessTimeWide();
    sceGxmPadHeartbeat(&buffers[back].surface,buffers[back].sync);void* data=buffers[back].pixels;
    if(check(sceGxmDisplayQueueAddEntry(buffers[front].sync,buffers[back].sync,&data),"Queue")) {front=back;back=(back+1)%3;completed=true;}
    const uint64_t queued=sceKernelGetProcessTimeWide();sceGxmFinish(ctx);collect();const uint64_t finished=sceKernelGetProcessTimeWide();
    submitTotal+=submitted-sceneStarted;queueTotal+=queued-submitted;finishTotal+=finished-queued;
    quadTotal+=frameStats.quads;drawTotal+=frameStats.draws;uniformTotal+=frameStats.uniforms;plainTotal+=frameStats.plainQuads;++reportFrames;
    zeroTotal+=frameStats.zeroAlpha;outsideTotal+=frameStats.outside;emptyTotal+=frameStats.empty;trimTotal+=frameStats.trimmed;
    areaBeforeTotal+=frameStats.areaBefore;areaAfterTotal+=frameStats.areaAfter;
    if(finished-reportAt>=5000000){
        log("[gxm-perf] frames=%u quads_avg=%llu draws_avg=%llu plain_quads_avg=%llu uniforms_avg=%llu submit_avg_us=%llu queue_avg_us=%llu finish_avg_us=%llu",
            reportFrames,(unsigned long long)(quadTotal/reportFrames),(unsigned long long)(drawTotal/reportFrames),(unsigned long long)(plainTotal/reportFrames),
            (unsigned long long)(uniformTotal/reportFrames),(unsigned long long)(submitTotal/reportFrames),(unsigned long long)(queueTotal/reportFrames),(unsigned long long)(finishTotal/reportFrames));
        log("[gxm-cull] frames=%u zero_avg=%llu outside_avg=%llu empty_avg=%llu trimmed_avg=%llu screen_area_before_avg=%.0f screen_area_after_avg=%.0f; overlapping bounding rectangles, not GPU fragment counts",
            reportFrames,(unsigned long long)(zeroTotal/reportFrames),(unsigned long long)(outsideTotal/reportFrames),
            (unsigned long long)(emptyTotal/reportFrames),(unsigned long long)(trimTotal/reportFrames),areaBeforeTotal/reportFrames,areaAfterTotal/reportFrames);
        zeroTotal=outsideTotal=emptyTotal=trimTotal=0;areaBeforeTotal=areaAfterTotal=0;
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
    if(scanned-started>=8000||size_t(w)*h>=512*512)log("[gxm-upload] size=%ux%u alloc_us=%llu clear_us=%llu copy_us=%llu bounds_us=%llu scene=%d",
        w,h,(unsigned long long)(allocated-started),(unsigned long long)(cleared-allocated),
        (unsigned long long)(copied-cleared),(unsigned long long)(scanned-copied),int(active));
    if(!check(sceGxmTextureInitLinear(&t->descriptor,t->pixels,SCE_GXM_TEXTURE_FORMAT_A8B8G8R8,w,h,0),"Texture")){release(m);delete t;return nullptr;}
    sceGxmTextureSetMinFilter(&t->descriptor,SCE_GXM_TEXTURE_FILTER_LINEAR);sceGxmTextureSetMagFilter(&t->descriptor,SCE_GXM_TEXTURE_FILTER_LINEAR);
    sceGxmTextureSetUAddrMode(&t->descriptor,SCE_GXM_TEXTURE_ADDR_CLAMP);sceGxmTextureSetVAddrMode(&t->descriptor,SCE_GXM_TEXTURE_ADDR_CLAMP);return t;
}
Texture* import_texture(const SceGxmTexture& d){auto* t=new Texture;t->descriptor=d;t->w=sceGxmTextureGetWidth(&d);t->h=sceGxmTextureGetHeight(&d);return t;}
bool update(Texture* t,const uint8_t* rgba,unsigned x,unsigned y,unsigned w,unsigned h){
    if(active||!t||!t->pixels||!rgba||x>=t->w||y>=t->h||w>t->w-x||h>t->h-y)return false;
    // Each previous frame completed in end(); never write a currently submitted texture.
    update_texture_pixels(t->pixels,t->stride,rgba,t->w,x,y,w,h,sceClibMemcpy);
    t->alphaBounds.include(rgba,t->w,x,y,w,h);
    return true;
}
void destroy(Texture* t){if(!t)return;if(active)retired.push_back(t);else {release({t->uid,t->pixels,0});delete t;}}
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
    unsigned variant=(rule?2:0)|(clipped?1:0);blend=blend==1?1:0;
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
void rect(float x,float y,float w,float h,uint32_t c){
    const float r=(c>>24)/255.0f,g=((c>>16)&255)/255.0f,b=((c>>8)&255)/255.0f,a=(c&255)/255.0f;
    Vertex v[]={{x,y,0,0,r,g,b,a},{x+w,y,1,0,r,g,b,a},{x,y+h,0,1,r,g,b,a},{x+w,y+h,1,1,r,g,b,a}};draw_quad(solid,v);
}
bool readback(unsigned w,unsigned h,uint8_t* out){if(active||!completed||!out||!w||!h)return false;
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
