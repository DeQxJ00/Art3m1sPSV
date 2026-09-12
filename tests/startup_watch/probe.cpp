#define STARTUP_WATCH_LOG "ux0:data/art3-startup-watch-test.log"
#define STARTUP_WATCH_STACK_PREFIX "ux0:data/art3-startup-watch-test-stack"
#include "watch.hpp"
#include <cstring>

int main(){
    // Some emulator builds append despite O_TRUNC. Never accept observations
    // from an earlier run as evidence for this probe.
    int previous=sceIoOpen(STARTUP_WATCH_LOG,SCE_O_RDONLY,0);
    if(previous>=0){
        sceIoClose(previous);
        if(sceIoRemove(STARTUP_WATCH_LOG)<0){sceKernelExitProcess(2);return 2;}
    }
    startupwatch::start();
    if(startupwatch::worker<0)return 1;
    startupwatch::mark("video-open-close-old");
    startupwatch::mark("deliberate-delay");
    sceKernelDelayThread(12000000);
    for(unsigned i=0;i<100;++i){startupwatch::mark("resumed");sceKernelDelayThread(100000);}
    startupwatch::stop();
    char data[4096]{};
    int fd=sceIoOpen(STARTUP_WATCH_LOG,SCE_O_RDONLY,0);
    int n=fd>=0?sceIoRead(fd,data,sizeof(data)-1):-1;
    if(fd>=0)sceIoClose(fd);
    bool ok=n>0&&std::strstr(data,"phase=deliberate-delay")&&std::strstr(data,"unchanged_ms=2000")&&std::strstr(data,"phase=resumed");
    fd=sceIoOpen(STARTUP_WATCH_LOG,SCE_O_WRONLY|SCE_O_APPEND,0);
    const char* result=ok?"PASS: observed delay and recovery\n":"FAIL: missing phase observation\n";
    if(fd>=0){sceIoWrite(fd,result,std::strlen(result));sceIoClose(fd);}
    sceKernelExitProcess(ok?0:1);
    return ok?0:1;
}
