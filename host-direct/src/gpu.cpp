#include "gpu.hpp"
#include "shaders.hpp"
#include "readback.hpp"
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
SceGxmVertexProgram* vp=nullptr; SceGxmFragmentProgram* fp[5][BlendCount]{};
SceGxmShaderPatcherId vid{},fid[5]{};
const SceGxmProgramParameter *effectParam[5]{},*clipParam[5]{};
const char* builtinNames[]={"flags","transition","corners","uvRect","modelClip","wipe","modelX","modelY"};
const SceGxmProgramParameter* builtinParams[8]{};
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
    Effects effects{};
} batch;
struct Offscreen {
    Texture* texture=nullptr; SceGxmRenderTarget* target=nullptr;
    SceGxmColorSurface surface{}; SceGxmSyncObject* sync=nullptr;
};
struct Group { Offscreen content{},mask{}; Offscreen* parent=nullptr; bool hasMask=false; };
std::vector<Group*> groupPool;
unsigned groupDepth=0;Offscreen* currentTarget=nullptr;
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
        if(batch.variant==4){
            const auto& e=batch.effects;
            const float* blocks[]={e.flags,e.transition,e.corners,e.uvRect,e.modelClip,e.wipe,e.modelX,e.modelY};
            for(unsigned i=0;i<8;i++)sceGxmSetUniformDataF(uniform,builtinParams[i],0,i==2?16:4,blocks[i]);
            sceGxmSetUniformDataF(uniform,clipParam[4],0,4,batch.clip);
        }else{
            if(batch.variant&2){float effect[]={1,batch.progress,batch.vague,0};sceGxmSetUniformDataF(uniform,effectParam[batch.variant],0,4,effect);}
            if(batch.variant&1)sceGxmSetUniformDataF(uniform,clipParam[batch.variant],0,4,batch.clip);
        }
    }
    sceGxmSetVertexStream(ctx,0,vertices+batch.first);
    check(sceGxmDraw(ctx,SCE_GXM_PRIMITIVE_TRIANGLES,SCE_GXM_INDEX_FORMAT_U16,indices,batch.count*6),"Draw");
    ++frameStats.draws;batch.count=0;
}
void scene_state(){
    boundProgram=nullptr;boundImage=boundRule=nullptr;
    sceGxmSetViewport(ctx,480,480,272,-272,0.5f,0.5f);
    sceGxmSetCullMode(ctx,SCE_GXM_CULL_NONE);
    sceGxmSetFrontDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);sceGxmSetBackDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);sceGxmSetBackDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);
    sceGxmSetVertexProgram(ctx,vp);
}
bool start_target(Offscreen* t){
    currentTarget=t;
    active=check(sceGxmBeginScene(ctx,0,t?t->target:target,nullptr,nullptr,
        t?t->sync:buffers[back].sync,t?&t->surface:&buffers[back].surface,nullptr),"BeginTarget");
    if(active)scene_state();return active;
}
void finish_target(){
    if(!active)return;flush_batch();check(sceGxmEndScene(ctx,nullptr,nullptr),"EndTarget");active=false;
    // Complete writes before another pass samples/reuses this tile buffer.
    sceGxmFinish(ctx);
}
bool ensure_target(Offscreen& t){
    if(t.texture)return true;
    std::vector<uint8_t> zero(960*544*4);
    auto* image=texture(960,544,zero.data());if(!image)return false;
    SceGxmRenderTargetParams p{};p.width=960;p.height=544;p.scenesPerFrame=1;
    p.multisampleMode=SCE_GXM_MULTISAMPLE_NONE;p.driverMemBlock=-1;
    if(!check(sceGxmCreateRenderTarget(&p,&t.target),"GroupTarget")){destroy(image);return false;}
    if(!check(sceGxmSyncObjectCreate(&t.sync),"GroupSync")||
       !check(sceGxmColorSurfaceInit(&t.surface,SCE_GXM_COLOR_FORMAT_A8B8G8R8,SCE_GXM_COLOR_SURFACE_LINEAR,
        SCE_GXM_COLOR_SURFACE_SCALE_NONE,SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,960,544,image->stride,image->pixels),"GroupSurface")){
        if(t.sync)sceGxmSyncObjectDestroy(t.sync);sceGxmDestroyRenderTarget(t.target);t={};destroy(image);return false;
    }
    image->alphaBounds={};t.texture=image;return true;
}
void clear_target(){
    Vertex q[]={{0,0,0,0,0,0,0,0},{960,0,1,0,0,0,0,0},{0,544,0,1,0,0,0,0},{960,544,1,1,0,0,0,0}};
    Effects e;draw_quad(solid,q,Copy,nullptr,nullptr,0,1.f/255,&e);
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
    const unsigned char* fragments[]={image_f,clip_f,rule_f,ruleclip_f,builtin_f};
    for(unsigned variant=0;variant<5;variant++){
      auto* f=reinterpret_cast<const SceGxmProgram*>(fragments[variant]);
      if(!check(sceGxmShaderPatcherRegisterProgram(patcher,f,&fid[variant]),"RegisterFragment"))return false;
      effectParam[variant]=sceGxmProgramFindParameterByName(f,"effect");clipParam[variant]=sceGxmProgramFindParameterByName(f,"clipRect");
      if(((variant&2)&&!effectParam[variant])||((variant&1)&&!clipParam[variant]))return false;
      if(variant==4){
          if(!clipParam[4])return false;
          for(unsigned i=0;i<8;i++){builtinParams[i]=sceGxmProgramFindParameterByName(f,builtinNames[i]);if(!builtinParams[i])return false;}
      }
      for(unsigned i=0;i<(variant==4?unsigned(BlendCount):2u);i++){
        SceGxmBlendInfo blend{};blend.colorMask=SCE_GXM_COLOR_MASK_ALL;
        blend.colorFunc=blend.alphaFunc=SCE_GXM_BLEND_FUNC_ADD;
        blend.colorSrc=blend.alphaSrc=SCE_GXM_BLEND_FACTOR_ONE;
        blend.colorDst=blend.alphaDst=i?SCE_GXM_BLEND_FACTOR_ONE:SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        if(variant==4){
            using F=SceGxmBlendFactor;
            const F one=SCE_GXM_BLEND_FACTOR_ONE,zero=SCE_GXM_BLEND_FACTOR_ZERO;
            const F sa=SCE_GXM_BLEND_FACTOR_SRC_ALPHA,invsa=SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend.colorSrc=one;blend.colorDst=invsa;blend.alphaSrc=one;blend.alphaDst=invsa;
            switch(i){
            case Alpha:blend.colorSrc=sa;break;
            case Add:blend.colorSrc=blend.alphaSrc=sa;blend.colorDst=blend.alphaDst=one;break;
            case Multiply:blend.colorSrc=SCE_GXM_BLEND_FACTOR_DST_COLOR;blend.alphaSrc=SCE_GXM_BLEND_FACTOR_DST_ALPHA;break;
            case Screen:blend.colorDst=SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;break;
            case ReverseSubtract:blend.colorFunc=SCE_GXM_BLEND_FUNC_REVERSE_SUBTRACT;[[fallthrough]];
            case NativeAdd:blend.colorSrc=sa;blend.colorDst=one;blend.alphaSrc=zero;blend.alphaDst=one;break;
            case PremultipliedAlpha:break;
            case PremultipliedAdd:blend.colorDst=blend.alphaDst=one;break;
            case NativeMultiply:blend.colorSrc=SCE_GXM_BLEND_FACTOR_DST_COLOR;blend.alphaSrc=zero;blend.alphaDst=one;break;
            case NativeScreen:blend.colorSrc=SCE_GXM_BLEND_FACTOR_ONE_MINUS_DST_COLOR;blend.colorDst=one;blend.alphaSrc=zero;blend.alphaDst=one;break;
            case Copy:blend.colorDst=blend.alphaDst=zero;break;
            }
        }else if(i==Add){blend.alphaSrc=SCE_GXM_BLEND_FACTOR_SRC_ALPHA;}
        if(!check(sceGxmShaderPatcherCreateFragmentProgram(patcher,fid[variant],SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
            SCE_GXM_MULTISAMPLE_NONE,&blend,v,&fp[variant][i]),"FragmentProgram"))return false;
      }
    }
    vertices=static_cast<Vertex*>(memory(sizeof(Vertex)*vertexCapacity));indices=static_cast<uint16_t*>(memory(batchCapacity*6*sizeof(uint16_t)));
    if(!vertices||!indices)return false;
    for(unsigned i=0;i<batchCapacity;i++){const unsigned v=i*4,j=i*6;indices[j]=v;indices[j+1]=v+1;indices[j+2]=v+2;indices[j+3]=v+2;indices[j+4]=v+1;indices[j+5]=v+3;}
    const uint8_t whitePixel[]={255,255,255,255};solid=texture(1,1,whitePixel);
    log("direct GXM effects initialized: ordinary fast path + core built-ins, 10 blend modes; 3 buffers, no MSAA");return solid!=nullptr;
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
    auto* t=new Texture;t->w=w;t->h=h;t->stride=(w+7)&~7u;
    auto m=allocate(size_t(t->stride)*h*4);if(!m.p){delete t;return nullptr;}
    t->uid=m.uid;t->pixels=static_cast<uint8_t*>(m.p);std::memset(t->pixels,0,size_t(t->stride)*h*4);
    for(unsigned y=0;y<h;y++)std::memcpy(t->pixels+size_t(y)*t->stride*4,rgba+size_t(y)*w*4,w*4);
    t->alphaBounds.include(rgba,w,0,0,w,h);
    if(!check(sceGxmTextureInitLinear(&t->descriptor,t->pixels,SCE_GXM_TEXTURE_FORMAT_A8B8G8R8,w,h,0),"Texture")){release(m);delete t;return nullptr;}
    sceGxmTextureSetMinFilter(&t->descriptor,SCE_GXM_TEXTURE_FILTER_LINEAR);sceGxmTextureSetMagFilter(&t->descriptor,SCE_GXM_TEXTURE_FILTER_LINEAR);
    sceGxmTextureSetUAddrMode(&t->descriptor,SCE_GXM_TEXTURE_ADDR_CLAMP);sceGxmTextureSetVAddrMode(&t->descriptor,SCE_GXM_TEXTURE_ADDR_CLAMP);return t;
}
Texture* import_texture(const SceGxmTexture& d){auto* t=new Texture;t->descriptor=d;t->w=sceGxmTextureGetWidth(&d);t->h=sceGxmTextureGetHeight(&d);return t;}
bool update(Texture* t,const uint8_t* rgba,unsigned x,unsigned y,unsigned w,unsigned h){
    if(active||!t||!t->pixels||!rgba||x>=t->w||y>=t->h||w>t->w-x||h>t->h-y)return false;
    // Each previous frame completed in end(); never write a currently submitted texture.
    for(unsigned row=0;row<h;row++)std::memcpy(t->pixels+((y+row)*size_t(t->stride)+x)*4,rgba+((y+row)*size_t(t->w)+x)*4,w*4);
    t->alphaBounds.include(rgba,t->w,x,y,w,h);
    return true;
}
void destroy(Texture* t){if(!t)return;if(active)retired.push_back(t);else {release({t->uid,t->pixels,0});delete t;}}
Texture* white(){return solid;}
void draw_quad(Texture* t,const Vertex* src,unsigned blend,const float* clip,Texture* rule,float progress,float vague,const Effects* effects){
    if(!active||!t)return;
    // All CPU transforms stay in cached stack memory. CDRAM is written once;
    // reading it back just to divide x/y caused unnecessary bus transactions.
    Vertex prepared[4];std::memcpy(prepared,src,sizeof(prepared));bool trimmed=false;
    frameStats.areaBefore+=screen_area(src);
    // Screen/copy, opaque groups and E-Mote wipes can make zero-alpha source
    // texels contribute. Alpha-only trimming is valid on the ordinary path.
    switch(effects||blend>1?QuadResult::Draw:trim_quad(prepared,t->alphaBounds,t->w,t->h,trimmed)){
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
    unsigned variant=(rule?2:0)|(clipped?1:0);
    Effects fallback;
    if(blend>=BlendCount)blend=Alpha;
    if(effects||blend>1){variant=4;if(!effects){fallback.flags[0]=rule?Rule:Sprite;fallback.transition[0]=progress;fallback.transition[1]=vague;effects=&fallback;}}
    const float fullClip[]={0,0,960,544};
    if(variant==4){if(!clip)clip=fullClip;clipped=true;if(!rule)rule=solid;}
    const bool compatible=batch.count&&batch.texture==t&&batch.rule==rule&&batch.variant==variant&&batch.blend==blend&&
        (!clipped||std::memcmp(batch.clip,clip,sizeof(batch.clip))==0)&&(!rule||(batch.progress==progress&&batch.vague==vague))&&
        (variant!=4||std::memcmp(&batch.effects,effects,sizeof(Effects))==0);
    if(!compatible||batch.count==batchCapacity)flush_batch();
    if(vertexUsed+4>vertexCapacity){log("direct vertex arena exhausted; frame refused");return;}
    if(!batch.count){batch.first=vertexUsed;batch.texture=t;batch.rule=rule;batch.variant=variant;batch.blend=blend;batch.progress=progress;batch.vague=vague;
        if(clipped)std::memcpy(batch.clip,clip,sizeof(batch.clip));if(effects)batch.effects=*effects;}
    for(auto& vertex:prepared){vertex.x=vertex.x/480-1;vertex.y=1-vertex.y/272;}
    std::memcpy(vertices+vertexUsed,prepared,sizeof(prepared));vertexUsed+=4;
    ++batch.count;++frameStats.quads;if(!variant)++frameStats.plainQuads;
}
bool group_begin(){
    if(!active)return false;auto* parent=currentTarget;finish_target();
    if(groupDepth==groupPool.size())groupPool.push_back(new Group);
    auto& g=*groupPool[groupDepth];g.parent=parent;g.hasMask=false;
    if(!ensure_target(g.content)){start_target(parent);return false;}
    if(!start_target(&g.content)){start_target(parent);return false;}
    ++groupDepth;clear_target();return true;
}
bool group_mask_begin(){
    if(!groupDepth||!active)return false;auto& g=*groupPool[groupDepth-1];finish_target();
    if(!ensure_target(g.mask)){start_target(&g.content);return false;}
    if(!start_target(&g.mask)){start_target(&g.content);return false;}
    g.hasMask=true;clear_target();return true;
}
void group_end(const Effects& effects,unsigned blend,const float* clip,Texture* mask,float r,float g,float b,float alpha){
    if(!groupDepth)return;auto& group=*groupPool[--groupDepth];finish_target();
    if(!start_target(group.parent))return;
    Vertex q[]={{0,0,0,0,r,g,b,alpha},{960,0,1,0,r,g,b,alpha},{0,544,0,1,r,g,b,alpha},{960,544,1,1,r,g,b,alpha}};
    draw_quad(group.content.texture,q,blend,clip,group.hasMask?group.mask.texture:mask,0,1.f/255,&effects);
}
void rect(float x,float y,float w,float h,uint32_t c){
    const float r=(c>>24)/255.0f,g=((c>>16)&255)/255.0f,b=((c>>8)&255)/255.0f,a=(c&255)/255.0f;
    Vertex v[]={{x,y,0,0,r,g,b,a},{x+w,y,1,0,r,g,b,a},{x,y+h,0,1,r,g,b,a},{x+w,y+h,1,1,r,g,b,a}};draw_quad(solid,v);
}
bool readback(unsigned w,unsigned h,uint8_t* out){if(active||!completed||!out||!w||!h)return false;
    copy_completed_frame(buffers[front].pixels,w,h,out);return true;
}
Texture* snapshot_completed(){
    if(active||!completed)return nullptr;
    Offscreen captured;if(!ensure_target(captured))return nullptr;
    unsigned nonzero=0;
    for(unsigned y=0;y<544;y+=32)for(unsigned x=0;x<960;x+=32){
        const auto* p=buffers[front].pixels+(size_t(y)*1024+x)*4;
        nonzero+=p[0]!=0||p[1]!=0||p[2]!=0||p[3]!=0;
    }
    Texture source;source.w=960;source.h=544;
    const bool sourceReady=check(sceGxmTextureInitLinearStrided(&source.descriptor,buffers[front].pixels,
        SCE_GXM_TEXTURE_FORMAT_A8B8G8R8,960,544,1024*4),"SnapshotSource");
    sceGxmTextureSetMinFilter(&source.descriptor,SCE_GXM_TEXTURE_FILTER_POINT);
    sceGxmTextureSetMagFilter(&source.descriptor,SCE_GXM_TEXTURE_FILTER_POINT);
    sceGxmTextureSetUAddrMode(&source.descriptor,SCE_GXM_TEXTURE_ADDR_CLAMP);
    sceGxmTextureSetVAddrMode(&source.descriptor,SCE_GXM_TEXTURE_ADDR_CLAMP);
    // end() already completed the frame. This extra pass owns the vertex arena
    // until Finish and never writes the displayed buffer or queues a new frame.
    vertexUsed=0;const FrameStats savedStats=frameStats;
    bool ok=sourceReady&&start_target(&captured);
    if(ok){
        Vertex q[]={{0,0,0,0,1,1,1,1},{960,0,1,0,1,1,1,1},{0,544,0,1,1,1,1,1},{960,544,1,1,1,1,1,1}};
        Effects effect;draw_quad(&source,q,Copy,nullptr,nullptr,0,1.f/255,&effect);finish_target();
    }
    currentTarget=nullptr;frameStats=savedStats;
    sceGxmSyncObjectDestroy(captured.sync);sceGxmDestroyRenderTarget(captured.target);
    log("[transition-snapshot] source_cpu_nonzero_samples=%u/510 gpu_pass=%u",nonzero,ok);
    if(!ok){destroy(captured.texture);return nullptr;}
    return captured.texture;
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
