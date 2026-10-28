#pragma once
#include <cstdint>
#include <algorithm>

namespace direct {
struct PerformanceDrawStats {
    uint64_t draws=0,triangles=0,passes=0;
    uint64_t gpuWaitUs=0,queueWaitUs=0,uploads=0,uploadBytes=0,uploadUs=0,shared=0;
};
// Main-thread wall times, including waits; these are not GPU timer queries or
// CPU utilization. The sampling interval also includes work between frames.
struct PerformanceSample {
    double fps=0,frameMs=0,maxMs=0,mediaMs=0,logicMs=0,presentMs=0;
    double draws=0,triangles=0,passes=0;
    uint64_t peakDraws=0;
    double gpuWaitMs=0,queueWaitMs=0,uploads=0,uploadMiB=0,uploadMs=0,shared=0;
};
struct PerformanceWindow {
    uint64_t start=0,last=0,frames=0,maxUs=0,mediaUs=0,logicUs=0,presentUs=0;
    PerformanceDrawStats total{};
    uint64_t peakDraws=0;
    PerformanceSample sample{};
    void reset(uint64_t now){*this={};start=last=now;}
    bool add(uint64_t now,uint64_t media,uint64_t logic,uint64_t present,PerformanceDrawStats draw){
        if(now<=last)return false;
        maxUs=std::max(maxUs,now-last);last=now;++frames;
        mediaUs+=media;logicUs+=logic;presentUs+=present;
        total.draws+=draw.draws;total.triangles+=draw.triangles;total.passes+=draw.passes;
        total.gpuWaitUs+=draw.gpuWaitUs;total.queueWaitUs+=draw.queueWaitUs;
        total.uploads+=draw.uploads;total.uploadBytes+=draw.uploadBytes;
        total.uploadUs+=draw.uploadUs;total.shared+=draw.shared;
        peakDraws=std::max(peakDraws,draw.draws);
        const auto elapsed=now-start;if(elapsed<500000)return false;
        sample={double(frames)*1000000/elapsed,double(elapsed)/frames/1000,
            double(maxUs)/1000,double(mediaUs)/frames/1000,double(logicUs)/frames/1000,
            double(presentUs)/frames/1000,double(total.draws)/frames,
            double(total.triangles)/frames,double(total.passes)/frames,peakDraws};
        sample.gpuWaitMs=double(total.gpuWaitUs)/frames/1000;
        sample.queueWaitMs=double(total.queueWaitUs)/frames/1000;
        sample.uploads=double(total.uploads)/frames;sample.uploadMiB=double(total.uploadBytes)/frames/1048576;
        sample.uploadMs=double(total.uploadUs)/frames/1000;sample.shared=double(total.shared)/frames;
        start=now;frames=maxUs=mediaUs=logicUs=presentUs=peakDraws=0;total={};
        return true;
    }
};
}
