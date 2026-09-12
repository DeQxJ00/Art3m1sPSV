#include "gpu.hpp"
#include "fallback_menu.hpp"
#include "font_settings_menu.hpp"
#include "cpu3_setting.hpp"
#include "clock_settings_menu.hpp"
#include "launcher_settings_menu.hpp"
#include "loading_ps_guard.hpp"
#include <psp2/shellutil.h>
#include "diagnostic_io.hpp"
#include "log_queue.hpp"
#include "game_library.hpp"
#include "media_gxm.hpp"
#include "runtime_api.h"
extern "C" {
#include "files.h"
#include "cpu_affinity.h"
#include "video.h"
#include <libavutil/log.h>
}
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/power.h>
#include <psp2/apputil.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <pthread.h>
#include <memory>
#include <string>
#ifdef DIRECT_HEAP_DIAGNOSTICS
#include <malloc.h>
#endif

extern "C" { unsigned int _newlib_heap_size_user=320*1024*1024;
void art3m1s_gxm_finish_host_frame();void art3m1s_gxm_reset_readback();
#ifdef ART3M1S_HOST_CPU3
void art3m1s_register_worker_init_callback(void (*)(const char*));
#endif
int art3m1s_runtime_prepare_gxm_textures(void*);
int art3m1s_runtime_set_message_font_sizes(void*,int,uint32_t,uint32_t);
void art3m1s_runtime_set_profiler_enabled(const void*,int);
#ifdef DIRECT_SEMANTIC_CONTROLS
uint32_t art3m1s_runtime_host_action_key(const void*,uint32_t);
int art3m1s_runtime_has_native_host_menu(const void*);
int art3m1s_runtime_host_menu_context(const void*);
#endif
#ifdef DIRECT_TEXT_EPOCH_CANDIDATE
void art3m1s_runtime_set_text_epoch_enabled(void*,int);
#endif
#ifdef DIRECT_SCENE_ORDER_CANDIDATE
void art3m1s_runtime_set_scene_order_cache_enabled(void*,int);
#endif
#if defined(DIRECT_MESSAGE_CANDIDATE) || defined(DIRECT_MESSAGE_INPUT_CANDIDATE)
void art3m1s_runtime_set_message_cache_enabled(void*,int);
#endif
#ifdef DIRECT_HISTORY_CANDIDATE
void art3m1s_runtime_set_history_cache_enabled(void*,int);
#endif
#ifdef DIRECT_KEYLESS_CANDIDATE
void art3m1s_runtime_set_gxm_keyless_enabled(void*,int);
#endif
#ifdef DIRECT_TEXT_LAYOUT_CANDIDATE
void art3m1s_runtime_set_text_layout_cache_enabled(void*,int);
#ifdef DIRECT_TEXT_COMMAND_CANDIDATE
void art3m1s_runtime_set_text_command_cache_enabled(void*,int);
#endif
#endif
int art3m1s_runtime_profiler_snapshot(const void*,uint8_t*,uint32_t); }
namespace {
FILE* output=nullptr;
const char* clockSettingsPath="ux0:data/art3m1s-gxm/cpu-clock.conf";
direct::ClockPolicy cpuClock{scePowerGetArmClockFrequency,scePowerSetArmClockFrequency};
direct::ClockPolicy es4Clock{scePowerGetGpuClockFrequency,scePowerSetGpuClockFrequency};
direct::ClockSettings current_clock_settings(){return {cpuClock.settings.global,cpuClock.settings.ogv,es4Clock.settings.global,es4Clock.settings.ogv};}
void configure_clocks(direct::ClockSettings value){
    cpuClock.configure({value.global,value.ogv});es4Clock.configure({value.es4Global,value.es4Ogv});
}
void report_cpu_clock(const char* reason){
    direct::log("[cpu-clock] reason=%s global=%d ogv=%d video=%d requested=%d actual=%d result=%08x baseline=%d",
        reason,cpuClock.settings.global,cpuClock.settings.ogv,int(cpuClock.video),cpuClock.requested,
        cpuClock.actual,unsigned(cpuClock.result),cpuClock.baseline);
    direct::log("[es4-clock] reason=%s global=%d ogv=%d video=%d requested=%d actual=%d result=%08x baseline=%d",
        reason,es4Clock.settings.global,es4Clock.settings.ogv,int(es4Clock.video),es4Clock.requested,
        es4Clock.actual,unsigned(es4Clock.result),es4Clock.baseline);
}
LogQueue logQueue;
void log_sink(const char* data,size_t size,bool flush){
    if(flush){if(output)std::fflush(output);direct::refresh_diagnostic_gates();}
    else if(output)std::fwrite(data,1,size,output);
}
void flush_log(){logQueue.flush();}
void report_log_timing(){
    auto t=logQueue.stats();
    direct::log("[log-async] queued=%u dropped=%llu truncated=%llu max_write_us=%llu max_flush_refresh_us=%llu; lifetime worker I/O incl. gate refresh, no producer storage waits",
        t.queued,(unsigned long long)t.dropped,(unsigned long long)t.truncated,
        (unsigned long long)t.maxWrite,(unsigned long long)t.maxFlush);
}
std::atomic<int> archiveDone{0},archiveTotal{0};
void media_log(void* c,int level,const char* format,va_list args){
    if(level>av_log_get_level())return;
    // Keep FFmpeg chatter filtered, but retain the host diagnostics needed to
    // distinguish finished speech from continuing decoder/render workload.
    bool videoTrace=false;
#ifdef DIRECT_DEFERRED_FINISH_CANDIDATE
    videoTrace=std::strncmp(format,"[video",6)==0;
#endif
    if(level>AV_LOG_WARNING&&!videoTrace&&std::strncmp(format,"[audio",6)&&std::strncmp(format,"[thread-perf]",13)&&std::strncmp(format,"[cpu3-",6))return;
    char line[4096];thread_local int prefix=1;av_log_format_line(c,level,format,args,line,sizeof(line),&prefix);direct::log("[media] %s",line);}
void core_log(const char* level,const char* text){direct::log("[core:%s] %s",level?level:"?",text?text:"");}
std::vector<uint8_t> read_ini(const std::string& path){
    std::vector<uint8_t> bytes;FILE* f=std::fopen(path.c_str(),"rb");
    if(f){std::fseek(f,0,SEEK_END);long n=std::ftell(f);std::rewind(f);if(n>0&&n<16*1024*1024){bytes.resize(n);if(std::fread(bytes.data(),1,n,f)!=size_t(n))bytes.clear();}std::fclose(f);}
    if(bytes.empty()){int n=host_read("system.ini",nullptr,0,-1);if(n>0&&n<16*1024*1024){bytes.resize(n);if(host_read("system.ini",bytes.data(),n,0)!=n)bytes.clear();}}return bytes;
}
const std::string fontSettingsDirectory=std::string(art3m1s::kDataRoot)+"/font-settings";
int shellEventsResult=-1;
int lock_loading_ps(){return shellEventsResult<0?shellEventsResult:sceShellUtilLock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN);}
int unlock_loading_ps(){return sceShellUtilUnlock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN);}
void report_loading_ps(const char* action,int result){direct::log("[loading-ps] action=%s result=%08x at_us=%llu",action,unsigned(result),(unsigned long long)sceKernelGetProcessTimeWide());}
struct Game {
    direct::LoadingPsGuard loadingPs{lock_loading_ps,unlock_loading_ps,report_loading_ps};
    bool gameFrameDrawn=false,loadingDisplayDrained=false;
    void loading_frame_complete(uint64_t now){
        if(!loadingPs.active())return;
        if(!error.empty())loadingPs.finish("load-failed");
        else if(gameFrameDrawn&&!loadingDisplayDrained){
            // One-time drain: do not release PS while the first game frame is
            // merely queued behind a loading frame. Never wait here in gameplay.
            int r=sceGxmDisplayQueueFinish();loadingDisplayDrained=true;
            if(r<0)report_loading_ps("display-drain-failed",r);
            loadingPs.finish(r>=0?"first-game-frame":"display-failed");
        }
        loadingPs.poll(now);
    }
    direct::FontSettings fontSettings;direct::FontSettingsMenu fontMenu;bool fontMenuOpen=false,fontMenuStandalone=false;
    std::string settings_path() const { return direct::font_settings_path(fontSettingsDirectory,entry.id); }
    bool apply_font_settings(const direct::FontSettings& value){
        return art3m1s_runtime_set_message_font_sizes(runtime,int(value.enabled),value.name,value.dialogue)!=0;
    }
    art3m1s::GameEntry entry;void* runtime=nullptr;pthread_t worker{};bool joining=false,leaving=false;
    std::atomic<int> result{-999};int phase=0;std::string error;uint64_t last=0;uint32_t buttons=0;bool touched=false;
    int mouseX=480,mouseY=272;
    bool tracing=false,traceRequested=false;uint64_t traceAt=0,tracePollAt=0,logicMax=0,prepareMax=0;unsigned slowTicks=0;
#ifdef DIRECT_TEXT_EPOCH_CANDIDATE
    bool textEpoch=true;uint64_t textEpochPollAt=0;
    void update_text_epoch(uint64_t now,bool initial=false){
        if(!initial&&now-textEpochPollAt<1000000)return;
        textEpochPollAt=now;SceIoStat stat{};
        bool enabled=direct::diagnostic_stat("ux0:data/art3m1s-gxm/text-epoch.off",&stat)<0;
        if(!initial&&enabled==textEpoch)return;
        textEpoch=enabled;art3m1s_runtime_set_text_epoch_enabled(runtime,int(enabled));
        direct::log("[text-epoch-state] at_us=%llu enabled=%d arm=%d bus=%d gpu=%d xbar=%d; discard crossing windows",
            (unsigned long long)now,int(enabled),scePowerGetArmClockFrequency(),scePowerGetBusClockFrequency(),scePowerGetGpuClockFrequency(),scePowerGetGpuXbarClockFrequency());
    }
#endif
#ifdef DIRECT_SCENE_ORDER_CANDIDATE
    bool sceneOrderCache=true;uint64_t sceneOrderPollAt=0;
    void update_scene_order_cache(uint64_t now,bool initial=false){
        if(!initial&&now-sceneOrderPollAt<1000000)return;
        sceneOrderPollAt=now;SceIoStat stat{};
        bool enabled=direct::diagnostic_stat("ux0:data/art3m1s-gxm/scene-order-cache.off",&stat)<0;
        if(!initial&&enabled==sceneOrderCache)return;
        sceneOrderCache=enabled;art3m1s_runtime_set_scene_order_cache_enabled(runtime,int(enabled));
        direct::log("[scene-order-state] at_us=%llu enabled=%d arm=%d bus=%d gpu=%d xbar=%d; discard crossing windows",
            (unsigned long long)now,int(enabled),scePowerGetArmClockFrequency(),scePowerGetBusClockFrequency(),scePowerGetGpuClockFrequency(),scePowerGetGpuXbarClockFrequency());
    }
#endif
#if defined(DIRECT_MESSAGE_CANDIDATE) || defined(DIRECT_MESSAGE_INPUT_CANDIDATE)
    bool messageCache=true;uint64_t messagePollAt=0;
    void update_message_cache(uint64_t now,bool initial=false){
        if(!initial&&now-messagePollAt<1000000)return;
        messagePollAt=now;SceIoStat stat{};
        bool enabled=direct::diagnostic_stat("ux0:data/art3m1s-gxm/message-cache.off",&stat)<0;
        if(!initial&&enabled==messageCache)return;
        messageCache=enabled;art3m1s_runtime_set_message_cache_enabled(runtime,int(enabled));
        direct::log("[message-state] at_us=%llu enabled=%d arm=%d bus=%d gpu=%d xbar=%d; discard crossing windows",
            (unsigned long long)now,int(enabled),scePowerGetArmClockFrequency(),scePowerGetBusClockFrequency(),scePowerGetGpuClockFrequency(),scePowerGetGpuXbarClockFrequency());
    }
#endif
#ifdef DIRECT_HISTORY_CANDIDATE
    bool historyCache=true;uint64_t historyPollAt=0;
    void update_history_cache(uint64_t now,bool initial=false){
        if(!initial&&now-historyPollAt<1000000)return;
        historyPollAt=now;SceIoStat stat{};
        bool enabled=direct::diagnostic_stat("ux0:data/art3m1s-gxm/history-cache.off",&stat)<0;
        if(!initial&&enabled==historyCache)return;
        historyCache=enabled;art3m1s_runtime_set_history_cache_enabled(runtime,int(enabled));
        direct::log("[history-state] at_us=%llu enabled=%d arm=%d bus=%d gpu=%d xbar=%d; discard crossing windows",
            (unsigned long long)now,int(enabled),scePowerGetArmClockFrequency(),scePowerGetBusClockFrequency(),scePowerGetGpuClockFrequency(),scePowerGetGpuXbarClockFrequency());
    }
#endif
#ifdef DIRECT_DEFERRED_FINISH_CANDIDATE
    bool deferredFinish=false;uint64_t deferredPollAt=0;
    void update_deferred_finish(uint64_t now,bool initial=false){
        if(!initial&&now-deferredPollAt<1000000)return;
        deferredPollAt=now;SceIoStat stat{};
        bool enabled=direct::diagnostic_stat("ux0:data/art3m1s-gxm/gxm-deferred-finish.off",&stat)<0;
        if(!initial&&enabled==deferredFinish)return;
        // Called on the owner thread before begin; disabling drains pending work.
        if(!direct::set_deferred_finish(enabled)){direct::log("deferred wait gate refused in active scene");return;}
        deferredFinish=enabled;
        direct::log("[deferred-state] at_us=%llu enabled=%d arm=%d bus=%d gpu=%d xbar=%d; all resource guards remain active",
            (unsigned long long)now,int(enabled),scePowerGetArmClockFrequency(),scePowerGetBusClockFrequency(),scePowerGetGpuClockFrequency(),scePowerGetGpuXbarClockFrequency());
    }
#endif
#ifdef DIRECT_TEXT_LAYOUT_CANDIDATE
#ifdef DIRECT_TEXT_COMMAND_CANDIDATE
#ifdef DIRECT_KEYLESS_CANDIDATE
    bool keyless=true;uint64_t keylessPollAt=0;
    void update_keyless(uint64_t now,bool initial=false){
        if(!initial&&now-keylessPollAt<1000000)return;
        keylessPollAt=now;SceIoStat stat{};
        bool enabled=direct::diagnostic_stat("ux0:data/art3m1s-gxm/gxm-keyless.off",&stat)<0;
        if(!initial&&enabled==keyless)return;
        keyless=enabled;art3m1s_runtime_set_gxm_keyless_enabled(runtime,int(enabled));
        direct::log("[keyless-state] at_us=%llu enabled=%d arm=%d bus=%d gpu=%d xbar=%d; discard crossing windows",
            (unsigned long long)now,int(enabled),scePowerGetArmClockFrequency(),scePowerGetBusClockFrequency(),scePowerGetGpuClockFrequency(),scePowerGetGpuXbarClockFrequency());
    }
#endif
    bool commandCache=true;uint64_t commandPollAt=0;
    void update_command_cache(uint64_t now,bool initial=false){
        if(!initial&&now-commandPollAt<1000000)return;
        commandPollAt=now;SceIoStat stat{};
        bool enabled=direct::diagnostic_stat("ux0:data/art3m1s-gxm/text-command-cache.off",&stat)<0;
        if(!initial&&enabled==commandCache)return;
        commandCache=enabled;art3m1s_runtime_set_text_command_cache_enabled(runtime,int(enabled));
        direct::log("[command-cache-state] at_us=%llu enabled=%d arm=%d bus=%d gpu=%d xbar=%d; discard crossing windows",
            (unsigned long long)now,int(enabled),scePowerGetArmClockFrequency(),scePowerGetBusClockFrequency(),scePowerGetGpuClockFrequency(),scePowerGetGpuXbarClockFrequency());
    }
#endif
    bool layoutCache=true;uint64_t layoutPollAt=0;
    void update_layout_cache(uint64_t now,bool initial=false){
        if(!initial&&now-layoutPollAt<1000000)return;
        layoutPollAt=now;SceIoStat stat{};
        bool enabled=direct::diagnostic_stat("ux0:data/art3m1s-gxm/text-layout-cache.off",&stat)<0;
        if(!initial&&enabled==layoutCache)return;
        layoutCache=enabled;art3m1s_runtime_set_text_layout_cache_enabled(runtime,int(enabled));
        direct::log("[layout-cache-state] at_us=%llu enabled=%d arm=%d bus=%d gpu=%d xbar=%d; discard crossing windows",
            (unsigned long long)now,int(enabled),scePowerGetArmClockFrequency(),scePowerGetBusClockFrequency(),scePowerGetGpuClockFrequency(),scePowerGetGpuXbarClockFrequency());
    }
#endif
    explicit Game(art3m1s::GameEntry e):entry(std::move(e)){loadingPs.begin(sceKernelGetProcessTimeWide());archiveDone=0;archiveTotal=0;art3m1s_gxm_reset_readback();}
    static void* load(void* p){host_background_thread_enter("archive-loader");auto* g=static_cast<Game*>(p);std::string save=std::string(art3m1s::kDataRoot)+"/saves/"+g->entry.id;
        sceIoMkdir((std::string(art3m1s::kDataRoot)+"/saves").c_str(),0777);g->result=host_files_open(g->entry.path.c_str(),save.c_str());archiveDone=archiveTotal.load();return nullptr;}
    ~Game(){loadingPs.finish("game-destroy");loadingPs.poll(sceKernelGetProcessTimeWide());if(joining)pthread_join(worker,nullptr);art3m1s_gxm_reset_readback();gxm_media_detach();gxm_media_pump();direct::wait();
#ifdef DIRECT_DEFERRED_FINISH_CANDIDATE
        direct::set_deferred_finish(false); // Return launcher rendering to end waits.
#endif
        if(runtime)art3m1s_runtime_destroy(runtime);direct::menu_release();direct::fallback_menu_release();direct::log("game resources released");}
    void boot(){
        art3m1s_register_log_callback(core_log);art3m1s_register_file_reader(host_read);art3m1s_register_file_writer(host_write);art3m1s_register_file_delete(host_delete);
        runtime=art3m1s_runtime_create(960,544,5);if(!runtime){error="无法创建运行时";return;}
        gxm_media_attach(runtime);art3m1s_register_media_command_callback(gxm_media_command);auto ini=read_ini(entry.path+"/system.ini");
        // A native Vita package can ship Windows tables but no Windows art.
        // Keep the established port default; opt native packages in per game.
        const char* platform="WINDOWS";
        if(FILE* f=std::fopen((entry.path+"/platform.txt").c_str(),"r")){
            char token[16]{};int count=std::fscanf(f,"%15s",token);std::fclose(f);
            if(count==1&&(!std::strcmp(token,"VITA")||!std::strcmp(token,"vita")))platform="VITA";
        }
        direct::log("[game-platform] id=%s platform=%s",entry.id.c_str(),platform);
        if(ini.empty()||art3m1s_runtime_load_project_bytes(runtime,ini.data(),ini.size(),platform)!=0){error="加载游戏失败";return;}
        fontSettings=direct::load_font_settings(settings_path());
        if(!apply_font_settings(fontSettings))direct::log("[font-settings] initial application failed id=%s",entry.id.c_str());
        SceIoStat traceStat{};traceRequested=direct::diagnostic_stat("ux0:data/art3m1s-gxm/trace-nextline.flag",&traceStat)>=0;
        update_trace(sceKernelGetProcessTimeWide(),true);
#ifdef DIRECT_SCENE_ORDER_CANDIDATE
        update_scene_order_cache(sceKernelGetProcessTimeWide(),true);
#endif
#if defined(DIRECT_MESSAGE_CANDIDATE) || defined(DIRECT_MESSAGE_INPUT_CANDIDATE)
        update_message_cache(sceKernelGetProcessTimeWide(),true);
#endif
#ifdef DIRECT_TEXT_EPOCH_CANDIDATE
        update_text_epoch(sceKernelGetProcessTimeWide(),true);
#endif
#ifdef DIRECT_HISTORY_CANDIDATE
        update_history_cache(sceKernelGetProcessTimeWide(),true);
#endif
#ifdef DIRECT_DEFERRED_FINISH_CANDIDATE
        update_deferred_finish(sceKernelGetProcessTimeWide(),true);
#endif
#ifdef DIRECT_TEXT_LAYOUT_CANDIDATE
        update_layout_cache(sceKernelGetProcessTimeWide(),true);
#ifdef DIRECT_TEXT_COMMAND_CANDIDATE
        update_command_cache(sceKernelGetProcessTimeWide(),true);
#ifdef DIRECT_KEYLESS_CANDIDATE
        update_keyless(sceKernelGetProcessTimeWide(),true);
#endif
#endif
#endif
        direct::menu_release();traceAt=last=sceKernelGetProcessTimeWide();phase=4;
        direct::log("game loaded: %s stage=%ux%u",entry.id.c_str(),art3m1s_runtime_stage_width(runtime),art3m1s_runtime_stage_height(runtime));
    }
    void update_trace(uint64_t now,bool initial=false){
        if(!initial&&(!traceRequested||now-tracePollAt<1000000))return;
        tracePollAt=now;SceIoStat stat{};
        bool enabled=traceRequested&&direct::diagnostic_stat("ux0:data/art3m1s-gxm/trace-nextline.off",&stat)<0;
        if(!initial&&enabled==tracing)return;
        tracing=enabled;art3m1s_runtime_set_profiler_enabled(runtime,tracing?1:0);
        traceAt=now;logicMax=prepareMax=slowTicks=0;
        direct::log("[profile-state] at_us=%llu enabled=%d arm=%d bus=%d gpu=%d xbar=%d; discard frame windows crossing this marker",
            (unsigned long long)now,int(tracing),scePowerGetArmClockFrequency(),scePowerGetBusClockFrequency(),scePowerGetGpuClockFrequency(),scePowerGetGpuXbarClockFrequency());
    }
#ifdef DIRECT_SEMANTIC_CONTROLS
    bool hostMenu=false;int hostMenuItem=0;uint32_t menuPulse=0;
    uint32_t mappedKeys[9]{};
    uint64_t hostMenuOpenedAt=0;
    const uint32_t menuActions[8]={5,6,3,4,7,2,1,0};
    uint32_t menuKeys[8]{};
    void close_host_menu(){fontMenuOpen=false;hostMenu=false;hostMenuOpenedAt=0;direct::fallback_menu_release();last=sceKernelGetProcessTimeWide();}
    void host_menu_presented(){if(hostMenuOpenedAt){direct::log("[host-menu] first_frame_us=%llu; input observed to frame completion, includes waits",
        (unsigned long long)(sceKernelGetProcessTimeWide()-hostMenuOpenedAt));hostMenuOpenedAt=0;}}
    void host_menu_input(const SceCtrlData& pad,const SceTouchData& touch){
        uint32_t pressed=pad.buttons&~buttons;bool tap=touch.reportNum&&!touched;
        buttons=pad.buttons;touched=touch.reportNum>0;
        if(fontMenuOpen){
            int action=fontMenu.input(pressed,tap,touch);
            if(action<0){if(fontMenuStandalone)close_host_menu();else fontMenuOpen=false;return;}
            if(action>0){
                if(!apply_font_settings(fontMenu.value)){fontMenu.failed=true;return;}
                sceIoMkdir(fontSettingsDirectory.c_str(),0777);
                if(!direct::save_font_settings(settings_path(),fontMenu.value)){
                    apply_font_settings(fontSettings);fontMenu.failed=true;return;}
                fontSettings=fontMenu.value;
                direct::log("[font-settings] id=%s override=%d name=%u dialogue=%u",entry.id.c_str(),int(fontSettings.enabled),fontSettings.name,fontSettings.dialogue);
                close_host_menu();
            }return;
        }
        if(pressed&(SCE_CTRL_CROSS|SCE_CTRL_SQUARE)){close_host_menu();return;}
        if(pressed&SCE_CTRL_UP)hostMenuItem=(hostMenuItem+9)%10;
        if(pressed&SCE_CTRL_DOWN)hostMenuItem=(hostMenuItem+1)%10;
        bool choose=pressed&SCE_CTRL_CIRCLE;
        if(tap){int x=touch.report[0].x/2,y=touch.report[0].y/2;
            if(x>=280&&x<680&&y>=85&&y<445){hostMenuItem=(y-85)/36;choose=true;}}
        if(!choose)return;
        if(hostMenuItem==7){fontMenu={fontSettings};fontMenuOpen=true;fontMenuStandalone=false;return;}
        if(hostMenuItem==8){close_host_menu();return;}
        if(hostMenuItem==9){
            direct::log("[host-menu] exit_game id=%s destination=game-selector",entry.id.c_str());
            close_host_menu();leaving=true;return;
        }
        uint32_t key=menuKeys[hostMenuItem];if(!key)return;
        close_host_menu();menuPulse=key;art3m1s_runtime_feed_key(runtime,key,1);
        direct::log("[host-menu] action=%u key=%u",menuActions[hostMenuItem],key);
    }
#endif
    void input(const SceCtrlData& pad,const SceTouchData& touch){
#ifdef DIRECT_SEMANTIC_CONTROLS
        if(menuPulse){art3m1s_runtime_feed_key(runtime,menuPulse,0);menuPulse=0;}
        if((pad.buttons&~buttons&SCE_CTRL_SQUARE)&&art3m1s_runtime_host_menu_context(runtime)
            &&(!art3m1s_runtime_has_native_host_menu(runtime)||(pad.buttons&SCE_CTRL_LTRIGGER))){
            hostMenuOpenedAt=sceKernelGetProcessTimeWide();
            for(auto& key:mappedKeys){if(key)art3m1s_runtime_feed_key(runtime,key,0);key=0;}
            art3m1s_runtime_feed_mouse_button(runtime,1,0);
            for(unsigned i=0;i<7;i++)menuKeys[i]=art3m1s_runtime_host_action_key(runtime,menuActions[i]);
            fontMenuOpen=fontMenuStandalone=(pad.buttons&SCE_CTRL_LTRIGGER)!=0;if(fontMenuOpen)fontMenu={fontSettings};
            hostMenu=true;hostMenuItem=0;buttons=pad.buttons;touched=touch.reportNum>0;
            direct::log("[host-menu] fallback opened");return;
        }
#endif
        uint32_t changed=buttons^pad.buttons;if(changed&pad.buttons&(SCE_CTRL_CROSS|SCE_CTRL_CIRCLE|SCE_CTRL_START))gxm_media_skip();
        // Low key codes also work with scripts that cap input at 226.
        // 225 uses the core's guarded alias to the script's native MENU binding.
        struct Key{uint32_t b,k;};const Key keys[]={
            {SCE_CTRL_CIRCLE|SCE_CTRL_START,13},{SCE_CTRL_CROSS,27},
            {SCE_CTRL_TRIANGLE|SCE_CTRL_UP,38},{SCE_CTRL_SQUARE,225},{SCE_CTRL_SELECT,113},
            {SCE_CTRL_RTRIGGER,17},{SCE_CTRL_LEFT,37},
            {SCE_CTRL_RIGHT,39},{SCE_CTRL_DOWN,40}};
        for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);++i){auto key=keys[i];
            bool down=(pad.buttons&key.b)!=0,wasDown=(buttons&key.b)!=0;
            if(down==wasDown)continue;
#ifdef DIRECT_SEMANTIC_CONTROLS
            if(down){
                // Select is a semantic action; other keys retain UI navigation.
                if(key.b==SCE_CTRL_SELECT)key.k=art3m1s_runtime_host_menu_context(runtime)?art3m1s_runtime_host_action_key(runtime,1):0;
                if(key.b==SCE_CTRL_SQUARE)key.k=art3m1s_runtime_host_menu_context(runtime)&&art3m1s_runtime_has_native_host_menu(runtime)?art3m1s_runtime_host_action_key(runtime,0):0;
                mappedKeys[i]=key.k;
            }else{key.k=mappedKeys[i];mappedKeys[i]=0;}
#endif
            if(key.k)art3m1s_runtime_feed_key(runtime,key.k,down);
        }
        if(std::abs(int(pad.lx)-128)>24)mouseX+=(int(pad.lx)-128)/20;if(std::abs(int(pad.ly)-128)>24)mouseY+=(int(pad.ly)-128)/20;
        int w=art3m1s_runtime_stage_width(runtime),h=art3m1s_runtime_stage_height(runtime);
        mouseX=std::clamp(mouseX,0,std::max(w-1,0));mouseY=std::clamp(mouseY,0,std::max(h-1,0));
        if(touch.reportNum){mouseX=int(touch.report[0].x)*w/1920;mouseY=int(touch.report[0].y)*h/1088;if(!touched)gxm_media_skip();}
        if(tracing&&(changed||touched!=(touch.reportNum>0)))
            direct::log("[input-trace] buttons=%08x touch=%u mouse=%d,%d",unsigned(pad.buttons),unsigned(touch.reportNum),mouseX,mouseY);
        art3m1s_runtime_feed_mouse(runtime,mouseX,mouseY);art3m1s_runtime_feed_mouse_button(runtime,1,touch.reportNum>0);
        buttons=pad.buttons;touched=touch.reportNum>0;
    }
    void tick(const SceCtrlData& pad,const SceTouchData& touch){
        if(!error.empty()){if(pad.buttons&SCE_CTRL_CROSS)leaving=true;return;}
        if(phase==0){phase=1;if(pthread_create(&worker,nullptr,load,this))error="无法启动资源读取线程";else joining=true;return;}
        if(phase==1){if(result.load()!=-999){pthread_join(worker,nullptr);joining=false;if(result<0)error="无法打开游戏目录";else phase=2;}return;}
        if(phase==2){phase=3;return;}if(phase==3){boot();buttons=pad.buttons;touched=touch.reportNum>0;return;}
#ifdef DIRECT_SEMANTIC_CONTROLS
        if(hostMenu){host_menu_input(pad,touch);last=sceKernelGetProcessTimeWide();return;}
#endif
        uint64_t now=sceKernelGetProcessTimeWide();update_trace(now);
#ifdef DIRECT_SCENE_ORDER_CANDIDATE
        update_scene_order_cache(now);
#endif
#if defined(DIRECT_MESSAGE_CANDIDATE) || defined(DIRECT_MESSAGE_INPUT_CANDIDATE)
        update_message_cache(now);
#endif
#ifdef DIRECT_TEXT_EPOCH_CANDIDATE
        update_text_epoch(now);
#endif
#ifdef DIRECT_HISTORY_CANDIDATE
        update_history_cache(now);
#endif
#ifdef DIRECT_DEFERRED_FINISH_CANDIDATE
        update_deferred_finish(now);
#endif
#ifdef DIRECT_TEXT_LAYOUT_CANDIDATE
        update_layout_cache(now);
#ifdef DIRECT_TEXT_COMMAND_CANDIDATE
        update_command_cache(now);
#ifdef DIRECT_KEYLESS_CANDIDATE
        update_keyless(now);
#endif
#endif
#endif
        uint32_t delta=std::clamp(uint32_t((now-last)/1000),1u,100u);last=now;
        art3m1s_runtime_advance_without_render(runtime,delta);
        uint64_t logicDone=tracing?sceKernelGetProcessTimeWide():0;
        art3m1s_runtime_prepare_gxm_textures(runtime);
        // Preserve the previous host's ordering: consume last frame's input in
        // advance, then collect input for the next frame before presenting.
        input(pad,touch);
        if(tracing){uint64_t done=sceKernelGetProcessTimeWide();logicMax=std::max(logicMax,logicDone-now);prepareMax=std::max(prepareMax,done-logicDone);
            if(done-now>16667)++slowTicks;
            if(done-traceAt>=5000000){direct::log("[nextline-trace] logic_max_us=%llu prepare_input_max_us=%llu tick_over16ms=%u",(unsigned long long)logicMax,(unsigned long long)prepareMax,slowTicks);
                static uint8_t snapshot[32768];int n=art3m1s_runtime_profiler_snapshot(runtime,snapshot,sizeof(snapshot)-1);
                if(n>0&&n<int(sizeof(snapshot))){snapshot[n]=0;direct::log("[nextline-core] %s",snapshot);}
                traceAt=sceKernelGetProcessTimeWide();logicMax=prepareMax=slowTicks=0;}}
        if(art3m1s_runtime_is_exit_requested(runtime))leaving=true;
    }
    void prepare(){
#ifdef DIRECT_SEMANTIC_CONTROLS
        if(hostMenu){if(!direct::fallback_menu_prepare()){
            direct::log("[host-menu] atlas unavailable; closing fallback menu");close_host_menu();}return;}
#endif
        if(phase==4&&error.empty())return;
        direct::menu_prepare("正在加载游戏  正在读取资源  正在初始化引擎  × 返回",24);direct::menu_prepare(entry.title.c_str(),22);direct::menu_prepare(error.c_str(),24);}
    void draw(){
        gameFrameDrawn=false;
#ifdef DIRECT_SEMANTIC_CONTROLS
        if(hostMenu&&fontMenuOpen){fontMenu.draw();return;}
        if(hostMenu){direct::rect(0,0,960,544,0x101b2bff);direct::fallback_menu_text(280,57,direct::FallbackLabel::Title);
            for(int i=0;i<10;i++){float y=85+i*36;direct::rect(280,y,400,32,i==hostMenuItem?0x286482ff:0x1c2838ff);
                direct::fallback_menu_text(300,y+26,i==7?direct::FallbackLabel::FontEntry:i==8?direct::FallbackLabel::Return:i==9?direct::FallbackLabel::ExitGame:direct::FallbackLabel(unsigned(direct::FallbackLabel::Save)+i),i>=7||menuKeys[i]?0xffffffff:0x8895a5ff);}
            direct::fallback_menu_text(280,495,direct::FallbackLabel::Help);return;}
#endif
        if(phase==4&&error.empty()){
            const unsigned before=loadingPs.active()?direct::last_frame_stats().quads:0;
            const bool rendered=art3m1s_runtime_present_gxm(runtime)!=0;host_video_present_idle();
            gameFrameDrawn=loadingPs.active()&&rendered&&direct::last_frame_stats().quads>before;
            return;
        }
        direct::menu_text(48,110,24,error.empty()?"正在加载游戏":error.c_str());direct::menu_text(48,170,22,entry.title.c_str());
        if(!error.empty()){direct::menu_text(48,250,24,"× 返回");return;}
        direct::menu_text(48,225,24,phase>=2?"正在初始化引擎":"正在读取资源");
        float progress=phase>=2?.85f:(archiveTotal>0?.1f+.6f*archiveDone.load()/archiveTotal.load():.05f);
        direct::rect(48,260,864,14,0x293748ff);direct::rect(48,260,864*progress,14,0x50c3ebff);
    }
};
}
namespace direct { void log(const char* format,...){
    va_list args;va_start(args,format);logQueue.append(format,args);va_end(args);
} }
extern "C" void host_load_timing_log(const char* op,const char* path,uint64_t wait,uint64_t work,uint64_t ended,int result){
    direct::log("[load-block] op=%s path=%.180s tid=%d wait_us=%llu work_us=%llu at_us=%llu result=%d; nested stages overlap",
        op,path?path:"",sceKernelGetThreadId(),(unsigned long long)wait,(unsigned long long)work,(unsigned long long)ended,result);
}
extern "C" void host_loading_show(int stage,const char* detail){int done=0,total=0;if(stage==2&&detail&&std::sscanf(detail,"PFS %d / %d",&done,&total)==2){archiveTotal=total;archiveDone=std::max(done-1,0);}}
extern "C" void host_loading_finish(){}
extern "C" void host_clock_video_active(int active){
    bool changed=cpuClock.video_active(active!=0);
    changed=es4Clock.video_active(active!=0)||changed;
    if(changed)report_cpu_clock(active?"ogv-start":"ogv-stop");
}

