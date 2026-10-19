// Production .inl protocol/CPU ownership test; GXM is instrumented, not emulated.
#include <psp2/gxm.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "render_packet.h"
#include "render_packet_internal.h"
#include "recovered_clip_region.hpp"
extern "C" {
#include "material.h"
}
#include "recovered_original_shaders.h"
#include "recovered_original_mask_shaders.h"
static int failAt=-1,calls=0;static bool sceneOpen=true;
static unsigned finishCalls=0;
static void finishMock(SceGxmContext*){++finishCalls;}
#define sceGxmFinish finishMock
static int fallible(){return calls++==failAt?-17:0;}
template<class... A> static int ignore(A...){return 0;}
static const SceGxmProgramParameter* parameter(const SceGxmProgram*,const char*){return reinterpret_cast<const SceGxmProgramParameter*>(1);}
static unsigned resource(const SceGxmProgramParameter*){return 1;}
static float uniforms[128];
static int reserveUniform(SceGxmContext*,void** out){int rc=fallible();if(rc>=0)*out=uniforms;return rc;}
static float vertexMatrix[16];
static int uniform(void*,const SceGxmProgramParameter*,unsigned,unsigned n,const float* values){int rc=fallible();if(rc>=0&&n==16)std::memcpy(vertexMatrix,values,sizeof vertexMatrix);return rc;}
static unsigned stencilRead,stencilWrite,stencilRef;static SceGxmVertexProgram* currentVp;
struct PlainState {uintptr_t program;unsigned depth[2],ref[2],stencil[2][6];};
static PlainState plainState{};static unsigned stateCalls;
static void fpSet(SceGxmContext*,SceGxmFragmentProgram* p){plainState.program=reinterpret_cast<uintptr_t>(p);++stateCalls;}
static void frontDepth(SceGxmContext*,SceGxmDepthFunc f){plainState.depth[0]=f;++stateCalls;}
static void backDepth(SceGxmContext*,SceGxmDepthFunc f){plainState.depth[1]=f;++stateCalls;}

