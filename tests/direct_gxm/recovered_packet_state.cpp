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
extern "C" {
#include "material.h"
}
#include "recovered_original_shaders.h"
#include "recovered_original_mask_shaders.h"
static int failAt=-1,calls=0;static bool sceneOpen=true;
static int fallible(){return calls++==failAt?-17:0;}
template<class... A> static int ignore(A...){return 0;}
static const SceGxmProgramParameter* parameter(const SceGxmProgram*,const char*){return reinterpret_cast<const SceGxmProgramParameter*>(1);}
static unsigned resource(const SceGxmProgramParameter*){return 1;}
static float uniforms[128];
static int reserveUniform(SceGxmContext*,void** out){int rc=fallible();if(rc>=0)*out=uniforms;return rc;}
static int uniform(void*,const SceGxmProgramParameter*,unsigned,unsigned,const float*){return fallible();}
static unsigned stencilRead,stencilWrite,stencilRef;static SceGxmVertexProgram* currentVp;
static unsigned region[4];static const void* stream;
static std::vector<std::vector<unsigned char>> meshes;
static std::vector<unsigned> writes,reads;
static int setStream(SceGxmContext*,unsigned,const void* v){stream=v;return fallible();}
static int texture(SceGxmContext*,unsigned,const SceGxmTexture*){return fallible();}
static int draw(SceGxmContext*,SceGxmPrimitiveType,SceGxmIndexFormat,const void* ptr,unsigned n){
 int rc=fallible();if(rc<0)return rc;assert(sceneOpen);auto* ix=static_cast<const uint16_t*>(ptr);unsigned vc=0;for(unsigned i=0;i<n;++i)vc=std::max(vc,unsigned(ix[i])+1);
 const auto* data=static_cast<const unsigned char*>(stream);meshes.emplace_back(data,data+vc*sizeof(EmRenderVertex));meshes.back().insert(meshes.back().end(),static_cast<const unsigned char*>(ptr),static_cast<const unsigned char*>(ptr)+n*2);writes.push_back(stencilWrite);reads.push_back(stencilRead);return 0;
}
static int endScene(SceGxmContext*,void*,void*){assert(sceneOpen);sceneOpen=false;return fallible();}
static int beginScene(SceGxmContext*,unsigned,SceGxmRenderTarget*,void*,void*,SceGxmSyncObject*,const SceGxmColorSurface*,const SceGxmDepthStencilSurface*){assert(!sceneOpen);int rc=fallible();sceneOpen=rc>=0;return rc;}
static void vpSet(SceGxmContext*,SceGxmVertexProgram* p){currentVp=p;}
static void regionSet(SceGxmContext*,SceGxmRegionClipMode,unsigned a,unsigned b,unsigned c,unsigned d){region[0]=a;region[1]=b;region[2]=c;region[3]=d;}
static void stencil(SceGxmContext*,SceGxmStencilFunc,SceGxmStencilOp,SceGxmStencilOp,SceGxmStencilOp,unsigned char read,unsigned char write){stencilRead=read;stencilWrite=write;}
static void ref(SceGxmContext*,unsigned char v){stencilRef=v;}
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
#define sceGxmSetFrontStencilFunc stencil
#define sceGxmSetBackStencilFunc stencil
#define sceGxmSetFrontStencilRef ::ref
#define sceGxmSetBackStencilRef ::ref
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
static Memory allocate(size_t n,int,int){return {-1,std::malloc(n),0,0};}static void release(Memory m){std::free(m.p);}
static bool create_offscreen(Offscreen&){return false;}
static void flush_batch(){}
static uint64_t sceKernelGetProcessTimeWide(){return 0;}
static bool recoveredDepthAttached=false;
static bool resume_target(Offscreen* o){recoveredDepthAttached=false;active=beginScene(ctx,0,o?o->target:target,nullptr,nullptr,nullptr,nullptr,nullptr)>=0;activeOffscreen=active?o:nullptr;currentVp=vp;return active;}
#define DIRECT_RECOVERED_PACKET 1
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
 auto reset=[&](){recoveredDepthAttached=false;active=sceneOpen=true;activeOffscreen=&parent;groupDepth=3;frameGroupsComplete=true;calls=0;meshes.clear();writes.clear();reads.clear();recovered.used=0;failAt=-1;};
 reset();assert(draw_recovered_packet(p,textures,1,nullptr,1,nullptr));auto baseline=meshes;int count=calls;assert(activeOffscreen==&parent&&groupDepth==3&&currentVp==vp&&!stencilWrite&&region[2]==959&&region[3]==543);
 reset();assert(draw_recovered_packet(p,textures,1,identity,1,nullptr));assert(baseline==meshes);
 const float mapping[]={.8f,.1f,-.2f,1.1f,55,-9},clip[]={120.25f,88.75f,730.5f,460.25f};
 reset();assert(draw_recovered_packet(p,textures,1,mapping,1,clip));assert(writes[0]==128&&writes[1]==128);bool low=false,clipRead=false;for(auto v:writes)low|=v==127;for(auto v:reads)clipRead|=v==255;assert(low&&clipRead);assert(activeOffscreen==&parent&&groupDepth==3&&currentVp==vp&&!stencilWrite);
 int mappedCalls=calls;
 bool transformed=false;for(const auto& bytes:meshes)if(bytes.size()==sizeof(vertices)+sizeof(indices)){assert(!std::memcmp(bytes.data()+sizeof(vertices),indices,sizeof indices));EmRenderVertex out[3];std::memcpy(out,bytes.data(),sizeof out);for(unsigned i=0;i<3;++i){assert(!std::memcmp(&out[i].z,&vertices[i].z,8*sizeof(float)));float x=(vertices[i].x+1)*480,y=(1-vertices[i].y)*272;assert(out[i].x==(mapping[0]*x+mapping[2]*y+mapping[4])/480-1);assert(out[i].y==1-(mapping[1]*x+mapping[3]*y+mapping[5])/272);}transformed=true;}assert(transformed);
 unsigned failures=0;for(int i=0;i<count;++i){reset();failAt=i;bool ok=draw_recovered_packet(p,textures,1,nullptr,1,nullptr);assert(!ok&&!frameGroupsComplete&&groupDepth==3);if(active)assert(activeOffscreen==&parent&&currentVp==vp&&!stencilWrite);++failures;}
 for(int i=0;i<mappedCalls;++i){reset();failAt=i;bool ok=draw_recovered_packet(p,textures,1,mapping,1,clip);assert(!ok&&!frameGroupsComplete&&groupDepth==3);if(active)assert(activeOffscreen==&parent&&currentVp==vp&&!stencilWrite);++failures;}
 reset();assert(!draw_recovered_packet(p,textures,1,nullptr,.5f,nullptr)&&calls==0);assert(activeOffscreen==&parent&&groupDepth==3);
 const float badMap[]={1,0,0,1,NAN,0};reset();assert(!draw_recovered_packet(p,textures,1,badMap,1,nullptr)&&calls==0);
 const float badClip[]={0,0,NAN,544};reset();assert(!draw_recovered_packet(p,textures,1,nullptr,1,badClip)&&calls==0);
 parentTexture.w=480;reset();assert(!draw_recovered_packet(p,textures,1,nullptr,1,nullptr)&&calls==0);parentTexture.w=960;
 em_render_packet_destroy(p);assert(!refs);std::free(recovered.arena.p);std::printf("PASS identity trace exact; affine preserves zw/RGBA/UV/index; clip high-bit/hard low-bit; parent/group/state restored; %u injected API failures marked incomplete; opacity fails before draw\n",failures);
}