int main(){
    sceIoMkdir(art3m1s::kDataRoot,0777);sceIoMkdir(art3m1s::kGamesRoot,0777);
    sceIoRemove("ux0:data/art3m1s-gxm/host.previous.log");sceIoRename("ux0:data/art3m1s-gxm/host.log","ux0:data/art3m1s-gxm/host.previous.log");
    output=std::fopen("ux0:data/art3m1s-gxm/host.log","w");if(output)std::setvbuf(output,nullptr,_IOFBF,32768);
    if(!logQueue.start(log_sink,[]()->uint64_t{return sceKernelGetProcessTimeWide();},
        [](){sceKernelChangeThreadPriority(0,180);})){if(output){std::fputs("log worker failed to start; exiting diagnostic build\n",output);std::fclose(output);output=nullptr;}return 1;}
    direct::log("[log-async] enabled slots=32 line_bytes=16384 priority=180; hot-animation LRU plus completed async surface lifecycle");
#if defined(DIRECT_BUILTIN_EFFECTS)
    direct::log("Direct GXM " DIRECT_APP_VERSION " builtin-v4 build %s %s; retained opaque group result; original sprite shader bytes preserved; effect invalidation and offscreen fences",__DATE__,__TIME__);
#elif defined(DIRECT_TEXT_EPOCH_CANDIDATE)
    direct::log("Direct GXM " DIRECT_APP_VERSION " optL-text-epoch build %s %s; renderer-owned backlog/metrics mutation cache; GPU unchanged",__DATE__,__TIME__);
#elif defined(DIRECT_REBUILD_AUDIT_CANDIDATE)
    direct::log("Direct GXM " DIRECT_APP_VERSION " optL-rebuild-audit build %s %s; message-input core with rebuild reason counters; GPU unchanged",__DATE__,__TIME__);
#elif defined(DIRECT_MESSAGE_INPUT_CANDIDATE)
    direct::log("Direct GXM " DIRECT_APP_VERSION " optL-message-input build %s %s; isolated exact message cache; full-cover GPU unchanged",__DATE__,__TIME__);
#elif defined(DIRECT_FULL_COVER_CANDIDATE)
    direct::log("Direct GXM " DIRECT_APP_VERSION " optL-full-cover build %s %s; visible-clip plus pending opaque cover culling; unchanged core, shaders, waits",__DATE__,__TIME__);
#elif defined(DIRECT_VISIBLE_CLIP_CANDIDATE)
    direct::log("Direct GXM " DIRECT_APP_VERSION " optL-visible-clip build %s %s; history core unchanged; target-visible clip redundancy only; unchanged shaders and GPU waits",__DATE__,__TIME__);
#ifdef DIRECT_DRAW_AUDIT
    direct::log("DRAW AUDIT: emulator geometry diagnostics; not hardware performance candidate");
#endif
#elif defined(DIRECT_REUSE_HISTORY_CANDIDATE)
    direct::log("Direct GXM " DIRECT_APP_VERSION " optL-reuse-history build %s %s; optL-reuse plus immutable history cache; unchanged shaders and GPU waits",__DATE__,__TIME__);
#elif defined(DIRECT_DRAWLIST_REUSE_CANDIDATE)
    direct::log("Direct GXM " DIRECT_APP_VERSION " optL-reuse candidate build %s %s; optL core plus CPU DrawList buffer reuse; unchanged shaders and GPU waits; hardware OOM mitigation under validation",__DATE__,__TIME__);
#elif defined(DIRECT_NUMERIC_TWEEN_CANDIDATE)
    direct::log("Direct GXM " DIRECT_APP_VERSION " optR numeric tween build %s %s; REBUILT current core, not pinned Opt2; optQ CPU plus numeric tween/reveal iteration, optK GPU/end waits and unchanged shaders",__DATE__,__TIME__);
#elif defined(DIRECT_SCENE_ORDER_CANDIDATE)
    direct::log("Direct GXM " DIRECT_APP_VERSION " optQ scene order cache build %s %s; REBUILT current core, not pinned Opt2; optP CPU, optK GPU/end waits and unchanged shaders; live cached/uncached traversal gate",__DATE__,__TIME__);
#elif defined(DIRECT_REBUILD_PROFILE_CANDIDATE)
    direct::log("Direct GXM " DIRECT_APP_VERSION " optP rebuild diagnostics build %s %s; REBUILT current core, not pinned Opt2; optO CPU, optK GPU/end waits and unchanged shaders; diagnostic only",__DATE__,__TIME__);
#elif defined(DIRECT_MESSAGE_CANDIDATE)
    direct::log("Direct GXM " DIRECT_APP_VERSION " optO message cache build %s %s; REBUILT current core, not pinned Opt2; optN timing, optK GPU/end waits and unchanged shaders; exact ordered/hash-map comparison gate",__DATE__,__TIME__);
#elif defined(DIRECT_TEXT_SYNC_PROFILE_CANDIDATE)
    direct::log("Direct GXM " DIRECT_APP_VERSION " optN text sync timings build %s %s; REBUILT current core, not pinned Opt2; optM history, optK GPU/end waits and unchanged shaders; diagnostic only",__DATE__,__TIME__);
#elif defined(DIRECT_HISTORY_CANDIDATE)
    direct::log("Direct GXM " DIRECT_APP_VERSION " optM history cache build %s %s; REBUILT current core, not pinned Opt2; optK GPU/end waits and unchanged shaders, live immutable-history/deep-compare gate",__DATE__,__TIME__);
#elif defined(DIRECT_DEFERRED_FINISH_CANDIDATE)
    direct::log("Direct GXM " DIRECT_APP_VERSION " optL guarded waits build %s %s; optK core archive (rebuilt, not pinned Opt2), optG audio; shaders unchanged; live end/begin wait comparison",__DATE__,__TIME__);
#elif defined(DIRECT_KEYLESS_CANDIDATE)
    direct::log("Direct GXM " DIRECT_APP_VERSION " optK keyless candidate build %s %s; REBUILT current core, not pinned Opt2; optJ text commands, optG audio and unchanged host GPU/shaders",__DATE__,__TIME__);
#elif defined(DIRECT_TEXT_COMMAND_CANDIDATE)
    direct::log("Direct GXM " DIRECT_APP_VERSION " optJ command cache candidate build %s %s; REBUILT current core, not pinned Opt2; optG audio, optE renderer and unchanged host shaders",__DATE__,__TIME__);
#elif defined(DIRECT_TEXT_LAYOUT_CANDIDATE)
    direct::log("Direct GXM " DIRECT_APP_VERSION " optI layout cache candidate build %s %s; REBUILT current core, not pinned Opt2; optG audio, optE renderer and unchanged host shaders",__DATE__,__TIME__);
#else
    direct::log("Direct GXM " DIRECT_APP_VERSION " optH profile gate build %s %s; optG audio and optE renderer, live diagnostic profiling control; pinned Opt2 core and unchanged shaders",__DATE__,__TIME__);
#endif
    flush_log();
    av_log_set_callback(media_log);av_log_set_level(AV_LOG_INFO);
    direct::ClockSettings clockSettings;
    const bool clockSettingsRead=direct::load_clock_settings(clockSettingsPath,clockSettings);
    configure_clocks(clockSettings);report_cpu_clock(clockSettingsRead?"startup":"config-read-failed");
#ifdef ART3M1S_HOST_CPU3
    host_cpu_affinity_init();
    art3m1s_register_worker_init_callback(host_background_thread_enter);
#endif
    SceAppUtilInitParam init{};SceAppUtilBootParam boot{};sceAppUtilInit(&init,&boot);
    shellEventsResult=sceShellUtilInitEvents(0);report_loading_ps("init",shellEventsResult);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT,SCE_TOUCH_SAMPLING_STATE_START);
