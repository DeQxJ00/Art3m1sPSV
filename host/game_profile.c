#include "game_profile.h"
#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>
#include <libavutil/log.h>
#include <stdio.h>
#include <string.h>
extern void art3m1s_runtime_set_profiler_enabled(void *,int);
extern int art3m1s_runtime_profiler_snapshot(void *,unsigned char *,uint32_t);
static uint64_t since,audio_total,video_total,runtime_total,max_tick;
static unsigned ticks,repaints;
static char game_id[64];
static unsigned char snapshot[16384];

void host_game_profile_start(void *runtime,const char *game){
    snprintf(game_id,sizeof(game_id),"%s",game);
    art3m1s_runtime_set_profiler_enabled(runtime,1);
    since=sceKernelGetProcessTimeWide();audio_total=video_total=runtime_total=max_tick=0;ticks=repaints=0;
    av_log(NULL,AV_LOG_INFO,"[game-perf] enabled game=%s details=ux0:data/art3m1s/perf.log\n",game_id);
}
void host_game_profile_tick(void *runtime,uint64_t frame_start,uint64_t audio_us,uint64_t video_us,uint64_t runtime_us,int presented){
    uint64_t now=sceKernelGetProcessTimeWide(),elapsed=now-since;
    ticks++;repaints+=presented>0;audio_total+=audio_us;video_total+=video_us;runtime_total+=runtime_us;
    if(now-frame_start>max_tick)max_tick=now-frame_start;
    if(elapsed<5000000)return;
    av_log(NULL,AV_LOG_INFO,"[game-perf] ticks=%u repaints=%u wall_ms=%llu audio_poll_us=%llu video_us=%llu runtime_us=%llu max_tick_us=%llu\n",
        ticks,repaints,(unsigned long long)(elapsed/1000),(unsigned long long)(audio_total/ticks),
        (unsigned long long)(video_total/ticks),(unsigned long long)(runtime_total/ticks),(unsigned long long)max_tick);
    int n=art3m1s_runtime_profiler_snapshot(runtime,snapshot,sizeof(snapshot));
    if(n>0&&(size_t)n<=sizeof(snapshot)){
        char header[512];int size=snprintf(header,sizeof(header),
            "{\"build\":\"%s %s\",\"game\":\"%s\",\"uptime_ms\":%llu,\"host\":{\"ticks\":%u,\"repaints\":%u,\"wall_ms\":%llu,\"audio_poll_us\":%llu,\"video_us\":%llu,\"runtime_us\":%llu,\"max_tick_us\":%llu},\"core\":",
            __DATE__,__TIME__,game_id,(unsigned long long)(now/1000),ticks,repaints,(unsigned long long)(elapsed/1000),
            (unsigned long long)(audio_total/ticks),(unsigned long long)(video_total/ticks),
            (unsigned long long)(runtime_total/ticks),(unsigned long long)max_tick);
        if(size>0&&(size_t)size<sizeof(header)){
            int fd=sceIoOpen("ux0:data/art3m1s/perf.log",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_APPEND,0666);
            if(fd>=0){sceIoWrite(fd,header,size);sceIoWrite(fd,snapshot,n);sceIoWrite(fd,"}\n",2);sceIoClose(fd);}
        }
    }else av_log(NULL,AV_LOG_WARNING,"[game-perf] snapshot unavailable size=%d\n",n);
    since=now;ticks=repaints=0;audio_total=video_total=runtime_total=max_tick=0;
}
