#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include "files.h"
#include "audio.h"
#include "video.h"
#include "launcher.h"
#include "game_profile.h"
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <vitaGL.h>
#include <libavutil/log.h>
#include <stdarg.h>

/* Open/close each record so a device crash retains the last completed stage. */
static void media_log(void *context, int level, const char *fmt, va_list args) {
    if (level > AV_LOG_INFO) return;
    char line[1024];
    vsnprintf(line, sizeof(line), fmt, args);
    sceClibPrintf("[ffmpeg] %s", line);
    SceUID fd = sceIoOpen("ux0:data/art3m1s/media.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) { sceIoWrite(fd, line, strlen(line)); sceIoClose(fd); }
}

unsigned int _newlib_heap_size_user = 192 * 1024 * 1024;
extern void art3m1s_register_log_callback(void (*cb)(const char *, const char *));
extern void *art3m1s_runtime_create(uint32_t, uint32_t, int);
extern void art3m1s_runtime_destroy(void *);
extern int art3m1s_runtime_set_external_surface(void *, int, void *, int, int);
extern int art3m1s_runtime_advance_and_present(void *, uint32_t);
extern void art3m1s_register_file_reader(int (*)(const char *,uint8_t *,int,int64_t));
extern void art3m1s_register_file_writer(int (*)(const char *,const uint8_t *,int));
extern void art3m1s_register_file_delete(int (*)(const char *));
extern int art3m1s_runtime_load_project_bytes(void *,const uint8_t *,size_t,const char *);
extern void art3m1s_runtime_feed_key(void *,uint32_t,int);
extern void art3m1s_runtime_feed_mouse(void *,int,int);
extern void art3m1s_runtime_feed_mouse_button(void *,uint32_t,int);
extern void art3m1s_runtime_feed_touch(void *,uint32_t,uint8_t,int,int);
extern uint32_t art3m1s_runtime_stage_width(void *);
extern uint32_t art3m1s_runtime_stage_height(void *);
extern int art3m1s_runtime_is_exit_requested(void *);
extern void art3m1s_register_media_command_callback(void (*)(const char *,const char *));

static FILE *log_file;
static void log_message(const char *level, const char *msg) {
    sceClibPrintf("[art3m1s:%s] %s\n", level, msg);
    if (log_file) { fprintf(log_file, "[%s] %s\n", level, msg); fflush(log_file); }
    printf("[%s] %s\n", level, msg);
}
int main(void) {
    sceIoMkdir("ux0:data/art3m1s", 0777);
    av_log_set_callback(media_log);
    av_log(NULL,AV_LOG_INFO,"\n[build] %s %s video_hw=%d audio_hw=%d\n",__DATE__,__TIME__,
#ifdef ART3M1S_EXPERIMENTAL_VITA_HW
        1,
#else
        0,
#endif
#ifdef ART3M1S_EXPERIMENTAL_VITA_AUDIO
        1
#else
        0
#endif
    );
    log_file = fopen("ux0:data/art3m1s/host.log", "w");
    log_message("host", "initializing vitaGL");
    // Extended's fourth argument reserves USER RAM only. The codec allocates
    // CDRAM directly, outside vitaGL's pools, so it needs a separate threshold.
    int codec_cdram_reserve=0;
#ifdef ART3M1S_EXPERIMENTAL_VITA_HW
    codec_cdram_reserve=32*1024*1024;
#endif
    SceKernelFreeMemorySizeInfo memory={0};memory.size=sizeof(memory);
    if(sceKernelGetFreeMemorySize(&memory)>=0)
        av_log(NULL,AV_LOG_INFO,"[memory] before GL user=%u cdram=%u phycont=%u codec_reserve=%d\n",memory.size_user,memory.size_cdram,memory.size_phycont,codec_cdram_reserve);
    // The return value reports resolution fallback, not initialization success.
    vglInitWithCustomThreshold(1024*1024,960,544,24*1024*1024,
        codec_cdram_reserve,0,0x8C6000 /* full CDialog budget: no pool */,SCE_GXM_MULTISAMPLE_NONE);
    if(sceKernelGetFreeMemorySize(&memory)>=0)
        av_log(NULL,AV_LOG_INFO,"[memory] after GL user=%u cdram=%u phycont=%u\n",memory.size_user,memory.size_cdram,memory.size_phycont);
    char selected[64]={0};
    if(!host_launcher_select(selected,sizeof(selected))){if(log_file)fclose(log_file);sceKernelExitProcess(0);return 0;}
    host_loading_show(0,NULL);
    art3m1s_register_log_callback(log_message);
    log_message("host", "creating Rust runtime");
    host_loading_show(1,NULL);
    void *runtime = art3m1s_runtime_create(960, 544, 1);
    if (runtime) {
        log_message("host", "runtime created");
        int result = art3m1s_runtime_set_external_surface(runtime, 4, NULL, 960, 544);
        log_message("host", result > 0 ? "surface configured" : "surface configuration failed");
        int loaded=0;
        char game_path[160],save_path[160];
        // Use the validated selection returned by the launcher. The preference
        // file is only for the next launch, not authoritative for this one.
        snprintf(game_path,sizeof(game_path),"ux0:data/art3m1s/games/%s",selected);
        snprintf(save_path,sizeof(save_path),"ux0:data/art3m1s/saves-%s",selected);
        log_message("game",selected);
        host_loading_show(2,NULL);
        if(host_files_open(game_path,save_path)>=0){
            art3m1s_register_file_reader(host_read);
            art3m1s_register_file_writer(host_write);
            art3m1s_register_file_delete(host_delete);
            if(host_audio_start()==0)art3m1s_register_media_command_callback(host_media_command);
            int size=host_read("system.ini",NULL,0,-1);
            host_loading_show(3,NULL);
            if(size>0){
                uint8_t *ini=malloc(size);
                if(ini && host_read("system.ini",ini,size,0)==size)
                    loaded=art3m1s_runtime_load_project_bytes(runtime,ini,size,"WINDOWS")==0;
                free(ini);
            }
        }
        log_message("host",loaded?"game loaded":"no game loaded; display probe only");
        host_loading_show(loaded?4:3,loaded?NULL:"Load failed - see host.log");
        if(!loaded)sceKernelDelayThread(3000000);
        host_loading_finish();
        sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
        sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT,SCE_TOUCH_SAMPLING_STATE_START);
        int stage_w=art3m1s_runtime_stage_width(runtime),stage_h=art3m1s_runtime_stage_height(runtime);
        SceTouchData old_touch={0};
#ifdef ART3M1S_PROFILE_GAME
        host_game_profile_start(runtime,selected);
#endif
        uint64_t previous_time=sceKernelGetProcessTimeWide();
        uint32_t previous=0; int mouse_x=480,mouse_y=270;
        for (int frame = 0; loaded || frame < 900; ++frame) {
            uint64_t frame_start=sceKernelGetProcessTimeWide();
            uint32_t delta_ms=(uint32_t)((frame_start-previous_time)/1000);
            previous_time=frame_start;
            if(delta_ms==0)delta_ms=1;
            SceCtrlData pad; sceCtrlPeekBufferPositive(0,&pad,1);
            uint32_t changed=previous^pad.buttons;
            if(changed & pad.buttons & (SCE_CTRL_CIRCLE|SCE_CTRL_CROSS|SCE_CTRL_START))host_video_skip(runtime);
            const uint32_t buttons[]={SCE_CTRL_CIRCLE,SCE_CTRL_CROSS,SCE_CTRL_START,SCE_CTRL_RTRIGGER,SCE_CTRL_UP,SCE_CTRL_DOWN};
            const uint32_t keys[]={13,27,13,17,38,40};
            for(int i=0;i<6;i++)if(changed&buttons[i])art3m1s_runtime_feed_key(runtime,keys[i],(pad.buttons&buttons[i])!=0);
            if(abs((int)pad.lx-128)>24)mouse_x+=((int)pad.lx-128)/20;
            if(abs((int)pad.ly-128)>24)mouse_y+=((int)pad.ly-128)/20;
            if(mouse_x<0)mouse_x=0;if(mouse_x>=stage_w)mouse_x=stage_w-1;
            if(mouse_y<0)mouse_y=0;if(mouse_y>=stage_h)mouse_y=stage_h-1;
            art3m1s_runtime_feed_mouse(runtime,mouse_x,mouse_y);
            previous=pad.buttons;
            SceTouchData touch={0}; sceTouchPeek(SCE_TOUCH_PORT_FRONT,&touch,1);
            // Windows game scripts use mouse handlers; mirror the primary touch.
            if(touch.reportNum){
                mouse_x=(int)touch.report[0].x*stage_w/1920;
                mouse_y=(int)touch.report[0].y*stage_h/1088;
                art3m1s_runtime_feed_mouse(runtime,mouse_x,mouse_y);
            }
            art3m1s_runtime_feed_mouse_button(runtime,1,touch.reportNum>0 || (pad.buttons&SCE_CTRL_SQUARE)!=0);
            for(unsigned i=0;i<touch.reportNum;i++){
                int found=0;
                for(unsigned j=0;j<old_touch.reportNum;j++)if(old_touch.report[j].id==touch.report[i].id)found=1;
                art3m1s_runtime_feed_touch(runtime,touch.report[i].id,found?1:0,
                    (int)touch.report[i].x*stage_w/1920,(int)touch.report[i].y*stage_h/1088);
            }
            for(unsigned i=0;i<old_touch.reportNum;i++){
                int found=0;
                for(unsigned j=0;j<touch.reportNum;j++)if(old_touch.report[i].id==touch.report[j].id)found=1;
                if(!found)art3m1s_runtime_feed_touch(runtime,old_touch.report[i].id,2,
                    (int)old_touch.report[i].x*stage_w/1920,(int)old_touch.report[i].y*stage_h/1088);
            }
            old_touch=touch;
#ifdef ART3M1S_PROFILE_GAME
            uint64_t audio_start=sceKernelGetProcessTimeWide();
#endif
            host_audio_poll(runtime);
#ifdef ART3M1S_PROFILE_GAME
            uint64_t video_start=sceKernelGetProcessTimeWide();
#endif
            host_video_tick(runtime);
#ifdef ART3M1S_PROFILE_GAME
            uint64_t runtime_start=sceKernelGetProcessTimeWide();
#endif
            int presented=art3m1s_runtime_advance_and_present(runtime, delta_ms);
            if(presented<0)break;
            if(!presented)host_video_present_idle();
#ifdef ART3M1S_PROFILE_GAME
            host_game_profile_tick(runtime,frame_start,video_start-audio_start,runtime_start-video_start,sceKernelGetProcessTimeWide()-runtime_start,presented);
#endif
            if(art3m1s_runtime_is_exit_requested(runtime))break;
            uint64_t elapsed=sceKernelGetProcessTimeWide()-frame_start;
            if(elapsed<16667)sceKernelDelayThread(16667-elapsed);
        }
        art3m1s_runtime_destroy(runtime);
        host_video_close();
        host_audio_stop();
        host_files_close();
    } else {log_message("error", "runtime creation failed");host_loading_show(1,"Runtime creation failed - see host.log");sceKernelDelayThread(3000000);host_loading_finish();}
    log_message("host", "probe complete");
    if (log_file) fclose(log_file);
    sceKernelExitProcess(0);
    return 0;
}