#ifdef DIRECT_RESOURCE_LEDGER
    art3m1s_register_log_callback(core_log);
    direct::log("[resource-ledger] A1 observation version=%u owners=11; reserve is accounting only, no admission or eviction policy",art3m1s_resource_ledger_audio_version());
#endif
    if(!direct::init()){cpuClock.shutdown();es4Clock.shutdown();logQueue.stop();if(output){std::fclose(output);output=nullptr;}return 1;}
    // Local-base and overlay capabilities start disabled and are enabled by
    // pixel validation. Validate on every launch: a deployment-only .once flag
    // made ordinary restarts silently lose both optimizations.
    sceIoRemove("ux0:data/art3m1s-gxm/retained-probe.once"); // Consume legacy requests.
    const auto validationStarted=sceKernelGetProcessTimeWide();
    const bool retainedValidated=direct::retained_self_test();
    direct::shared_surface_self_test();
    direct::log("[render-capabilities] startup_validation=1 retained=%d local_base=%d overlay=%d elapsed_us=%llu; failed paths remain disabled",
        int(retainedValidated),int(direct::local_base_enabled()),int(direct::overlay_cache_enabled()),
        (unsigned long long)(sceKernelGetProcessTimeWide()-validationStarted));
    direct::log("[clock-readonly] arm=%d bus=%d gpu=%d xbar=%d MHz",
        scePowerGetArmClockFrequency(),scePowerGetBusClockFrequency(),scePowerGetGpuClockFrequency(),scePowerGetGpuXbarClockFrequency());
    auto games=art3m1s::scan_games();size_t selected=0;auto last=art3m1s::load_last_game();
    for(size_t i=0;i<games.size();i++)if(games[i].id==last)selected=i;
    direct::Cpu3Setting cpu3Setting;cpu3Setting.open();
    bool launcherFontOpen=false;direct::FontSettingsMenu launcherFontMenu;
    bool launcherClockOpen=false;direct::ClockSettingsMenu launcherClockMenu;
    bool launcherSettingsOpen=false;direct::LauncherSettingsMenu launcherSettingsMenu;
    std::unique_ptr<Game> game;uint32_t previous=0;bool previousTouch=false;uint64_t heartbeat=0;
    uint64_t mediaUs=0,logicUs=0,presentUs=0,captureUs=0,maxUs=0;unsigned samples=0,slowFrames=0;
    const char* title="art3m1s  /  Direct GXM";const char* help="○ 确认   × 退出   ↑↓ 选择   START 设置   □ 字号   游戏内 L+□ 字号";
    for(;;){
        const uint64_t t0=sceKernelGetProcessTimeWide();
        SceCtrlData pad{};SceTouchData touch{};sceCtrlPeekBufferPositive(0,&pad,1);sceTouchPeek(SCE_TOUCH_PORT_FRONT,&touch,1);
        uint32_t pressed=pad.buttons&~previous;bool touchEdge=touch.reportNum&&!previousTouch;
        previous=pad.buttons;previousTouch=touch.reportNum>0;gxm_media_pump();
        const uint64_t t1=sceKernelGetProcessTimeWide();
        if(!game&&!launcherFontOpen&&!launcherSettingsOpen&&(pressed&SCE_CTRL_CROSS))break;
        if(game){game->tick(pad,touch);if(game->leaving)game.reset();}
        else if(launcherClockOpen){
            const int action=launcherClockMenu.input(pressed,touchEdge,touch);
            if(action>0){launcherClockMenu.failed=!direct::save_clock_settings(clockSettingsPath,launcherClockMenu.value);
                if(!launcherClockMenu.failed){configure_clocks(launcherClockMenu.value);report_cpu_clock("menu-save");launcherClockMenu.saved=true;}}
            if(action<0)launcherClockOpen=false;
        }
        else if(launcherSettingsOpen){
            const int action=launcherSettingsMenu.input(pressed,touchEdge,touch);
            if(action<0)launcherSettingsOpen=false;
            else if(action==1)cpu3Setting.toggle();
            else if(action==2){launcherClockMenu={current_clock_settings()};
                cpuClock.actual=scePowerGetArmClockFrequency();es4Clock.actual=scePowerGetGpuClockFrequency();launcherClockOpen=true;}
        }
        else if(launcherFontOpen){
            int action=launcherFontMenu.input(pressed,touchEdge,touch);
            if(action>0){sceIoMkdir(fontSettingsDirectory.c_str(),0777);
                if(!direct::save_font_settings(direct::font_settings_path(fontSettingsDirectory,games[selected].id),launcherFontMenu.value)){
                    launcherFontMenu.failed=true;action=0;}}
            if(action){launcherFontOpen=false;direct::fallback_menu_release();}
        }
        else if(pressed&(SCE_CTRL_START|SCE_CTRL_TRIANGLE)){
            cpu3Setting.open();launcherSettingsMenu={};launcherSettingsOpen=true;
        }
        else if(!games.empty()&&(pressed&SCE_CTRL_SQUARE)){
            launcherFontMenu={direct::load_font_settings(direct::font_settings_path(fontSettingsDirectory,games[selected].id))};launcherFontOpen=true;
        }
        else if(!games.empty()){
            if(pressed&SCE_CTRL_DOWN)selected=(selected+1)%games.size();if(pressed&SCE_CTRL_UP)selected=(selected+games.size()-1)%games.size();
            bool launch=pressed&SCE_CTRL_CIRCLE;
            if(touchEdge){int x=touch.report[0].x/2,y=touch.report[0].y/2;size_t first=selected/5*5;
                if(x>=30&&x<930&&y>=110&&y<460){size_t hit=first+(y-110)/70;if(hit<games.size()){selected=hit;launch=true;}}}
            if(launch&&games[selected].ready()){art3m1s::save_last_game(games[selected].id);game=std::make_unique<Game>(games[selected]);}
        }
        if(game)game->prepare();else if(launcherClockOpen){launcherClockMenu.prepare(cpuClock,es4Clock);}else if(launcherSettingsOpen){launcherSettingsMenu.prepare();}else if(launcherFontOpen){if(!direct::fallback_menu_prepare())launcherFontOpen=false;}else{
            direct::menu_prepare(title,30);direct::menu_prepare("选择游戏",24);direct::menu_prepare(help,20);
            direct::menu_prepare("未找到游戏，请复制到 games 目录。",24);direct::menu_prepare("资源不完整",18);
            size_t first=selected/5*5;for(size_t i=first;i<games.size()&&i<first+5;i++)direct::menu_prepare(games[i].title.c_str(),22);
        }
        const uint64_t t2=sceKernelGetProcessTimeWide();direct::begin();
        if(game)game->draw();else if(launcherClockOpen){launcherClockMenu.draw(cpuClock,es4Clock);}else if(launcherSettingsOpen){launcherSettingsMenu.draw(cpu3Setting);}else if(launcherFontOpen){launcherFontMenu.draw();}else{
            direct::menu_text(36,54,30,title);direct::rect(36,74,888,2,0x354256ff);direct::menu_text(36,103,24,"选择游戏");
            size_t first=selected/5*5;
            for(size_t i=first;i<games.size()&&i<first+5;i++){float y=110+(i-first)*70;
                direct::rect(30,y,900,62,i==selected?0x286482ff:0x1c2838ff);
                direct::menu_text(48,y+39,22,games[i].title.c_str(),games[i].ready()?0xffffffff:0x8895a5ff);
                if(!games[i].ready())direct::menu_text(770,y+39,18,"资源不完整");}
            if(games.empty())direct::menu_text(48,180,24,"未找到游戏，请复制到 games 目录。");
            direct::menu_text(36,529,20,help);
        }
        direct::end();const uint64_t t3=sceKernelGetProcessTimeWide();art3m1s_gxm_finish_host_frame();
        if(game)game->loading_frame_complete(sceKernelGetProcessTimeWide());
#ifdef DIRECT_SEMANTIC_CONTROLS
        if(game)game->host_menu_presented();
#endif
        uint64_t now=sceKernelGetProcessTimeWide();mediaUs+=t1-t0;logicUs+=t2-t1;presentUs+=t3-t2;captureUs+=now-t3;
        ++samples;maxUs=std::max(maxUs,now-t0);if(now-t0>20000)++slowFrames;
        // Bounded, queued diagnostics: retain individual transition spikes
        // instead of attributing a five-second aggregate to one frame.
        static unsigned spikeReports=0,spikeSuppressed=0;
        if(now-t0>=40000){
            if(spikeReports<8){
                ++spikeReports;
                direct::log("[frame-spike] at_us=%llu total_us=%llu media_us=%llu logic_menu_us=%llu direct_present_us=%llu capture_us=%llu",
                    (unsigned long long)now,(unsigned long long)(now-t0),(unsigned long long)(t1-t0),
                    (unsigned long long)(t2-t1),(unsigned long long)(t3-t2),(unsigned long long)(now-t3));
            }else ++spikeSuppressed;
        }
        if(now-heartbeat>5000000){heartbeat=now;
            if(spikeSuppressed)direct::log("[frame-spike] suppressed=%u at_us=%llu",spikeSuppressed,(unsigned long long)now);
            spikeReports=spikeSuppressed=0;
#ifdef DIRECT_RESOURCE_LEDGER
            art3m1s_resource_report();
#endif
#ifdef DIRECT_HEAP_DIAGNOSTICS
            // mallinfo takes the allocator lock; sample only on the existing
            // five-second heartbeat, before logging allocates formatting data.
            const auto heap=mallinfo();
            direct::log("[heap-perf] at_us=%llu arena=%d used=%d free=%d free_chunks=%d top=%d heap_limit=%u; newlib bytes, top is not largest free block",
                (unsigned long long)now,heap.arena,heap.uordblks,heap.fordblks,heap.ordblks,heap.keepcost,_newlib_heap_size_user);
#endif
#ifdef DIRECT_DEFERRED_FINISH_CANDIDATE
            static direct::WaitStats previousWaits{};auto waits=direct::deferred_wait_stats();
            uint64_t averages[unsigned(direct::WaitSite::Count)]{},totalWait=0;
            for(unsigned i=0;i<unsigned(direct::WaitSite::Count);++i){
                auto elapsed=waits.microseconds[i]-previousWaits.microseconds[i];
                averages[i]=elapsed/samples;totalWait+=elapsed;
            }
            previousWaits=waits;
            direct::log("[gxm-wait] frames=%u end_avg_us=%llu begin_avg_us=%llu update_avg_us=%llu destroy_avg_us=%llu readback_avg_us=%llu explicit_avg_us=%llu mode_avg_us=%llu total_avg_us=%llu at_us=%llu; total includes waits outside end",
                samples,(unsigned long long)averages[0],(unsigned long long)averages[1],(unsigned long long)averages[2],
                (unsigned long long)averages[3],(unsigned long long)averages[4],(unsigned long long)averages[5],
                (unsigned long long)averages[6],(unsigned long long)(totalWait/samples),(unsigned long long)now);
#endif
            direct::log("[frame-perf] frames=%u media_avg_us=%llu logic_menu_avg_us=%llu direct_present_avg_us=%llu capture_avg_us=%llu max_us=%llu over20ms=%u at_us=%llu; wall includes waits",
                samples,(unsigned long long)(mediaUs/samples),(unsigned long long)(logicUs/samples),(unsigned long long)(presentUs/samples),(unsigned long long)(captureUs/samples),(unsigned long long)maxUs,slowFrames,(unsigned long long)now);
            mediaUs=logicUs=presentUs=captureUs=maxUs=0;samples=slowFrames=0;
            report_log_timing();flush_log();}
    }
    game.reset();direct::menu_release();cpuClock.shutdown();es4Clock.shutdown();report_cpu_clock("exit");direct::prepare_process_exit();sceAppUtilShutdown();
#ifdef DIRECT_RESOURCE_LEDGER
    art3m1s_resource_report();
#endif
    direct::log("direct host clean exit");logQueue.stop();if(output){std::fclose(output);output=nullptr;}
    sceKernelExitProcess(0);return 0;
}
