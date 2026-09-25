// Exercise production gpu.cpp with delayed reads, not a duplicate fence model.
// VitaSDK headers provide ABI declarations; only GPU/OS calls are simulated.
// init() is excluded by linker section GC: the fixture supplies test resources.
#define DIRECT_DEFERRED_FINISH_PROBE 1
#include "../../host-direct/src/gpu.cpp"
#include <cassert>
#include <map>
#include <cstdio>

namespace mock {
struct Allocation { void* pointer; size_t bytes; };
struct Image { const uint8_t* pixels; unsigned width,height,stride; };
struct Read { const uint8_t* pointer; std::vector<uint8_t> expected; };
std::map<int,Allocation> memory;
std::map<unsigned,Image> images;
std::vector<Read> reads;
std::vector<std::pair<uint8_t*,uint8_t>> targets;
const direct::Vertex* stream=nullptr;
Image bound{};
unsigned nextId=1, verified=0, frame=0, finishes=0;
bool scene=false, failQueue=false, failBegin=false, displayDrained=false;
uint8_t lastCompleted=0;
void retain(const void* pointer,size_t bytes){
    auto* p=static_cast<const uint8_t*>(pointer);
    reads.push_back({p,std::vector<uint8_t>(p,p+bytes)});
}
void assert_unused(const void* pointer){
    for(const auto& read:reads)assert(read.pointer!=pointer && "GPU still reads released memory");
}
unsigned descriptor_id(const SceGxmTexture* t){unsigned id;std::memcpy(&id,t,sizeof(id));return id;}
}

namespace direct { void log(const char*,...){} }
extern "C" {
void shark_clear_output(){}
void shark_end(){}
int sceIoGetstat(const char*,SceIoStat*){return -1;}
SceUInt64 sceKernelGetProcessTimeWide(){static SceUInt64 time=0;return ++time;}
SceUID sceKernelAllocMemBlock(const char*,SceKernelMemBlockType,SceSize bytes,SceKernelAllocMemBlockOpt*){
    int id=mock::nextId++;mock::memory[id]={std::calloc(1,bytes),bytes};return id;
}
int sceKernelGetMemBlockBase(SceUID uid,void** out){*out=mock::memory.at(uid).pointer;return 0;}
int sceKernelFreeMemBlock(SceUID uid){
    auto allocation=mock::memory.at(uid);mock::assert_unused(allocation.pointer);
    std::free(allocation.pointer);mock::memory.erase(uid);return 0;
}
int sceGxmMapMemory(void*,SceSize,SceGxmMemoryAttribFlags){return 0;}
int sceGxmUnmapMemory(void* p){mock::assert_unused(p);return 0;}
int sceGxmUnmapVertexUsseMemory(void*){return 0;}
int sceGxmUnmapFragmentUsseMemory(void*){return 0;}
void* sceClibMemcpy(void* dst,const void* src,SceSize n){return std::memcpy(dst,src,n);}
void* sceClibMemset(void* dst,int c,SceSize n){return std::memset(dst,c,n);}
int sceGxmTextureInitLinear(SceGxmTexture* t,const void* p,SceGxmTextureFormat,unsigned w,unsigned h,unsigned){
    unsigned id=mock::nextId++;std::memcpy(t,&id,sizeof(id));
    mock::images[id]={static_cast<const uint8_t*>(p),w,h,(w+7)&~7u};return 0;
}
unsigned sceGxmTextureGetWidth(const SceGxmTexture* t){return mock::images.at(mock::descriptor_id(t)).width;}
unsigned sceGxmTextureGetHeight(const SceGxmTexture* t){return mock::images.at(mock::descriptor_id(t)).height;}
int sceGxmTextureSetMinFilter(SceGxmTexture*,SceGxmTextureFilter){return 0;}
int sceGxmTextureSetMagFilter(SceGxmTexture*,SceGxmTextureFilter){return 0;}
int sceGxmTextureSetUAddrMode(SceGxmTexture*,SceGxmTextureAddrMode){return 0;}
int sceGxmTextureSetVAddrMode(SceGxmTexture*,SceGxmTextureAddrMode){return 0;}
int sceGxmBeginScene(SceGxmContext*,unsigned,const SceGxmRenderTarget*,const SceGxmValidRegion*,
                    SceGxmSyncObject*,SceGxmSyncObject*,const SceGxmColorSurface*,const SceGxmDepthStencilSurface*){
    assert(!mock::scene);
    if(mock::failBegin){mock::failBegin=false;return -1;}
    mock::scene=true;return 0;
}
int sceGxmEndScene(SceGxmContext*,const SceGxmNotification*,const SceGxmNotification*){
    assert(mock::scene);mock::scene=false;
    mock::targets.emplace_back(direct::buffers[direct::back].pixels,uint8_t(++mock::frame%251+1));return 0;
}
int sceGxmPadHeartbeat(const SceGxmColorSurface*,SceGxmSyncObject*){return 0;}
int sceGxmDisplayQueueAddEntry(SceGxmSyncObject*,SceGxmSyncObject*,const void*){
    if(mock::failQueue){mock::failQueue=false;return -1;}return 0;
}
int sceGxmDisplayQueueFinish(){assert(mock::reads.empty());mock::displayDrained=true;return 0;}
void sceGxmFinish(SceGxmContext*){
    assert(!mock::scene && "Finish must never wait inside an unsubmitted scene");
    for(const auto& read:mock::reads){
        assert(std::memcmp(read.pointer,read.expected.data(),read.expected.size())==0 && "Submitted bytes changed before GPU consumption");
        ++mock::verified;
    }
    mock::reads.clear();++mock::finishes;
    for(auto [pixels,value]:mock::targets){std::memset(pixels,value,1024*544*4);mock::lastCompleted=value;}
    mock::targets.clear();
}
void sceGxmSetViewport(SceGxmContext*,float,float,float,float,float,float){}
void sceGxmSetCullMode(SceGxmContext*,SceGxmCullMode){}
void sceGxmSetFrontDepthFunc(SceGxmContext*,SceGxmDepthFunc){}
void sceGxmSetBackDepthFunc(SceGxmContext*,SceGxmDepthFunc){}
void sceGxmSetFrontDepthWriteEnable(SceGxmContext*,SceGxmDepthWriteMode){}
void sceGxmSetBackDepthWriteEnable(SceGxmContext*,SceGxmDepthWriteMode){}
void sceGxmSetVertexProgram(SceGxmContext*,const SceGxmVertexProgram*){}
void sceGxmSetFragmentProgram(SceGxmContext*,const SceGxmFragmentProgram*){}
int sceGxmSetVertexStream(SceGxmContext*,unsigned,const void* p){mock::stream=static_cast<const direct::Vertex*>(p);return 0;}
int sceGxmSetFragmentTexture(SceGxmContext*,unsigned slot,const SceGxmTexture* t){
    assert(slot==0);mock::bound=mock::images.at(mock::descriptor_id(t));return 0;
}
int sceGxmReserveFragmentDefaultUniformBuffer(SceGxmContext*,void**){std::abort();}
int sceGxmSetUniformDataF(void*,const SceGxmProgramParameter*,unsigned,unsigned,const float*){std::abort();}
int sceGxmDraw(SceGxmContext*,SceGxmPrimitiveType,SceGxmIndexFormat,const void*,unsigned count){
    assert(mock::scene&&count%6==0);
    mock::retain(mock::stream,count/6*4*sizeof(direct::Vertex));
    mock::retain(mock::bound.pixels,mock::bound.stride*mock::bound.height*4);return 0;
}
}

