#pragma once
#include <algorithm>
#include <cstdint>

namespace direct {
// Milestones, not an estimate of bytes/work remaining in arbitrary boot scripts.
// Completion is independent of the optional system PS-button lock.
struct LoadingProgress {
    enum Stage { Archives, Engine, Script, FirstFrame, Complete };
    Stage stage=Archives;
    uint64_t started=0,stageStarted=0;
    unsigned emptyFrames=0;
    bool pending=true;
    void begin(uint64_t now){started=stageStarted=now;}
    bool advance(Stage next,uint64_t now){
        if(next<=stage||!pending)return false;
        stage=next;stageStarted=now;return true;
    }
    // GXM's mandatory black clear is excluded by the bridge before this check.
    bool observe_frame(int result,bool hasContent){
        if(!pending||result<=0)return false;
        if(!hasContent){++emptyFrames;return false;}
        return true;
    }
    void presented(uint64_t now){stage=Complete;stageStarted=now;pending=false;}
    float fraction(unsigned done,unsigned total)const{
        if(stage==Complete)return 1.f;
        if(stage==Archives)return total?.25f*float(std::min(done,total))/total:0.f;
        return float(stage)*.25f;
    }
};
}
