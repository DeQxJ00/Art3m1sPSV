#pragma once
#include <atomic>
#include <cstdio>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>

// Diagnostic-only build: independent of the main log queue, no recovery or
// forced timeout. Keep literal phase names alive for the observer thread.
namespace startupwatch {
inline std::atomic<const char*> phase{"startup"};
inline std::atomic<unsigned> epoch{0};
inline std::atomic<bool> stopping{false};
inline SceUID mainThread=-1,worker=-1;
inline void mark(const char* name){
    if(sceKernelGetThreadId()!=mainThread)return;
    phase.store(name);epoch.fetch_add(1);
}
inline int run(SceSize,void*){
    auto fd=sceIoOpen("ux0:data/art3m1s-gxm/startup-watch.log",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0777);
    if(fd>=0)sceIoClose(fd);
    unsigned previous=epoch.load(),unchanged=0,ticks=0;
    while(!stopping.load()){
        sceKernelDelayThread(500000);
        const auto current=epoch.load();unchanged=current==previous?unchanged+1:0;previous=current;
        // One liveness line per ten seconds, plus a stuck report after two
        // seconds and every five seconds thereafter. Never use direct::log.
        if(++ticks%20 && unchanged!=4 && !(unchanged>=4 && unchanged%10==0))continue;
        SceKernelThreadInfo info{};info.size=sizeof(info);
        const int result=sceKernelGetThreadInfo(mainThread,&info);
        char line[512];const int n=std::snprintf(line,sizeof(line),
            "at_us=%llu phase=%s unchanged_ms=%u epoch=%u main=%x query=%x status=%x wait_type=%x wait_id=%x cpu=%d clocks=%llu\n",
            (unsigned long long)sceKernelGetProcessTimeWide(),phase.load(),unchanged*500,current,
            unsigned(mainThread),unsigned(result),unsigned(info.status),unsigned(info.waitType),
            unsigned(info.waitId),info.currentCpuId,(unsigned long long)info.runClocks);
        fd=sceIoOpen("ux0:data/art3m1s-gxm/startup-watch.log",SCE_O_WRONLY|SCE_O_APPEND,0777);
        if(fd>=0){if(n>0)sceIoWrite(fd,line,unsigned(n)<sizeof(line)?n:sizeof(line)-1);sceIoClose(fd);}
    }
    return 0;
}
inline void start(){
    mainThread=sceKernelGetThreadId();
    worker=sceKernelCreateThread("art3_startup_watch",run,180,65536,0,0,nullptr);
    if(worker>=0)sceKernelStartThread(worker,0,nullptr);
}
inline void stop(){
    stopping.store(true);
    if(worker>=0){sceKernelWaitThreadEnd(worker,nullptr,nullptr);sceKernelDeleteThread(worker);worker=-1;}
}
}
extern "C" void art3_startup_mark(const char* name){startupwatch::mark(name);}
