#include "gpu.hpp"
#include "game_library.hpp"
#include "media_gxm.hpp"
#include "runtime_api.h"
extern "C" {
#include "files.h"
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

extern "C" { unsigned int _newlib_heap_size_user=192*1024*1024;
void art3m1s_gxm_finish_host_frame();void art3m1s_gxm_reset_readback();
int art3m1s_runtime_prepare_gxm_textures(void*);
void art3m1s_runtime_set_profiler_enabled(const void*,int);
int art3m1s_runtime_profiler_snapshot(const void*,uint8_t*,uint32_t); }
namespace {
FILE* output=nullptr;pthread_mutex_t logMutex=PTHREAD_MUTEX_INITIALIZER;
std::atomic<int> archiveDone{0},archiveTotal{0};
void media_log(void* c,int level,const char* format,va_list args){
    if(level>av_log_get_level())return;
    // Keep FFmpeg chatter filtered, but retain the host diagnostics needed to
    // distinguish finished speech from continuing decoder/render workload.
    if(level>AV_LOG_WARNING&&std::strncmp(format,"[audio",6)&&std::strncmp(format,"[thread-perf]",13))return;
    char line[4096];thread_local int prefix=1;av_log_format_line(c,level,format,args,line,sizeof(line),&prefix);direct::log("[media] %s",line);}
void core_log(const char* level,const char* text){direct::log("[core:%s] %s",level?level:"?",text?text:"");}
std::vector<uint8_t> read_ini(const std::string& path){
    std::vector<uint8_t> bytes;FILE* f=std::fopen(path.c_str(),"rb");
    if(f){std::fseek(f,0,SEEK_END);long n=std::ftell(f);std::rewind(f);if(n>0&&n<16*1024*1024){bytes.resize(n);if(std::fread(bytes.data(),1,n,f)!=size_t(n))bytes.clear();}std::fclose(f);}
    if(bytes.empty()){int n=host_read("system.ini",nullptr,0,-1);if(n>0&&n<16*1024*1024){bytes.resize(n);if(host_read("system.ini",bytes.data(),n,0)!=n)bytes.clear();}}return bytes;
}
struct Game {
    art3m1s::GameEntry entry;void* runtime=nullptr;pthread_t worker{};bool joining=false,leaving=false;
    std::atomic<int> result{-999};int phase=0;std::string error;uint64_t last=0;uint32_t buttons=0;bool touched=false;
    int mouseX=480,mouseY=272;
    bool tracing=false,traceRequested=false;uint64_t traceAt=0,tracePollAt=0,logicMax=0,prepareMax=0;unsigned slowTicks=0;
    explicit Game(art3m1s::GameEntry e):entry(std::move(e)){archiveDone=0;archiveTotal=0;art3m1s_gxm_reset_readback();}
    static void* load(void* p){auto* g=static_cast<Game*>(p);std::string save=std::string(art3m1s::kDataRoot)+"/saves/"+g->entry.id;
        sceIoMkdir((std::string(art3m1s::kDataRoot)+"/saves").c_str(),0777);g->result=host_files_open(g->entry.path.c_str(),save.c_str());archiveDone=archiveTotal.load();return nullptr;}
    ~Game(){if(joining)pthread_join(worker,nullptr);art3m1s_gxm_reset_readback();gxm_media_detach();gxm_media_pump();direct::wait();
        if(runtime)art3m1s_runtime_destroy(runtime);direct::menu_release();direct::log("game resources released");}
    void boot(){
        art3m1s_register_log_callback(core_log);art3m1s_register_file_reader(host_read);art3m1s_register_file_writer(host_write);art3m1s_register_file_delete(host_delete);
        runtime=art3m1s_runtime_create(960,544,5);if(!runtime){error="无法创建运行时";return;}
        gxm_media_attach(runtime);art3m1s_register_media_command_callback(gxm_media_command);auto ini=read_ini(entry.path+"/system.ini");
        if(ini.empty()||art3m1s_runtime_load_project_bytes(runtime,ini.data(),ini.size(),"WINDOWS")!=0){error="加载游戏失败";return;}
        SceIoStat traceStat{};traceRequested=sceIoGetstat("ux0:data/art3m1s-gxm/trace-nextline.flag",&traceStat)>=0;
        update_trace(sceKernelGetProcessTimeWide(),true);
        direct::menu_release();traceAt=last=sceKernelGetProcessTimeWide();phase=4;
        direct::log("game loaded: %s stage=%ux%u",entry.id.c_str(),art3m1s_runtime_stage_width(runtime),art3m1s_runtime_stage_height(runtime));
    }
    void update_trace(uint64_t now,bool initial=false){
        if(!initial&&(!traceRequested||now-tracePollAt<1000000))return;
        tracePollAt=now;SceIoStat stat{};
        bool enabled=traceRequested&&sceIoGetstat("ux0:data/art3m1s-gxm/trace-nextline.off",&stat)<0;
        if(!initial&&enabled==tracing)return;
        tracing=enabled;art3m1s_runtime_set_profiler_enabled(runtime,tracing?1:0);
        traceAt=now;logicMax=prepareMax=slowTicks=0;
        direct::log("[profile-state] at_us=%llu enabled=%d arm=%d bus=%d gpu=%d xbar=%d; discard frame windows crossing this marker",
            (unsigned long long)now,int(tracing),scePowerGetArmClockFrequency(),scePowerGetBusClockFrequency(),scePowerGetGpuClockFrequency(),scePowerGetGpuXbarClockFrequency());
    }
    void input(const SceCtrlData& pad,const SceTouchData& touch){
        uint32_t changed=buttons^pad.buttons;if(changed&pad.buttons&(SCE_CTRL_CROSS|SCE_CTRL_CIRCLE|SCE_CTRL_START))gxm_media_skip();
        struct Key{uint32_t b,k;};const Key keys[]={{SCE_CTRL_CIRCLE,13},{SCE_CTRL_CROSS,27},{SCE_CTRL_START,13},{SCE_CTRL_RTRIGGER,17},{SCE_CTRL_UP,38},{SCE_CTRL_DOWN,40}};
        for(auto key:keys)if(changed&key.b)art3m1s_runtime_feed_key(runtime,key.k,(pad.buttons&key.b)!=0);
        if(std::abs(int(pad.lx)-128)>24)mouseX+=(int(pad.lx)-128)/20;if(std::abs(int(pad.ly)-128)>24)mouseY+=(int(pad.ly)-128)/20;
        int w=art3m1s_runtime_stage_width(runtime),h=art3m1s_runtime_stage_height(runtime);
        mouseX=std::clamp(mouseX,0,std::max(w-1,0));mouseY=std::clamp(mouseY,0,std::max(h-1,0));
        if(touch.reportNum){mouseX=int(touch.report[0].x)*w/1920;mouseY=int(touch.report[0].y)*h/1088;if(!touched)gxm_media_skip();}
        if(tracing&&(changed||touched!=(touch.reportNum>0)))
            direct::log("[input-trace] buttons=%08x touch=%u mouse=%d,%d",unsigned(pad.buttons),unsigned(touch.reportNum),mouseX,mouseY);
        art3m1s_runtime_feed_mouse(runtime,mouseX,mouseY);art3m1s_runtime_feed_mouse_button(runtime,1,touch.reportNum>0||(pad.buttons&SCE_CTRL_SQUARE));
        buttons=pad.buttons;touched=touch.reportNum>0;
    }
    void tick(const SceCtrlData& pad,const SceTouchData& touch){
        if(!error.empty()){if(pad.buttons&SCE_CTRL_CROSS)leaving=true;return;}
        if(phase==0){phase=1;if(pthread_create(&worker,nullptr,load,this))error="无法启动资源读取线程";else joining=true;return;}
        if(phase==1){if(result.load()!=-999){pthread_join(worker,nullptr);joining=false;if(result<0)error="无法打开游戏目录";else phase=2;}return;}
        if(phase==2){phase=3;return;}if(phase==3){boot();buttons=pad.buttons;touched=touch.reportNum>0;return;}
        uint64_t now=sceKernelGetProcessTimeWide();update_trace(now);
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
    void prepare(){if(phase==4&&error.empty())return;
        direct::menu_prepare("正在加载游戏  正在读取资源  正在初始化引擎  × 返回",24);direct::menu_prepare(entry.title.c_str(),22);direct::menu_prepare(error.c_str(),24);}
    void draw(){if(phase==4&&error.empty()){art3m1s_runtime_present_gxm(runtime);host_video_present_idle();return;}
        direct::menu_text(48,110,24,error.empty()?"正在加载游戏":error.c_str());direct::menu_text(48,170,22,entry.title.c_str());
        if(!error.empty()){direct::menu_text(48,250,24,"× 返回");return;}
        direct::menu_text(48,225,24,phase>=2?"正在初始化引擎":"正在读取资源");
        float progress=phase>=2?.85f:(archiveTotal>0?.1f+.6f*archiveDone.load()/archiveTotal.load():.05f);
        direct::rect(48,260,864,14,0x293748ff);direct::rect(48,260,864*progress,14,0x50c3ebff);
    }
};
}
namespace direct { void log(const char* format,...){pthread_mutex_lock(&logMutex);va_list args;va_start(args,format);
    if(output){std::vfprintf(output,format,args);std::fputc('\n',output);}va_end(args);pthread_mutex_unlock(&logMutex);} }
extern "C" void host_loading_show(int stage,const char* detail){int done=0,total=0;if(stage==2&&detail&&std::sscanf(detail,"PFS %d / %d",&done,&total)==2){archiveTotal=total;archiveDone=std::max(done-1,0);}}
extern "C" void host_loading_finish(){}

int main(){
    sceIoMkdir(art3m1s::kDataRoot,0777);sceIoMkdir(art3m1s::kGamesRoot,0777);
    sceIoRemove("ux0:data/art3m1s-gxm/host.previous.log");sceIoRename("ux0:data/art3m1s-gxm/host.log","ux0:data/art3m1s-gxm/host.previous.log");
    output=std::fopen("ux0:data/art3m1s-gxm/host.log","w");if(output)std::setvbuf(output,nullptr,_IOFBF,32768);
#ifdef DIRECT_TEXT_LAYOUT_CANDIDATE
    direct::log("Direct GXM 01.02 optI layout cache candidate build %s %s; REBUILT current core, not pinned Opt2; optG audio, optE renderer and unchanged host shaders",__DATE__,__TIME__);
#else
    direct::log("Direct GXM 01.02 optH profile gate build %s %s; optG audio and optE renderer, live diagnostic profiling control; pinned Opt2 core and unchanged shaders",__DATE__,__TIME__);
#endif
    if(output)std::fflush(output);
    av_log_set_callback(media_log);av_log_set_level(AV_LOG_INFO);
    SceAppUtilInitParam init{};SceAppUtilBootParam boot{};sceAppUtilInit(&init,&boot);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT,SCE_TOUCH_SAMPLING_STATE_START);
    if(!direct::init()){if(output)std::fflush(output);return 1;}
    direct::log("[clock-readonly] arm=%d bus=%d gpu=%d xbar=%d MHz",
        scePowerGetArmClockFrequency(),scePowerGetBusClockFrequency(),scePowerGetGpuClockFrequency(),scePowerGetGpuXbarClockFrequency());
    auto games=art3m1s::scan_games();size_t selected=0;auto last=art3m1s::load_last_game();
    for(size_t i=0;i<games.size();i++)if(games[i].id==last)selected=i;
    std::unique_ptr<Game> game;uint32_t previous=0;bool previousTouch=false;uint64_t heartbeat=0;
    uint64_t mediaUs=0,logicUs=0,presentUs=0,captureUs=0,maxUs=0;unsigned samples=0,slowFrames=0;
    const char* title="art3m1s  /  Direct GXM";const char* help="○ 确认   × 退出   ↑↓ 选择";
    for(;;){
        const uint64_t t0=sceKernelGetProcessTimeWide();
        SceCtrlData pad{};SceTouchData touch{};sceCtrlPeekBufferPositive(0,&pad,1);sceTouchPeek(SCE_TOUCH_PORT_FRONT,&touch,1);
        uint32_t pressed=pad.buttons&~previous;bool touchEdge=touch.reportNum&&!previousTouch;
        previous=pad.buttons;previousTouch=touch.reportNum>0;gxm_media_pump();
        const uint64_t t1=sceKernelGetProcessTimeWide();
        if(!game&&(pressed&SCE_CTRL_CROSS))break;
        if(game){game->tick(pad,touch);if(game->leaving)game.reset();}
        else if(!games.empty()){
            if(pressed&SCE_CTRL_DOWN)selected=(selected+1)%games.size();if(pressed&SCE_CTRL_UP)selected=(selected+games.size()-1)%games.size();
            bool launch=pressed&SCE_CTRL_CIRCLE;
            if(touchEdge){int x=touch.report[0].x/2,y=touch.report[0].y/2;size_t first=selected/5*5;
                if(x>=30&&x<930&&y>=110&&y<460){size_t hit=first+(y-110)/70;if(hit<games.size()){selected=hit;launch=true;}}}
            if(launch&&games[selected].ready()){art3m1s::save_last_game(games[selected].id);game=std::make_unique<Game>(games[selected]);}
        }
        if(game)game->prepare();else{
            direct::menu_prepare(title,30);direct::menu_prepare("选择游戏",24);direct::menu_prepare(help,20);
            direct::menu_prepare("未找到游戏，请复制到 games 目录。",24);direct::menu_prepare("资源不完整",18);
            size_t first=selected/5*5;for(size_t i=first;i<games.size()&&i<first+5;i++)direct::menu_prepare(games[i].title.c_str(),22);
        }
        const uint64_t t2=sceKernelGetProcessTimeWide();direct::begin();
        if(game)game->draw();else{
            direct::menu_text(36,54,30,title);direct::rect(36,74,888,2,0x354256ff);direct::menu_text(36,103,24,"选择游戏");
            size_t first=selected/5*5;
            for(size_t i=first;i<games.size()&&i<first+5;i++){float y=110+(i-first)*70;
                direct::rect(30,y,900,62,i==selected?0x286482ff:0x1c2838ff);
                direct::menu_text(48,y+39,22,games[i].title.c_str(),games[i].ready()?0xffffffff:0x8895a5ff);
                if(!games[i].ready())direct::menu_text(770,y+39,18,"资源不完整");}
            if(games.empty())direct::menu_text(48,180,24,"未找到游戏，请复制到 games 目录。");
            direct::menu_text(36,515,20,help);
        }
        direct::end();const uint64_t t3=sceKernelGetProcessTimeWide();art3m1s_gxm_finish_host_frame();
        uint64_t now=sceKernelGetProcessTimeWide();mediaUs+=t1-t0;logicUs+=t2-t1;presentUs+=t3-t2;captureUs+=now-t3;
        ++samples;maxUs=std::max(maxUs,now-t0);if(now-t0>20000)++slowFrames;
        if(now-heartbeat>5000000){heartbeat=now;
            direct::log("[frame-perf] frames=%u media_avg_us=%llu logic_menu_avg_us=%llu direct_present_avg_us=%llu capture_avg_us=%llu max_us=%llu over20ms=%u at_us=%llu; wall includes waits",
                samples,(unsigned long long)(mediaUs/samples),(unsigned long long)(logicUs/samples),(unsigned long long)(presentUs/samples),(unsigned long long)(captureUs/samples),(unsigned long long)maxUs,slowFrames,(unsigned long long)now);
            mediaUs=logicUs=presentUs=captureUs=maxUs=0;samples=slowFrames=0;
            pthread_mutex_lock(&logMutex);if(output)std::fflush(output);pthread_mutex_unlock(&logMutex);}
    }
    game.reset();direct::menu_release();direct::prepare_process_exit();sceAppUtilShutdown();
    direct::log("direct host clean exit");if(output){std::fclose(output);output=nullptr;}
    sceKernelExitProcess(0);return 0;
}
