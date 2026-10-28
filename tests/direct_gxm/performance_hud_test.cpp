#include "../../host-direct/src/performance_hud.hpp"
#include <cassert>
static unsigned allocations=0,draws=0;
static unsigned memoryQueries=0;
extern "C" int scePowerGetArmClockFrequency(){return 444;}
extern "C" int scePowerGetGpuClockFrequency(){return 222;}
namespace direct {
PerformanceMemory performance_memory_snapshot(){
    ++memoryQueries;return {true,true,true,128,320,15,80,32,48};
}
Texture* texture(unsigned w,unsigned h,const uint8_t*,const uint8_t*,size_t){
    assert(w==64&&h==64);++allocations;return new Texture;
}
void destroy(Texture* p){assert(p&&allocations);--allocations;delete p;}
void rect(float x,float y,float w,float h,uint32_t){assert(x>=0&&y>=0&&x+w<=960&&y+h<=544);++draws;}
void draw_quad(Texture* t,const Vertex* vertices,unsigned,const float*,Texture*,float,float){
    assert(t);for(unsigned i=0;i<4;++i)assert(vertices[i].x>=8&&vertices[i].x<=308&&vertices[i].y>=8&&vertices[i].y<=430);++draws;
}
}
int main(){
    direct::PerformanceHud hud;
    hud.prepare(false,1000);hud.draw();assert(!allocations&&!draws&&!memoryQueries);
    hud.prepare(true,1000);assert(allocations==1);
    for(unsigned i=1;i<=25;++i){
        hud.prepare(true,1000+i*20000);
        hud.record(1000+i*20000,2000,4000,14000,{100,600,3});
    }
    assert(hud.ready&&hud.window.sample.fps==50&&hud.cpuMHz==444&&hud.gpuMHz==222);
    assert(memoryQueries==1&&hud.memory.vramLive==32&&hud.memory.vramPeak==48);
    hud.draw();assert(draws>1&&allocations==1);
    hud.prepare(false,600000);assert(!hud.ready&&!allocations);
    const auto before=draws;hud.draw();assert(draws==before);
    hud.record(5000000,1,1,1,{1,1,1});assert(hud.window.frames==0&&memoryQueries==1);
    hud.prepare(true,6000000);assert(!hud.ready&&allocations==1);
    hud.prepare(false,6000001);assert(!allocations);
}
