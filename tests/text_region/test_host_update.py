"""Exercise the exact production region-update function with guarded host memory."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[2]
src=(root/'host-gxm/src/gxm_bridge.cpp').read_text()
start=src.index('extern "C" int art3m1s_gxm_update_texture_region(')
brace=src.index('{',start); depth=1; end=brace+1
while depth:
    depth += (src[end]=='{')-(src[end]=='}'); end+=1
fn=src[start:end]
mock=r'''
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <cassert>
struct NVGcontext{};
struct Texture { uint8_t* data; };
struct Window { int context; };
static Window window;
static NVGcontext vg;
static Texture tex;
static int tw=13,th=7,waits=0;
static bool font_update_phase=false,font_update_waited=false,frame_active=false;
static std::unordered_map<uint64_t,int> textures{{9,1}};
namespace brls {
struct PsvVideoContext { Window* getWindow(){return &window;} };
static PsvVideoContext video;
struct Platform { PsvVideoContext* getVideoContext(){return &video;} };
static Platform platform;
struct Application {
static NVGcontext* getNVGContext(){return &vg;}
static Platform* getPlatform(){return &platform;}
};
}
void nvgImageSize(NVGcontext*,int,int*w,int*h){*w=tw;*h=th;}
Texture* nvgxmImageHandle(NVGcontext*,int){return &tex;}
void sceGxmFinish(int){++waits;}
'''
test=r'''
int main(){
std::vector<uint8_t> source(13*7*4), dest(16*7*4+32,0xcd);
for(size_t i=0;i<source.size();++i)source[i]=uint8_t(i*7);
tex.data=dest.data()+16;
auto update=[&](unsigned x,unsigned y,unsigned w,unsigned h){return art3m1s_gxm_update_texture_region(9,13,7,source.data(),source.size(),x,y,w,h);};
assert(!update(2,1,3,4)); assert(waits==0);
font_update_phase=true; frame_active=true;
assert(!update(2,1,3,4)); assert(waits==0); frame_active=false;
assert(!update(12,0,2,1));assert(!update(0,0,0,1));assert(waits==0);
assert(update(2,1,3,4));assert(waits==1);
for(int y=0;y<7;y++)for(int x=0;x<16;x++)for(int c=0;c<4;c++){
uint8_t expected=x>=2&&x<5&&y>=1&&y<5?source[(y*13+x)*4+c]:0xcd;
assert(tex.data[(y*16+x)*4+c]==expected);
}
assert(update(12,6,1,1)); assert(waits==1);
font_update_waited=false;assert(update(0,0,13,7));assert(waits==2);
for(int y=0;y<7;y++){
assert(!memcmp(tex.data+y*16*4,source.data()+y*13*4,13*4));
for(int x=13*4;x<16*4;x++)assert(tex.data[y*16*4+x]==0xcd);
}
for(int i=0;i<16;i++){assert(dest[i]==0xcd);assert(dest[dest.size()-1-i]==0xcd);}
}
'''
out=root/'build/text-region-host-test';out.mkdir(exist_ok=True)
(out/'test.cpp').write_text(mock+fn+test)
subprocess.run(['g++','-std=c++17','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(out/'test.cpp'),'-o',str(out/'test')],check=True)
subprocess.run([str(out/'test')],check=True)
print('PASS: production region copy, stride/padding/guards, bounds, phase gating, one wait per batch')
