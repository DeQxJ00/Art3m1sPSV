#pragma once
#include <algorithm>
#include <cstdint>
#include <string>

namespace direct {
// Shared with the Vita core progress ABI. Total is the current dispatch batch,
// not a prediction of shaders in later scripts or branches.
enum class ShaderStage : int { Read=0, Ready=1, Failed=2, Builtin=3, Source=4, Cache=5, Compile=6, CacheHit=7, Batch=8, Skipped=9, BatchDone=10 };
using ShaderProgressCallback = void (*)(void*,unsigned,unsigned,const char*,ShaderStage);
inline ShaderProgressCallback shaderProgressCallback=nullptr;
inline void* shaderProgressContext=nullptr;
inline void shader_stage(ShaderStage stage){
    if(shaderProgressCallback)shaderProgressCallback(shaderProgressContext,0,0,nullptr,stage);
}
struct ShaderProgress {
    unsigned done=0,total=0,failed=0,skipped=0;
    bool seen=false;
    ShaderStage stage=ShaderStage::Read;
    std::string file;
    void update(unsigned completed,unsigned count,const char* path,ShaderStage next){
        if(next==ShaderStage::Batch){
            done=failed=skipped=0;total=count;seen=true;file.clear();
        }else if(path){
            done=std::min(completed,count);total=count;seen=true;
            if(*path){file=path;auto slash=file.find_last_of("/\\");if(slash!=std::string::npos)file.erase(0,slash+1);}
        }
        if(next==ShaderStage::Failed)++failed;
        if(next==ShaderStage::Skipped)++skipped;
        stage=next;
    }
    float fraction()const{return total?float(done)/total:0;}
    bool finished()const{return seen&&done==total;}
    bool should_present(uint64_t now,uint64_t previous,bool first)const{
        return seen&&(first||stage==ShaderStage::Compile||stage==ShaderStage::BatchDone||now-previous>=100000);
    }
};
}
