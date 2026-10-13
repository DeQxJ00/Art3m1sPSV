"""Compile the actual resource bridge against a fake GPU; exercise status/lifetime contracts."""
from pathlib import Path
import subprocess,shutil,json,hashlib
root=Path(__file__).resolve().parents[1]; recovery=root.parent/'emote-psv-test';out=root/'build/recovered-emote-bridge-test';out.mkdir(parents=True,exist_ok=True)
def linux(p):return '/mnt/'+str(p)[0].lower()+str(p)[2:].replace('\\','/')
for n in ['recovered_emote_bridge.cpp','recovered_emote_bridge.hpp','compressed_layout.hpp','bc3_layout.hpp','builtin_effects.hpp']:shutil.copyfile(root/'host-direct/src'/n,out/n)
for n in ['texture_host.h','render_packet.h']:shutil.copyfile(recovery/'native'/n,out/n)
# The packet implementation is tested independently in recovery; this host test
# mocks only accessor output to exercise ID validation and actual bridge callbacks.
(out/'gpu.hpp').write_text(r'''#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include "builtin_effects.hpp"
struct EmRenderPacket;
namespace direct {
struct Texture {uint64_t contentRevision=0;std::vector<uint8_t> data;};
bool group_begin();void group_end(const EffectDraw&,Texture*,float,float);
bool in_scene();bool compressed_texture_allowed(unsigned);
Texture* texture(unsigned,unsigned,const uint8_t*);
Texture* texture_compressed(unsigned,unsigned,unsigned,bool,const uint8_t*,size_t,bool);
void destroy(Texture*);bool draw_recovered_packet(const EmRenderPacket*,Texture*const*,size_t,const float*,float,const float*);
}
''')
(out/'test.cpp').write_text(r'''#include "recovered_emote_bridge.hpp"
#include "gpu.hpp"
#include "render_packet.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>
struct EmRenderPacket {uint32_t count=0;uint64_t handles[2]{};int status=1;bool valid=true;};
extern "C" uint32_t em_render_packet_version(const EmRenderPacket*p){return p&&p->valid?EM_RENDER_PACKET_VERSION:0;}
extern "C" uint32_t em_render_packet_texture_count(const EmRenderPacket*p){return p->count;}
extern "C" int em_render_packet_prepare_host(const EmRenderPacket*p,uint64_t*h,uint32_t cap){assert(cap>=p->count);if(p->status==1)for(unsigned i=0;i<p->count;i++)h[i]=p->handles[i];return p->status;}
static bool scene=false,fail=false,allowed=true,swizzled=false,drawOkay=true;
static unsigned live=0,draws=0,uploads=0,groups=0,composites=0;static std::vector<direct::Texture*> retired;
namespace direct {
bool group_begin(){++groups;return true;}void group_end(const EffectDraw& d,Texture*,float,float){assert(groups==1&&d.tint[3]==.5f&&d.blend==5);--groups;++composites;}
bool in_scene(){return scene;}bool compressed_texture_allowed(unsigned f){return allowed&&f==3;}
Texture* texture(unsigned w,unsigned h,const uint8_t*p){++uploads;if(fail)return nullptr;auto*t=new Texture;t->data.assign(p,p+w*h*4);++live;return t;}
Texture* texture_compressed(unsigned,unsigned,unsigned f,bool opaque,const uint8_t*p,size_t n,bool sw){assert(f==3&&!opaque);swizzled=sw;++uploads;if(fail)return nullptr;auto*t=new Texture;t->data.assign(p,p+n);++live;return t;}
void destroy(Texture*t){if(scene)retired.push_back(t);else{delete t;--live;}}
bool draw_recovered_packet(const EmRenderPacket*,Texture*const*t,size_t n,const float* affine,float opacity,const float* clip){assert(!affine&&opacity==1&&!clip);assert(scene);for(size_t i=0;i<n;i++)assert(t[i]&&!t[i]->data.empty());if(n==2){assert(t[0]->data[0]==0);assert(t[1]->data[0]==42);}++draws;return drawOkay;}
}
int main(){auto*h=direct::recovered_texture_host();assert(h&&h->upload&&h->release);uint8_t pixels[64]{};EmTextureSource s{1,EM_TEXTURE_RGBA8,EM_TEXTURE_LINEAR,4,4,4,4,64,pixels};uint64_t a=99,b=99;
 assert(h->upload(h->user,nullptr,&a)==-1&&a==0);s.bytes=63;assert(h->upload(h->user,&s,&a)==-1&&a==0);s.bytes=64;
 scene=true;assert(h->upload(h->user,&s,&a)==0&&a==0&&uploads==0);scene=false;fail=true;assert(h->upload(h->user,&s,&a)==0&&a==0&&live==0);fail=false;
 assert(h->upload(h->user,&s,&a)==1&&a&&live==1);pixels[0]=42;
 s.format=EM_TEXTURE_BC3;s.layout=EM_TEXTURE_SWIZZLED;s.bytes=16;assert(h->upload(h->user,&s,&b)==1&&b!=a&&swizzled&&live==2);
 s.layout=EM_TEXTURE_LINEAR;uint64_t c=0;assert(h->upload(h->user,&s,&c)==1&&!swizzled);h->release(h->user,c);assert(live==2);
 allowed=false;assert(h->upload(h->user,&s,&c)==-1&&!c);allowed=true;s.layout=3;assert(h->upload(h->user,&s,&c)==-1&&!c);
 EmRenderPacket p{2,{a,b}};assert(direct::prepare_recovered_packet(&p)==1);assert(direct::replay_recovered_packet(&p,nullptr,1,nullptr)==-1);scene=true;
 assert(direct::replay_recovered_packet(&p,nullptr,1,nullptr)==1&&draws==1);p.status=0;assert(direct::replay_recovered_packet(&p,nullptr,1,nullptr)==0&&draws==1);p.status=-1;assert(direct::replay_recovered_packet(&p,nullptr,1,nullptr)==-1&&draws==1);p.status=1;drawOkay=false;assert(direct::replay_recovered_packet(&p,nullptr,1,nullptr)==-1);drawOkay=true;
 assert(direct::replay_recovered_packet(&p,nullptr,.5f,nullptr)==1&&groups==0&&composites==1);drawOkay=false;assert(direct::replay_recovered_packet(&p,nullptr,.5f,nullptr)==-1&&groups==0&&composites==2);drawOkay=true;assert(direct::replay_recovered_packet(&p,nullptr,-1,nullptr)==-1&&groups==0&&composites==2);
 h->release(h->user,a);assert(retired.size()==1&&live==2);assert(direct::replay_recovered_packet(&p,nullptr,1,nullptr)==-1);h->release(h->user,a);assert(retired.size()==1);
 scene=false;for(auto*t:retired){delete t;--live;}retired.clear();h->release(h->user,b);assert(live==0);p.valid=false;assert(direct::prepare_recovered_packet(&p)==-1);assert(direct::prepare_recovered_packet(nullptr)==-1);
 puts("PASS actual bridge: input/format errors, scene/allocation deferral, opaque token uniqueness, linear/swizzled BC3, ready/deferred/error replay, missing token, host GPU retirement and cleanup");}
''')
subprocess.run(['wsl','-e','g++','-std=c++17','-O2','-g','-fsanitize=address,undefined','-DDIRECT_RECOVERED_PACKET=1','-I',linux(out),linux(out/'recovered_emote_bridge.cpp'),linux(out/'test.cpp'),'-o',linux(out/'test')],check=True)
r=subprocess.run(['wsl','-e',linux(out/'test')],capture_output=True,text=True);print(r.stdout,r.stderr);r.check_returncode();assert not r.stderr
report={'status':'pass','scope':'Actual C++ resource bridge, fake GPU and packet accessors; no physical GPU or packet lease refcount equivalence claimed. ASan/UBSan.','output':r.stdout,'sha256':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [root/'host-direct/src/recovered_emote_bridge.cpp',root/'host-direct/src/recovered_emote_bridge.hpp',Path(__file__)]}}
(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