static unsigned region[4];static const void* stream;
static std::vector<std::vector<unsigned char>> meshes;
static std::vector<unsigned> writes,reads;
struct PendingMesh {const unsigned char* vertices;const unsigned char* indices;size_t vb,ib;std::vector<unsigned char> expected;};
static std::vector<PendingMesh> pendingMeshes;
static int setStream(SceGxmContext*,unsigned,const void* v){stream=v;return fallible();}
static int texture(SceGxmContext*,unsigned,const SceGxmTexture*){return fallible();}
static int draw(SceGxmContext*,SceGxmPrimitiveType,SceGxmIndexFormat,const void* ptr,unsigned n){
 int rc=fallible();if(rc<0)return rc;assert(sceneOpen);auto* ix=static_cast<const uint16_t*>(ptr);unsigned vc=0;for(unsigned i=0;i<n;++i)vc=std::max(vc,unsigned(ix[i])+1);
 if(vc==4){const float identity[]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};assert(!std::memcmp(vertexMatrix,identity,sizeof identity));}
 const auto* data=static_cast<const unsigned char*>(stream);meshes.emplace_back(data,data+vc*sizeof(EmRenderVertex));meshes.back().insert(meshes.back().end(),static_cast<const unsigned char*>(ptr),static_cast<const unsigned char*>(ptr)+n*2);pendingMeshes.push_back({data,static_cast<const unsigned char*>(ptr),vc*sizeof(EmRenderVertex),n*2,meshes.back()});// CPU oracle for the existing vertex shader's column-major matrix ABI.
 // Keep pending bytes raw: the GPU owns those immutable buffers until completion.
 for(unsigned i=0;i<vc;++i){EmRenderVertex v;std::memcpy(&v,meshes.back().data()+i*sizeof(v),sizeof v);
  const float x=v.x,y=v.y,z=v.z,w=v.w;
  v.x=((vertexMatrix[0]*x+vertexMatrix[4]*y)+vertexMatrix[8]*z)+vertexMatrix[12]*w;
  v.y=((vertexMatrix[1]*x+vertexMatrix[5]*y)+vertexMatrix[9]*z)+vertexMatrix[13]*w;
  std::memcpy(meshes.back().data()+i*sizeof(v),&v,sizeof v);
 }
 writes.push_back(stencilWrite);reads.push_back(stencilRead);return 0;
}
static int endScene(SceGxmContext*,void*,void*){assert(sceneOpen);sceneOpen=false;return fallible();}
static int beginScene(SceGxmContext*,unsigned,SceGxmRenderTarget*,void*,void*,SceGxmSyncObject*,const SceGxmColorSurface*,const SceGxmDepthStencilSurface*){assert(!sceneOpen);int rc=fallible();sceneOpen=rc>=0;return rc;}
static void vpSet(SceGxmContext*,SceGxmVertexProgram* p){currentVp=p;}
static void regionSet(SceGxmContext*,SceGxmRegionClipMode,unsigned a,unsigned b,unsigned c,unsigned d){region[0]=a;region[1]=b;region[2]=c;region[3]=d;}
static void stencil(SceGxmContext*,SceGxmStencilFunc,SceGxmStencilOp,SceGxmStencilOp,SceGxmStencilOp,unsigned char read,unsigned char write){stencilRead=read;stencilWrite=write;}
static void ref(SceGxmContext*,unsigned char v){stencilRef=v;}
static void stencilSide(unsigned side,SceGxmStencilFunc f,SceGxmStencilOp a,SceGxmStencilOp b,SceGxmStencilOp c,unsigned char r,unsigned char w){unsigned v[]={unsigned(f),unsigned(a),unsigned(b),unsigned(c),r,w};std::memcpy(plainState.stencil[side],v,sizeof v);stencilRead=r;stencilWrite=w;++stateCalls;}
static void frontStencil(SceGxmContext*,SceGxmStencilFunc f,SceGxmStencilOp a,SceGxmStencilOp b,SceGxmStencilOp c,unsigned char r,unsigned char w){stencilSide(0,f,a,b,c,r,w);}
static void backStencil(SceGxmContext*,SceGxmStencilFunc f,SceGxmStencilOp a,SceGxmStencilOp b,SceGxmStencilOp c,unsigned char r,unsigned char w){stencilSide(1,f,a,b,c,r,w);}
static void frontRef(SceGxmContext*,unsigned char r){plainState.ref[0]=stencilRef=r;++stateCalls;}
static void backRef(SceGxmContext*,unsigned char r){plainState.ref[1]=stencilRef=r;++stateCalls;}
#define sceGxmSetFragmentProgram fpSet
#define sceGxmSetFrontDepthFunc frontDepth
#define sceGxmSetBackDepthFunc backDepth
#define sceGxmProgramFindParameterByName parameter
#define sceGxmProgramParameterGetResourceIndex resource
#define sceGxmReserveVertexDefaultUniformBuffer reserveUniform
#define sceGxmReserveFragmentDefaultUniformBuffer reserveUniform
#define sceGxmSetUniformDataF ::uniform
#define sceGxmSetVertexStream setStream
#define sceGxmSetFragmentTexture texture
#define sceGxmDraw draw
#define sceGxmEndScene endScene
#define sceGxmBeginScene beginScene
#define sceGxmSetVertexProgram vpSet
#define sceGxmSetRegionClip regionSet
#define sceGxmSetFrontStencilFunc frontStencil
#define sceGxmSetBackStencilFunc backStencil
#define sceGxmSetFrontStencilRef frontRef
#define sceGxmSetBackStencilRef backRef
#include "recovered_packet_mock_ignores.inc"
namespace direct {
struct Texture {SceGxmTexture descriptor{};unsigned w=960,h=544;int uid=-1;uint8_t* pixels=nullptr;int allocation=0;};
struct Memory {int uid=-1;void* p=nullptr;int usse=0;int charge=0;};
struct Bounds {bool invalid=false;void invalidate(){invalid=true;}};
struct Offscreen {Texture* image=nullptr;SceGxmColorSurface surface{};SceGxmRenderTarget* target=nullptr;SceGxmSyncObject* sync=nullptr;Bounds dirty;};
struct Group {bool masking=false;Bounds bounds;};
static SceGxmContext* ctx=nullptr;static SceGxmShaderPatcher* patcher=nullptr;static SceGxmRenderTarget* target=nullptr;static SceGxmVertexProgram* vp=reinterpret_cast<SceGxmVertexProgram*>(2);
static SceGxmFragmentProgram* boundProgram=nullptr;static Texture *boundImage=nullptr,*boundRule=nullptr,*solid=nullptr;
static bool active=true,frameGroupsComplete=true;static unsigned groupDepth=3,back=0;static Group groups[8];static Offscreen* activeOffscreen;
struct {SceGxmColorSurface surface{};SceGxmSyncObject* sync=nullptr;} buffers[3];
struct {unsigned draws=0;} frameStats;
struct {uint64_t switchUs=0;unsigned switches=0;} effectFrameTiming;
static bool check(int rc,const char*){return rc>=0;}static void log(const char*,...){}
static bool group_failed(){frameGroupsComplete=false;return false;}
static Memory allocate(size_t n,int,int){return {-1,std::malloc(n),0,0};}static Memory allocate_upload(size_t n,unsigned owner){return allocate(n,0,owner);}static void release(Memory m){std::free(m.p);}
static bool create_offscreen(Offscreen&){return false;}
static void flush_batch(){}
static uint64_t sceKernelGetProcessTimeWide(){return 0;}
static bool recoveredDepthAttached=false,recoveredFramePackets=false;
static bool offscreenQueueAllowed=false,offscreenQueueDisabled=false,gpuPending=false;
static uint64_t gpuCompletionEpoch=0;
namespace {const SceGxmDepthStencilSurface* recovered_group_depth(Offscreen*);void recovered_neutral_state();}
#include "recovered_resume_target.inc"
#define DIRECT_RECOVERED_PACKET 1
#define DIRECT_DEFERRED_FINISH_PROBE 1
#include "recovered_packet.inl"
}
static int refs;
static void retain(void*){++refs;}static void releaseLease(void*){--refs;}static void* resolve(void*p){return p;}
int main(){using namespace direct;
 Texture tex,white,parentTexture,maskTex[2];solid=&white;Offscreen parent;parent.image=&parentTexture;parent.target=reinterpret_cast<SceGxmRenderTarget*>(3);activeOffscreen=&parent;
 recovered.ready=true;recovered.arena.p=std::malloc(recoveredArenaSize);recovered.vertex=reinterpret_cast<SceGxmVertexProgram*>(4);recovered.plain=reinterpret_cast<SceGxmFragmentProgram*>(5);recovered.alpha=recovered.clear=recovered.plain;recovered.matrix=recovered.alphaRef=recovered.clearColor=reinterpret_cast<const SceGxmProgramParameter*>(1);
 for(auto& row:recovered.materials)for(auto*&p:row)p=recovered.plain;for(auto& row:recovered.softMaterials)for(auto*&p:row)p=recovered.plain;for(auto*&p:recovered.writers)p=recovered.plain;for(auto*&p:recovered.screen)p=recovered.matrix;
 for(unsigned i=0;i<2;++i){recovered.masks[i].image=&maskTex[i];recovered.masks[i].target=reinterpret_cast<SceGxmRenderTarget*>(uintptr_t(6+i));}
 EmRenderLease lease{&tex,retain,releaseLease,resolve,{1,960,544}};
 EmRenderVertex vertices[]={{-.7f,.7f,.5f,1,.2f,.3f,.4f,.8f,0,0},{.7f,.7f,.5f,1,.2f,.3f,.4f,.8f,1,0},{-.7f,-.7f,.5f,1,.2f,.3f,.4f,.8f,0,1}};uint16_t indices[]={0,1,2};
 auto* p=em_render_packet_create();auto command=[&](EmRenderOp op,int a=0,int b=0,int c=0,int d=0){assert(!em_render_packet_command(p,op,a,b,c,d));};auto mesh=[&](){assert(!em_render_packet_mesh(p,&lease,vertices,3,indices,3));};
 command(EM_RENDER_MATERIAL,0);command(EM_RENDER_HARD_RESET);command(EM_RENDER_HARD_STEP,0,1);mesh();command(EM_RENDER_HARD_STEP,1,0);mesh();command(EM_RENDER_MASK_STATE);
 command(EM_RENDER_SOFT_BEGIN);command(EM_RENDER_SOFT_REGION,16,16,900,500);command(EM_RENDER_SOFT_POSITIVE);mesh();command(EM_RENDER_SOFT_NEGATIVE);mesh();command(EM_RENDER_SOFT_END);command(EM_RENDER_MATERIAL,0);command(EM_RENDER_MASK_STATE);mesh();command(EM_RENDER_SOFT_DISABLE);assert(em_render_packet_seal(p));
 Texture* textures[]={&tex};const float identity[]={1,0,0,1,0,0};
 auto reset=[&](){recoveredDepthAttached=false;active=sceneOpen=true;activeOffscreen=&parent;groupDepth=3;frameGroupsComplete=true;calls=0;meshes.clear();pendingMeshes.clear();writes.clear();reads.clear();recovered_frame_begin();failAt=-1;};
 reset();assert(draw_recovered_packet(p,textures,1,nullptr,1,nullptr));auto baseline=meshes;int count=calls;assert(activeOffscreen==&parent&&groupDepth==3&&currentVp==vp&&!stencilWrite&&region[2]==959&&region[3]==543);
 reset();assert(draw_recovered_packet(p,textures,1,identity,1,nullptr));assert(baseline==meshes);
 const float mapping[]={.8f,.1f,-.2f,1.1f,55,-9},clip[]={120.25f,88.75f,730.5f,460.25f};
 reset();assert(draw_recovered_packet(p,textures,1,mapping,1,clip));assert(writes[0]==128&&writes[1]==128);bool low=false,clipRead=false;for(auto v:writes)low|=v==127;for(auto v:reads)clipRead|=v==255;assert(low&&clipRead);assert(activeOffscreen==&parent&&groupDepth==3&&currentVp==vp&&!stencilWrite);
 int mappedCalls=calls;
 bool transformed=false;for(const auto& bytes:meshes)if(bytes.size()==sizeof(vertices)+sizeof(indices)){assert(!std::memcmp(bytes.data()+sizeof(vertices),indices,sizeof indices));EmRenderVertex out[3];std::memcpy(out,bytes.data(),sizeof out);for(unsigned i=0;i<3;++i){assert(!std::memcmp(&out[i].z,&vertices[i].z,8*sizeof(float)));float x=(vertices[i].x+1)*480,y=(1-vertices[i].y)*272;assert(std::fabs(out[i].x-((mapping[0]*x+mapping[2]*y+mapping[4])/480-1))*480<.001f);assert(std::fabs(out[i].y-(1-(mapping[1]*x+mapping[3]*y+mapping[5])/272))*272<.001f);}transformed=true;}assert(transformed);
 // Reuse scratch across changing transforms, then identity, without changing packet bytes.
 uint32_t packetNv=0;const auto* immutable=em_render_packet_vertices(p,&packetNv);
 std::vector<EmRenderVertex> original(immutable,immutable+packetNv);
 for(unsigned trial=0;trial<128;++trial){
  const float varying[]={.1f+trial*.017f,-.3f+trial*.004f,.2f-trial*.003f,.6f+trial*.011f,float(trial*7)-300,float(trial*3)-120};
  reset();assert(draw_recovered_packet(p,textures,1,varying,1,clip));
  for(const auto& bytes:meshes)if(bytes.size()==sizeof(vertices)+sizeof(indices)){
   EmRenderVertex expected[3];std::memcpy(expected,vertices,sizeof expected);
   RecoveredPlayback reference{&parent};reference.affine=varying;
   for(auto& v:expected)assert(reference.map(v));
   EmRenderVertex actual[3];std::memcpy(actual,bytes.data(),sizeof actual);
   for(unsigned k=0;k<3;++k){assert(std::fabs(actual[k].x-expected[k].x)*480<.001f);assert(std::fabs(actual[k].y-expected[k].y)*272<.001f);assert(!std::memcmp(&actual[k].z,&expected[k].z,8*sizeof(float)));}
   assert(!std::memcmp(bytes.data()+sizeof expected,indices,sizeof indices));
  }
  assert(!std::memcmp(immutable,original.data(),original.size()*sizeof(EmRenderVertex)));
 }
 reset();assert(draw_recovered_packet(p,textures,1,identity,1,nullptr));assert(baseline==meshes);
 const float overflowMap[]={3.4e38f,0,0,1,0,0};reset();assert(!draw_recovered_packet(p,textures,1,overflowMap,1,nullptr)&&calls==0&&meshes.empty());
 std::puts("PASS 128 changing affine maps within 0.001 stage pixel; other vertex fields byte-exact; immutable source; identity after scratch reuse; overflow rejected before drawing");
 // Bulk can fit after per-mesh alignment would exhaust the remaining arena.
 auto* tight=em_render_packet_create();assert(!em_render_packet_mesh(tight,&lease,vertices,3,indices,3));
 assert(!em_render_packet_mesh(tight,&lease,vertices,3,indices,3));assert(em_render_packet_seal(tight));
 reset();const size_t tightBytes=((6*sizeof(EmRenderVertex)+15)&~size_t(15))+((6*sizeof(uint16_t)+15)&~size_t(15));
 recovered.used=recoveredArenaSize-tightBytes;
 assert(draw_recovered_packet(tight,textures,1,nullptr,1,nullptr));assert(recovered.used==recoveredArenaSize);
 assert(meshes.size()==2);assert(meshes[0]==meshes[1]); // Explicit repeated passes must both draw.
 em_render_packet_destroy(tight);
 for(const auto& mesh:pendingMeshes){assert(!std::memcmp(mesh.vertices,mesh.expected.data(),mesh.vb));assert(!std::memcmp(mesh.indices,mesh.expected.data()+mesh.vb,mesh.ib));}
 tight=em_render_packet_create();assert(!em_render_packet_mesh(tight,&lease,vertices,3,indices,3));assert(!em_render_packet_mesh(tight,&lease,vertices,3,indices,3));assert(em_render_packet_seal(tight));
 reset();recovered.used=recoveredArenaSize-tightBytes+16;const size_t exhaustedUsed=recovered.used;
 assert(!draw_recovered_packet(tight,textures,1,mapping,1,nullptr));assert(calls==0&&pendingMeshes.empty()&&recovered.used==exhaustedUsed);
 em_render_packet_destroy(tight);
 std::puts("PASS exact bulk capacity despite per-mesh padding; exhausted arena rejected before GXM; repeated passes and post-destroy GPU bytes preserved");
 // A valid sparse stream may retain unused vertices; exercise the fallback.
 auto* sparse=em_render_packet_create();for(unsigned i=0;i<3;++i)assert(!em_render_packet_mesh(sparse,&lease,vertices,3,indices,3));
 sparse->command_count=1;assert(em_render_packet_seal(sparse));
 reset();recovered.used=recoveredArenaSize-144;assert(draw_recovered_packet(sparse,textures,1,nullptr,1,nullptr));
 assert(recovered.used==recoveredArenaSize&&meshes.size()==1);em_render_packet_destroy(sparse);
 for(const auto& mesh:pendingMeshes){assert(!std::memcmp(mesh.vertices,mesh.expected.data(),mesh.vb));assert(!std::memcmp(mesh.indices,mesh.expected.data()+mesh.vb,mesh.ib));}
 std::puts("PASS sparse stream falls back to per-mesh upload within remaining arena; submitted data outlives packet");
 // Emulate late GPU reads after another packet has overwritten CPU map scratch.
 reset();assert(draw_recovered_packet(p,textures,1,mapping,1,clip));
 const size_t firstEnd=recovered.used;const size_t firstDraws=pendingMeshes.size();
 assert(draw_recovered_packet(p,textures,1,identity,1,nullptr));assert(recovered.used>firstEnd);
 for(size_t i=0;i<firstDraws;++i){const auto& mesh=pendingMeshes[i];
  assert(mesh.vertices>=static_cast<const unsigned char*>(recovered.arena.p));
  assert(mesh.vertices+mesh.vb<=static_cast<const unsigned char*>(recovered.arena.p)+firstEnd);
  assert(!std::memcmp(mesh.vertices,mesh.expected.data(),mesh.vb));
  assert(!std::memcmp(mesh.indices,mesh.expected.data()+mesh.vb,mesh.ib));
 }
 std::puts("PASS packet stream remains byte-exact for deferred GPU reads after subsequent packet/scratch reuse");
 // Explicit frame hints attach unused DS when a background group resumes main.
 reset();recoveredFramePackets=true;sceneOpen=active=false;assert(resume_target(nullptr));assert(recoveredDepthAttached&&recovered.depthUsed);
 assert(recovered_group_depth(nullptr)==nullptr);assert(recovered_finish());assert(!recovered.depthUsed);
 reset();recoveredFramePackets=false;sceneOpen=active=false;assert(resume_target(nullptr));assert(!recoveredDepthAttached&&!recovered.depthUsed);
 // Production resume_target prebinds only a fresh, recovered-only 960x544 parent.
 reset();sceneOpen=active=false;assert(resume_target(&parent,true));assert(recoveredDepthAttached&&recovered.depthUsed);
 assert(recovered_group_depth(&parent)==nullptr); // No second borrow while GPU may reference DS.
 auto* simple=em_render_packet_create();assert(!em_render_packet_command(simple,EM_RENDER_MATERIAL,0,0,0,0));
 assert(!em_render_packet_command(simple,EM_RENDER_MASK_STATE,0,0,0,0));assert(!em_render_packet_mesh(simple,&lease,vertices,3,indices,3));assert(em_render_packet_seal(simple));
 meshes.clear();assert(draw_recovered_packet(simple,textures,1,nullptr,1,nullptr));assert(meshes.size()==1);
 assert(recoveredDepthAttached&&recovered.depthUsed&&activeOffscreen==&parent);
 assert(recovered_group_depth(&parent)==nullptr);
 assert(recovered_finish());assert(!recovered.depthUsed);assert(recovered_group_depth(&parent)!=nullptr);
 // Queued scenes retain ownership until the host fence. A failed End or an
 // explicit diagnostic override must fall back to completion immediately.
 reset();offscreenQueueAllowed=true;recovered.depthUsed=true;auto finishes=finishCalls;
 assert(recovered_finish()&&gpuPending&&recovered.depthUsed&&finishCalls==finishes);
 assert(recovered_group_depth(&parent)!=nullptr&&gpuPending&&finishCalls==finishes);
 reset();recovered.depthUsed=true;recoveredQueueDisabled=true;
 assert(recovered_finish()&&!gpuPending&&!recovered.depthUsed&&finishCalls==finishes+1);
 reset();recoveredQueueDisabled=false;recovered.depthUsed=true;failAt=0;
 assert(!recovered_finish()&&!gpuPending&&!recovered.depthUsed&&finishCalls==finishes+2);
 offscreenQueueAllowed=false;failAt=-1;
 puts("PASS queued packet ownership, explicit fallback and failed-End completion");
 reset();parentTexture.w=480;assert(recovered_group_depth(&parent)==nullptr&&!recovered.depthUsed);parentTexture.w=960;
 reset();recovered.ready=false;assert(recovered_group_depth(&parent)==nullptr&&!recovered.depthUsed);recovered.ready=true;
 reset();sceneOpen=active=false;failAt=0;assert(!resume_target(&parent,true)&&!active&&!recoveredDepthAttached&&recovered.depthUsed);
 em_render_packet_destroy(simple);
 std::puts("PASS production parent prebind: ready/full target only; conservative DS cannot reborrow before completion; failed Begin stays inactive");
 unsigned failures=0;for(int i=0;i<count;++i){reset();failAt=i;bool ok=draw_recovered_packet(p,textures,1,nullptr,1,nullptr);assert(!ok&&!frameGroupsComplete&&groupDepth==3);if(active)assert(activeOffscreen==&parent&&currentVp==vp&&!stencilWrite);++failures;}
 for(int i=0;i<mappedCalls;++i){reset();failAt=i;bool ok=draw_recovered_packet(p,textures,1,mapping,1,clip);assert(!ok&&!frameGroupsComplete&&groupDepth==3);if(active)assert(activeOffscreen==&parent&&currentVp==vp&&!stencilWrite);++failures;}
 reset();assert(!draw_recovered_packet(p,textures,1,nullptr,.5f,nullptr)&&calls==0);assert(activeOffscreen==&parent&&groupDepth==3);
 const float badMap[]={1,0,0,1,NAN,0};reset();assert(!draw_recovered_packet(p,textures,1,badMap,1,nullptr)&&calls==0);
 const float badClip[]={0,0,NAN,544};reset();assert(!draw_recovered_packet(p,textures,1,nullptr,1,badClip)&&calls==0);
 parentTexture.w=480;reset();assert(!draw_recovered_packet(p,textures,1,nullptr,1,nullptr)&&calls==0);parentTexture.w=960;
 // Same production command sequences with reuse disabled at each operation.
 // Track both faces and the actual fragment pointer, not just command counts.
 uintptr_t programId=100;
 for(auto& row:recovered.materials)for(auto*& program:row)program=reinterpret_cast<SceGxmFragmentProgram*>(programId++);
 for(auto& row:recovered.softMaterials)for(auto*& program:row)program=reinterpret_cast<SceGxmFragmentProgram*>(programId++);
 for(auto*& program:recovered.writers)program=reinterpret_cast<SceGxmFragmentProgram*>(programId++);
 recovered.alpha=reinterpret_cast<SceGxmFragmentProgram*>(programId++);recovered.clear=reinterpret_cast<SceGxmFragmentProgram*>(programId++);
 std::vector<PlainState> expected;unsigned uncachedCalls=0,cachedCalls=0;
 for(unsigned pass=0;pass<2;++pass){reset();plainState={};stateCalls=0;RecoveredPlayback replay{&parent};uint32_t rng=0x811a918a;
  for(unsigned n=0;n<100000;++n){if(!pass)replay.invalidate_plain();rng=rng*1664525u+1013904223u;unsigned action=(rng>>16)%16;recovered.used=0;
   bool ok=true;
   switch(action){
    case 0:ok=replay.step((rng>>8)&7,(rng>>6)%3);break;
    case 1:ok=replay.reset(rng&7);break;
    case 2:ok=replay.clear((rng&1)?1:0);break;
    case 3:ok=replay.scene(nullptr,false);break;
    case 4:ok=replay.disable();break;
    case 5:replay.clip=(rng&1)?clip:nullptr;ok=replay.clip_setup();break;
    case 6:ok=replay.mask(3,(rng&1)?4:0);break;
    case 7:replay.bound=(rng&1)!=0;replay.building=(rng&2)!=0;ok=replay.mask(0,0);break;
    case 8:ok=replay.material((rng&1)?0:UINT32_MAX)&&replay.mask(0,0);break;
    default:ok=replay.mask(0,0);break;
   }
   assert(ok);if(!pass)expected.push_back(plainState);else assert(!std::memcmp(&expected[n],&plainState,sizeof plainState));
  }
  if(!pass)uncachedCalls=stateCalls;else cachedCalls=stateCalls;
 }
 assert(cachedCalls<uncachedCalls);std::printf("PASS plain-state differential 100000 operations; state calls %u -> %u\n",uncachedCalls,cachedCalls);
 em_render_packet_destroy(p);assert(!refs);std::free(recovered.arena.p);std::printf("PASS identity trace exact; affine preserves zw/RGBA/UV/index; clip high-bit/hard low-bit; parent/group/state restored; %u injected API failures marked incomplete; opacity fails before draw\n",failures);
}
