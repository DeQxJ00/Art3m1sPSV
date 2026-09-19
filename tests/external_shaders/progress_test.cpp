#include "../../host-direct/src/shader_progress.hpp"
#include <cassert>
#include <iostream>

int main(){
    using direct::ShaderStage;
    direct::ShaderProgress p;
    assert(!p.should_present(0,0,true)&&p.fraction()==0);
    p.update(0,3,"",ShaderStage::Batch);
    p.update(0,3,"other.glsl",ShaderStage::Skipped);
    assert(p.skipped==1&&p.failed==0&&p.done==0&&p.total==3);
    p.update(0,3,"system/shader/pc/first.hlsl",ShaderStage::Read);
    assert(p.file=="first.hlsl"&&p.should_present(1,0,true));
    p.update(0,0,nullptr,ShaderStage::Builtin);
    assert(!p.should_present(50,1,false)); // no vblank per fast stage
    p.update(1,3,"first.hlsl",ShaderStage::Ready);
    assert(p.fraction()>0.3f&&p.fraction()<0.34f&&!p.finished());
    p.update(1,3,"second.hlsl",ShaderStage::Read);
    p.update(0,0,nullptr,ShaderStage::Compile);
    assert(p.done==1&&p.should_present(60,50,false)); // before blocking compile
    p.update(2,3,"second.hlsl",ShaderStage::Failed);
    assert(p.failed==1&&p.done==2);
    p.update(2,3,"third.hlsl",ShaderStage::Read);
    p.update(0,0,nullptr,ShaderStage::CacheHit);
    assert(p.failed==1&&p.done==2);
    p.update(3,3,"third.hlsl",ShaderStage::Ready);
    assert(p.finished()&&p.fraction()==1);
    p.update(3,3,"",ShaderStage::BatchDone);
    assert(p.should_present(70,60,false)&&p.skipped==1&&p.failed==1);
    p.update(0,1,"",ShaderStage::Batch);
    p.update(0,1,"next.hlsl",ShaderStage::Read);
    assert(!p.finished()&&p.failed==0&&p.fraction()==0);
    p.update(8,1,"next.hlsl",ShaderStage::Ready);
    assert(p.fraction()==1); // defensive bounds
    p.update(0,0,"",ShaderStage::Batch);
    assert(p.total==0&&p.done==0&&p.failed==0&&p.skipped==0&&p.file.empty());
    p.update(0,0,"only.glsl",ShaderStage::Skipped);
    p.update(0,0,"second.cg",ShaderStage::Skipped);
    assert(p.skipped==2&&p.failed==0&&p.done==0&&p.total==0&&p.file=="second.cg");
    assert(!p.should_present(90,80,false)); // no present for every skipped item
    p.update(0,0,"",ShaderStage::BatchDone);
    assert(p.should_present(90,80,false)); // all-skipped summary is still visible
    std::cout<<"PASS shader batch counts, errors, cache stages, throttling and reset\n";
}
