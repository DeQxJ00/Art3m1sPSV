#pragma once
#include <atomic>
#include <cstdio>
#include <cstring>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/io/fcntl.h>
#ifndef STARTUP_WATCH_LOG
#define STARTUP_WATCH_LOG "ux0:data/art3m1s-gxm/startup-watch.log"
#endif
#ifndef STARTUP_WATCH_STACK_PREFIX
#define STARTUP_WATCH_STACK_PREFIX "ux0:data/art3m1s-gxm/startup-stack"
#endif

// Diagnostic-only build: independent of the main log queue, no recovery or
// forced timeout. Keep literal phase names alive for the observer thread.
namespace startupwatch {
inline std::atomic<const char*> phase{"startup"};
inline std::atomic<unsigned> epoch{0};
inline std::atomic<bool> stopping{false};
inline std::atomic<bool> videoSeen{false};
inline SceUID mainThread=-1,worker=-1;
inline void mark(const char* name){
    if(sceKernelGetThreadId()!=mainThread)return;
    if(!videoSeen.load()&&!std::strcmp(name,"video-open-close-old"))videoSeen.store(true);
    phase.store(name);epoch.fetch_add(1);
}
inline int run(SceSize,void*){
    auto fd=sceIoOpen(STARTUP_WATCH_LOG,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0777);
    if(fd>=0)sceIoClose(fd);
    unsigned previous=epoch.load(),unchanged=0,ticks=0,dumps=0,lastDumpEpoch=0;
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
        fd=sceIoOpen(STARTUP_WATCH_LOG,SCE_O_WRONLY|SCE_O_APPEND,0777);
        if(fd>=0){
            if(n>0)sceIoWrite(fd,line,unsigned(n)<sizeof(line)?n:sizeof(line)-1);
            if(unchanged>=4 && result>=0 && info.waitId>=0){
                auto waitId=info.waitId;
                for(unsigned depth=0;depth<4;++depth){
                    SceKernelMutexInfo mutex{};mutex.size=sizeof(mutex);
                    const int mr=sceKernelGetMutexInfo(waitId,&mutex);
                    if(mr<0)break; // Not every wait is a kernel mutex.
                    SceKernelThreadInfo owner{};owner.size=sizeof(owner);
                    const int tr=mutex.currentOwnerId>=0?sceKernelGetThreadInfo(mutex.currentOwnerId,&owner):-1;
                    const int m=std::snprintf(line,sizeof(line),
                        "mutex depth=%u id=%x name=%.32s count=%d waiters=%d owner=%x owner_query=%x owner_name=%.32s owner_status=%x owner_wait_type=%x owner_wait_id=%x owner_clocks=%llu\n",
                        depth,unsigned(waitId),mutex.name,mutex.currentCount,mutex.numWaitThreads,
                        unsigned(mutex.currentOwnerId),unsigned(tr),owner.name,unsigned(owner.status),
                        unsigned(owner.waitType),unsigned(owner.waitId),(unsigned long long)owner.runClocks);
                    if(m>0)sceIoWrite(fd,line,unsigned(m)<sizeof(line)?m:sizeof(line)-1);
                    if(tr<0||owner.waitId<0||owner.waitId==waitId)break;
                    waitId=owner.waitId;
                }
            }
            if(videoSeen.load()&&unchanged>=20&&result>=0&&dumps<4&&lastDumpEpoch!=current){
                // Raw stack evidence, not a reconstructed backtrace. Validate
                // the full range against a mapped block before reading it.
                SceKernelMemBlockInfo block{};block.size=sizeof(block);
                const int br=info.stack&&info.stackSize>0&&info.stackSize<=2*1024*1024?
                    sceKernelGetMemBlockInfoByRange(info.stack,info.stackSize,&block):-1;
                const uintptr_t begin=reinterpret_cast<uintptr_t>(info.stack);
                const uintptr_t mapped=reinterpret_cast<uintptr_t>(block.mappedBase);
                const bool valid=br>=0&&begin>=mapped&&size_t(info.stackSize)<=block.mappedSize&&
                    begin-mapped<=block.mappedSize-size_t(info.stackSize);
                int bytes=-1;
                if(valid){
                    char path[192];std::snprintf(path,sizeof(path),"%s-%u.bin",STARTUP_WATCH_STACK_PREFIX,dumps);
                    const int stackFd=sceIoOpen(path,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0777);
                    if(stackFd>=0){bytes=sceIoWrite(stackFd,info.stack,info.stackSize);sceIoClose(stackFd);}
                }
                int m=std::snprintf(line,sizeof(line),"stack_dump index=%u epoch=%u address=%p size=%d range_query=%x valid=%d written=%d observer_code=%p\n",
                    dumps,current,info.stack,info.stackSize,unsigned(br),int(valid),bytes,reinterpret_cast<void*>(&run));
                if(m>0)sceIoWrite(fd,line,unsigned(m)<sizeof(line)?m:sizeof(line)-1);
                ++dumps;lastDumpEpoch=current;
            }
            sceIoClose(fd);
        }
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