static void draw(direct::Texture* t,unsigned variation=0){
    float x=float(variation%200);
    direct::Vertex q[]={{x,0,0,0,1,1,1,1},{x+20,0,1,0,1,1,1,1},
                        {x,20,0,1,1,1,1,1},{x+20,20,1,1,1,1,1,1}};
    direct::draw_quad(t,q);
}
static direct::Texture* texture(unsigned value){
    const uint8_t p[]={uint8_t(value),20,30,255};return direct::texture(1,1,p);
}
static void submit(direct::Texture* t,unsigned variation=0){direct::begin();draw(t,variation);direct::end();}

int main(){
    std::vector<direct::Vertex> vertices(direct::vertexCapacity);
    std::vector<uint16_t> indices(direct::batchCapacity*6);
    std::vector<uint8_t> framebuffers[3];
    direct::ctx=reinterpret_cast<SceGxmContext*>(uintptr_t(1));
    direct::vertices=vertices.data();direct::indices=indices.data();
    for(unsigned i=0;i<3;++i){framebuffers[i].resize(1024*544*4);direct::buffers[i].pixels=framebuffers[i].data();}
    direct::solid=texture(255);
    auto* persistent=texture(60);
    submit(persistent);assert(mock::reads.empty()); // Default remains end-wait.
    assert(direct::set_deferred_finish(true));
    direct::offscreenQueueAllowed=true;
    direct::begin();
    auto finishes=mock::finishes;
    for(unsigned pass=0;pass<12;++pass){
        draw(persistent,pass);
        const auto used=direct::vertexUsed;
        direct::finish_scene_for_target_change();
        assert(direct::gpuPending&&!mock::reads.empty());
        assert(mock::finishes==finishes); // No CPU wait between submitted passes.
        assert(direct::resume_target(nullptr));
        assert(direct::vertexUsed==used); // Never reuse the shared vertex arena.
    }
    direct::finish_scene_for_target_change(true); // Intermediate CPU readback.
    assert(!direct::gpuPending&&mock::reads.empty());
    assert(mock::finishes==finishes+1);
    assert(direct::resume_target(nullptr));draw(persistent);
    direct::finish_scene_for_target_change();mock::failBegin=true;
    assert(!direct::resume_target(nullptr));
    // A failed target resume has no final end(); pending work must survive it.
    direct::begin();assert(direct::gpuPending==false);
    assert(mock::reads.empty());direct::end();direct::wait();
    direct::offscreenQueueDisabled=true;
    direct::begin();draw(persistent);direct::finish_scene_for_target_change();
    assert(!direct::gpuPending&&mock::reads.empty());
    assert(direct::resume_target(nullptr));direct::end();direct::wait();
    direct::offscreenQueueDisabled=false;
    // Reopening an observed input only swaps ownership. Pending texture/vertex
    // readers survive the swap, and a failed Begin leaves no stale valid slot.
    auto* scratch=texture(71);auto* observed=texture(83);
    direct::groups[0].color.image=scratch;direct::retainedGroups[0].image=observed;
    direct::retainedValid[0]=true;
    direct::begin();draw(observed);finishes=mock::finishes;
    auto serial=direct::cache_slot_revision(0);
    assert(direct::group_begin_cached_input(0));
    assert(direct::groups[0].color.image==observed&&direct::retainedGroups[0].image==scratch);
    assert(direct::cache_slot_revision(0)==serial+1&&!direct::retainedValid[0]);
    assert(mock::finishes==finishes&&!mock::reads.empty());
    assert(!direct::group_begin_cached_input(0)); // Nested restore is forbidden.
    draw(persistent);direct::finish_scene_for_target_change();direct::groupDepth=0;
    assert(direct::resume_target(nullptr));draw(observed);direct::end();direct::wait();
    direct::retainedValid[0]=true;direct::begin();draw(scratch);mock::failBegin=true;
    assert(!direct::group_begin_cached_input(0));
    assert(direct::in_scene()&&direct::groupDepth==0&&!direct::retainedValid[0]);
    assert(!direct::group_begin_cached_input(0));
    draw(persistent);direct::end();direct::wait();
    direct::groups[0].color={};direct::retainedGroups[0]={};
    direct::destroy(scratch);direct::destroy(observed);
    for(unsigned i=0;i<128;++i){
        // A new scene changes the single vertex arena. GPU reads must happen first.
        submit(persistent,i);assert(!mock::reads.empty());
        submit(persistent,i+1);assert(!mock::reads.empty());
        // In-place texture writes must not change submitted samples.
        const uint8_t changed[]={uint8_t(i),40,50,255};
        assert(direct::update(persistent,changed,0,0,1,1));assert(mock::reads.empty());
        auto* transient=texture(i);
        direct::begin();draw(transient,i+2);
        assert(!direct::set_deferred_finish(false));
        assert(!direct::update(transient,changed,0,0,1,1));
        direct::wait();assert(mock::scene); // Cannot finish an open scene.
        direct::destroy(transient); // Retired even while still referenced by the batch.
        direct::end();assert(!mock::reads.empty());
        direct::begin();draw(persistent,i+3);direct::end();
        transient=texture(i+5);submit(transient,i+4);
        direct::destroy(transient);assert(mock::reads.empty());
        // External decoder ownership: release the raw buffer immediately after
        // deleting its imported wrapper, exactly like AVFrame release/recycle.
        auto* external=texture(i+6);auto* imported=direct::import_texture(external->descriptor);
        submit(imported,i+5);direct::destroy(imported);
        sceKernelFreeMemBlock(external->uid);external->uid=-1;external->pixels=nullptr;direct::destroy(external);
        submit(persistent,i+6);std::vector<uint8_t> out(320*180*4);
        assert(direct::readback(320,180,out.data()));
        assert(std::all_of(out.begin(),out.end(),[](uint8_t b){return b==mock::lastCompleted;}));
    }
    submit(persistent);assert(direct::set_deferred_finish(false));assert(mock::reads.empty());
    submit(persistent);assert(mock::reads.empty());assert(direct::set_deferred_finish(true));
    mock::failQueue=true;submit(persistent);direct::destroy(persistent);assert(mock::reads.empty());
    submit(direct::solid);mock::failBegin=true;direct::begin();assert(!direct::in_scene());assert(mock::reads.empty());
    submit(direct::solid);direct::prepare_process_exit();assert(mock::displayDrained);
    direct::destroy(direct::solid);assert(mock::memory.empty());
    auto stats=direct::deferred_wait_stats();
    for(unsigned site=0;site<unsigned(direct::WaitSite::Count);++site)assert(stats.calls[site]>0);
    std::printf("DEFERRED_FINISH delayed_reads=%u finishes=%u frames=%u passed\n",mock::verified,mock::finishes,mock::frame);
}
