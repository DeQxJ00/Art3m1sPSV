// Independent external-resource demo. Production cache settings are untouched.
#include "gpu.hpp"
#include <psp2/ctrl.h>
#include <psp2/power.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/clib.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <map>
#include <vector>
#include <pthread.h>
extern "C" {
void* art3m1s_texture_study_decode(const uint8_t*,size_t,uint32_t*,uint32_t*,const uint8_t**);
void* art3m1s_png_zero_study_decode(const uint8_t*,size_t,int,uint32_t*,uint32_t*,const uint8_t**,uint64_t*);
void art3m1s_texture_study_free(void*);
void* art3m1s_cache_study_encode(unsigned,const uint8_t*,size_t,const uint8_t**,size_t*);
int art3m1s_cache_study_decode(unsigned,const uint8_t*,size_t,uint8_t*,size_t);
void art3m1s_cache_study_free(void*);
int art3m1s_cache_study_select(const uint8_t*,size_t,int,uint64_t*);
}
namespace direct { namespace cache_study {
const char* modes[]={"OFF / RGBA","zlib level 1","LZ4 block","RLE32","LZW byte"};
struct Sample { unsigned scene=0,role=0,w=0,h=0;char name[32]{}; };
struct Block {std::shared_ptr<void> handle;const uint8_t* data=nullptr;size_t len=0,offset=0,raw=0;};
struct Pack { std::vector<Block> blocks;size_t bytes=0;uint64_t encodeUs=0;bool ok=false; };
struct Result {uint64_t alloc=0,decode=0,copy=0,seal=0,total=0,maxBlock=0;size_t scratch=0,gpu=0;bool ok=false;};
// Persistent CPU workers and the calling thread decode independent blocks.
// Only the calling thread allocates, copies, seals or otherwise accesses GXM textures.
class DecodeWorker {
    pthread_mutex_t mutex_=PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t wake_=PTHREAD_COND_INITIALIZER,done_=PTHREAD_COND_INITIALIZER;
    pthread_t thread_{};bool active_=false,stop_=false,pending_=false,finished_=true,ok_=false;
    const Block* block_=nullptr;unsigned mode_=0;
    static void* entry(void* p){static_cast<DecodeWorker*>(p)->run();return nullptr;}
    void run(){
        const int priorityResult=sceKernelChangeThreadPriority(0,161);
        SceKernelThreadInfo info{};info.size=sizeof(info);const int queried=sceKernelGetThreadInfo(sceKernelGetThreadId(),&info);
        log("[cache-study-worker] priority=161 apply=%d query=%d affinity=%x; CPU worker, no GPU calls",priorityResult,queried,queried>=0?info.currentCpuAffinityMask:0);
        for(;;){
            pthread_mutex_lock(&mutex_);
            while(!pending_&&!stop_)pthread_cond_wait(&wake_,&mutex_);
            if(stop_){pthread_mutex_unlock(&mutex_);return;}
            const Block* b=block_;unsigned mode=mode_;pending_=false;pthread_mutex_unlock(&mutex_);
            const bool ok=b->raw<=pixels.size()&&art3m1s_cache_study_decode(mode,b->data,b->len,pixels.data(),b->raw)!=0;
            pthread_mutex_lock(&mutex_);ok_=ok;finished_=true;pthread_cond_signal(&done_);pthread_mutex_unlock(&mutex_);
        }
    }
public:
    std::vector<uint8_t> pixels=std::vector<uint8_t>(64*1024);
    DecodeWorker(){active_=pthread_create(&thread_,nullptr,entry,this)==0;}
    bool valid()const{return active_;}
    void submit(unsigned mode,const Block& b){
        pthread_mutex_lock(&mutex_);mode_=mode;block_=&b;finished_=false;pending_=true;
        pthread_cond_signal(&wake_);pthread_mutex_unlock(&mutex_);
    }
    bool join_block(){pthread_mutex_lock(&mutex_);while(!finished_)pthread_cond_wait(&done_,&mutex_);bool ok=ok_;pthread_mutex_unlock(&mutex_);return ok;}
    ~DecodeWorker(){
        if(active_){pthread_mutex_lock(&mutex_);stop_=true;pthread_cond_signal(&wake_);pthread_mutex_unlock(&mutex_);pthread_join(thread_,nullptr);}
        pthread_cond_destroy(&wake_);pthread_cond_destroy(&done_);pthread_mutex_destroy(&mutex_);
    }
};
using DecodeWorkers=std::vector<std::unique_ptr<DecodeWorker>>;
uint64_t now(){return sceKernelGetProcessTimeWide();}
std::vector<uint8_t> read(const std::string& path){
    FILE* f=std::fopen(path.c_str(),"rb");if(!f)return {};
    std::fseek(f,0,SEEK_END);long n=std::ftell(f);std::rewind(f);
    if(n<=0||n>16*1024*1024){std::fclose(f);return {};}
    std::vector<uint8_t> out(n);if(std::fread(out.data(),1,n,f)!=size_t(n))out.clear();std::fclose(f);return out;
}
Pack pack(unsigned mode,const uint8_t* src,size_t n,size_t chunk){
    Pack p;const auto start=now();
    for(size_t offset=0;offset<n;offset+=chunk){
        Block b;b.offset=offset;b.raw=std::min(chunk,n-offset);
        void* h=art3m1s_cache_study_encode(mode,src+offset,b.raw,&b.data,&b.len);
        if(!h)return p;b.handle={h,art3m1s_cache_study_free};p.bytes+=b.len;p.blocks.push_back(std::move(b));
    }
    p.encodeUs=now()-start;p.ok=true;return p;
}
bool exact(Texture* t,const uint8_t* rgba,unsigned w,unsigned h){
    for(unsigned y=0;y<h;++y)if(std::memcmp(t->pixels+size_t(y)*t->stride*4,rgba+size_t(y)*w*4,size_t(w)*4))return false;
    return true;
}
void copy_rows(Texture* t,const uint8_t* src,size_t offset,size_t n){
    const size_t row=size_t(t->w)*4,stride=size_t(t->stride)*4;
    if(row==stride){sceClibMemcpy(t->pixels+offset,src,n);return;}
    while(n){const size_t y=offset/row,x=offset%row,take=std::min(n,row-x);
        sceClibMemcpy(t->pixels+y*stride+x,src,take);
        if(x+take==row&&stride>row)sceClibMemset(t->pixels+y*stride+row,src[take-1],stride-row);
        offset+=take;src+=take;n-=take;}
}
// Write-only GPU restore. Coalesce complete rows (including padding) into one
// fill. Never inspect CDRAM to find zeros, nor read it for back-references.
void fill_rows(Texture* t,uint8_t value,size_t offset,size_t n){
    const size_t row=size_t(t->w)*4,stride=size_t(t->stride)*4;
    if(row==stride){sceClibMemset(t->pixels+offset,value,n);return;}
    while(n){const size_t y=offset/row,x=offset%row;
        if(x==0&&n>=row){const size_t rows=n/row;sceClibMemset(t->pixels+y*stride,value,rows*stride);offset+=rows*row;n-=rows*row;}
        else {const size_t take=std::min(n,row-x);sceClibMemset(t->pixels+y*stride+x,value,take);
            if(x+take==row&&stride>row)sceClibMemset(t->pixels+y*stride+row,value,stride-row);
            offset+=take;n-=take;}
    }
}
uint32_t word32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
bool sparse_gpu(Texture* t,const Block& b){
    if(b.len<12||std::memcmp(b.data,"ZSP1",4)||word32(b.data+4)!=b.raw)return false;
    const auto tag=word32(b.data+8);
    if(tag==0){if(b.len-12!=b.raw)return false;copy_rows(t,b.data+12,b.offset,b.raw);return true;}
    if(tag!=1)return false;
    size_t s=12,d=0;
    while(d<b.raw){
        if(b.len-s<8)return false;const size_t zero=word32(b.data+s),literal=word32(b.data+s+4);s+=8;
        if(zero%4||literal%4||(!zero&&!literal)||zero>b.raw-d||literal>b.raw-d-zero||literal>b.len-s)return false;
        if(zero)fill_rows(t,0,b.offset+d,zero);d+=zero;
        if(literal)copy_rows(t,b.data+s,b.offset+d,literal);d+=literal;s+=literal;
    }
    return s==b.len;
}
// Fair control: the original RLE32 stream with the same write-only SDK copies.
// Any improvement shared by this route is an upload improvement, not a new codec win.
bool rle_gpu(Texture* t,const Block& b){
    size_t s=0,d=0;alignas(16) uint8_t repeated[1024];
    while(s<b.len){
        if(b.len-s<4)return false;const uint32_t header=word32(b.data+s);s+=4;
        const size_t count=header&0x7fffffff;
        if(!count||count>(b.raw-d)/4)return false;const size_t n=count*4;
        if(header&0x80000000){
            if(b.len-s<4)return false;const auto* pixel=b.data+s;s+=4;
            if(pixel[0]==pixel[1]&&pixel[0]==pixel[2]&&pixel[0]==pixel[3])fill_rows(t,pixel[0],b.offset+d,n);
            else {for(size_t j=0;j<sizeof(repeated);j+=4)std::memcpy(repeated+j,pixel,4);
                for(size_t j=0;j<n;j+=sizeof(repeated))copy_rows(t,repeated,b.offset+d+j,std::min(sizeof(repeated),n-j));}
        } else {if(n>b.len-s)return false;copy_rows(t,b.data+s,b.offset+d,n);s+=n;}
        d+=n;
    }
    return d==b.raw;
}
// Full CPU restore, bounded staging buffer, or decode directly into a private
// mapped GPU allocation. No GXM sampling until all blocks have been restored.
Result upload(const Sample& s,const uint8_t* reference,const std::vector<uint8_t>& proof,
              unsigned mode,const Pack* pack,const char* route,Texture** preview=nullptr,DecodeWorkers* workers=nullptr){
    Result r;const size_t n=size_t(s.w)*s.h*4;const auto start=now();
    Texture* t=surface_prepare(s.w,s.h);r.alloc=now()-start;if(!t)return r;r.gpu=t->allocation.bytes;
    const bool direct=std::strcmp(route,"direct")==0;
    const bool native=std::strcmp(route,"gpu_spans")==0;
    unsigned threads=1;
    if(std::strlen(route)==8&&std::strncmp(route,"staging",7)==0&&route[7]>='2'&&route[7]<='4')threads=unsigned(route[7]-'0');
    if(threads>1&&(!workers||workers->size()<threads-1)){surface_abort(t);return r;}
    for(unsigned j=0;j+1<threads;++j)if(!(*workers)[j]->valid()){surface_abort(t);return r;}
    size_t capacity=0;if(pack&&!direct&&!native)for(const auto& b:pack->blocks)capacity=std::max(capacity,b.raw);
    const auto allocated=now();std::vector<uint8_t> scratch(capacity);r.alloc+=now()-allocated;r.scratch=capacity;
    for(unsigned j=0;j+1<threads;++j)r.scratch+=(*workers)[j]->pixels.size();
    bool ok=true;
    if(!pack){const auto a=now();copy_rows(t,reference,0,n);r.copy=now()-a;}
    else if(native){
        r.scratch=mode==3?1024:0;
        for(const auto& b:pack->blocks){const auto a=now();
            ok=mode==7?sparse_gpu(t,b):mode==3?rle_gpu(t,b):false;
            const auto elapsed=now()-a;r.decode+=elapsed;r.maxBlock=std::max(r.maxBlock,elapsed);if(!ok)break;
        }
    }
    else for(size_t i=0;i<pack->blocks.size();i+=threads){
        const auto& b=pack->blocks[i];
        const auto a=now();auto* dest=direct?t->pixels+b.offset:scratch.data();
        const unsigned count=unsigned(std::min(size_t(threads),pack->blocks.size()-i));
        for(unsigned j=1;j<count;++j)(*workers)[j-1]->submit(mode,pack->blocks[i+j]);
        ok=art3m1s_cache_study_decode(mode,b.data,b.len,dest,b.raw)!=0;
        // Always join every dispatched block, including when another decode fails.
        for(unsigned j=1;j<count;++j)ok=(*workers)[j-1]->join_block()&&ok;
        const auto decoded=now();r.decode+=decoded-a;
        if(!direct&&ok)copy_rows(t,scratch.data(),b.offset,b.raw);
        if(ok)for(unsigned j=1;j<count;++j){const auto& next=pack->blocks[i+j];copy_rows(t,(*workers)[j-1]->pixels.data(),next.offset,next.raw);}
        const auto copied=now();r.copy+=copied-decoded;r.maxBlock=std::max(r.maxBlock,copied-a);
        if(!ok)break;
    }
    const auto sealed=now();if(ok)ok=surface_seal(t,proof.data(),proof.size(),!direct);r.seal=now()-sealed;r.total=now()-start;
    // Verification is deliberately outside timing; includes RGB under alpha=0.
    r.ok=ok&&exact(t,reference,s.w,s.h);
    if(r.ok&&preview)*preview=t;else surface_abort(t);
    return r;
}
void emit(FILE* f,const Sample& s,unsigned cpu,unsigned mode,const char* route,unsigned chunk,unsigned round,
          size_t png,size_t cached,uint64_t enc,uint64_t readUs,const Result& r){
    if(!f)return;
    std::fprintf(f,"%u,%s,%u,%u,%u,%u,%s,%u,%u,%u,%u,%u,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%u,%u,%d\n",
        s.scene,s.name,s.w,s.h,cpu,mode,route,chunk,round,unsigned(png),s.w*s.h*4,unsigned(cached),
        (unsigned long long)enc,(unsigned long long)readUs,(unsigned long long)r.alloc,(unsigned long long)r.decode,
        (unsigned long long)r.copy,(unsigned long long)r.seal,(unsigned long long)r.total,(unsigned long long)r.maxBlock,
        unsigned(r.scratch),unsigned(r.gpu),int(r.ok));std::fflush(f);
}
void progress(const char* name,unsigned cpu,unsigned done,unsigned count){
    sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);char b[160];begin();
    menu_text(32,90,26,"CPU cache compression study");std::snprintf(b,sizeof(b),"%s   CPU %u MHz   %u / %u",name,cpu,done,count);menu_text(32,150,22,b);
    menu_text(32,210,20,"Timing decoded RGBA, not PNG recompression.");
    menu_text(32,250,20,"Raw copy / PNG / zlib / LZ4 / RLE32 / LZW");
    rect(32,300,860,18,0x304050ff);rect(32,300,860.f*done/std::max(1u,count),18,0x48b8a0ff);
    menu_text(32,355,20,"Cross: stop after current measurement");end();wait();
}
bool cancel(){SceCtrlData pad{};sceCtrlPeekBufferPositive(0,&pad,1);return (pad.buttons&SCE_CTRL_CROSS)!=0;}
Result native_upload(const Sample& s,const std::vector<uint8_t>& bytes){
    Result r;if(bytes.size()<128||std::memcmp(bytes.data(),"DDS ",4))return r;
    unsigned format=0;size_t header=128;
    if(!std::memcmp(bytes.data()+84,"DXT1",4))format=1;
    if(!std::memcmp(bytes.data()+84,"DXT3",4))format=2;
    if(!std::memcmp(bytes.data()+84,"DXT5",4))format=3;
    if(!std::memcmp(bytes.data()+84,"DX10",4)&&bytes.size()>=148){header=148;unsigned code=bytes[128];if(code==71||code==72)format=1;if(code==74||code==75)format=2;if(code==77||code==78)format=3;}
    if(!format)return r;
    const auto start=now();auto* t=texture_compressed(s.w,s.h,format,false,bytes.data()+header,bytes.size()-header);
    r.total=now()-start;r.copy=r.total;r.ok=t!=nullptr;if(t){r.gpu=t->allocation.bytes;surface_abort(t);}return r;
}
bool file_benchmark(const std::string& root,const std::vector<Sample>& samples){
    const auto inputKey=read(root+"/cache-study.key"),savedKey=read(root+"/cache-files.key");
    std::map<std::string,std::vector<std::string>> prior;
    if(!inputKey.empty()&&inputKey==savedKey)if(FILE* f=std::fopen((root+"/cache-files.csv").c_str(),"r")){
        char line[2048];while(std::fgets(line,sizeof(line),f)){
            char name[40];unsigned w,h,cpu,gpu,round;
            if(std::sscanf(line,"%39[^,],%u,%u,%u,%u,%u,",name,&w,&h,&cpu,&gpu,&round)==6&&cpu==333&&gpu==111&&round<4)
                prior[name].push_back(line);
        }std::fclose(f);
    }
    for(auto it=prior.begin();it!=prior.end();){
        bool good=it->second.size()==4;unsigned mask=0;
        for(const auto& line:it->second){char name[40];unsigned w,h,cpu,gpu,round;
            good&=line.size()>=3&&line.compare(line.size()-3,3,",1\n")==0;
            if(std::sscanf(line.c_str(),"%39[^,],%u,%u,%u,%u,%u,",name,&w,&h,&cpu,&gpu,&round)==6&&round<4)mask|=1u<<round;
        }
        if(!good||mask!=15)it=prior.erase(it);else ++it;
    }
    FILE* csv=std::fopen((root+"/cache-files.csv").c_str(),"w");if(!csv)return false;
    if(FILE* key=std::fopen((root+"/cache-files.key").c_str(),"wb")){
        std::fwrite(inputKey.data(),1,inputKey.size(),key);std::fclose(key);
    }
    std::fprintf(csv,"sample,width,height,cpu_mhz,gpu_mhz,round,png_bytes,raw_bytes,zero_bytes,zero_runs,estimated_bytes,eligible,read_us,png_decode_us,select_us,encode_us,packed_bytes,rgba_gpu_us,zsp_gpu_us,rgba_alloc_us,zsp_alloc_us,rgba_write_us,zsp_write_us,rgba_seal_us,zsp_seal_us,ok\n");
    for(const auto& g:prior)for(const auto& line:g.second)std::fputs(line.c_str(),csv);
    std::fflush(csv);scePowerSetArmClockFrequency(333);scePowerSetGpuClockFrequency(111);
    log("[cache-study-files] start images=%u resume=%u cpu=%d gpu=%d",unsigned(samples.size()),unsigned(prior.size()),scePowerGetArmClockFrequency(),scePowerGetGpuClockFrequency());
    bool pass=true;unsigned done=0;
    for(const auto& s:samples){
        const auto filename=std::string(s.name)+".png";
        progress(s.name,scePowerGetArmClockFrequency(),++done,unsigned(samples.size()));
        if(prior.count(filename))continue;
        for(unsigned round=0;round<4;++round){
            if(cancel()){std::fclose(csv);return false;}
            sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
            const auto a=now();const auto png=read(root+"/"+filename);const auto b=now();
            uint32_t w=0,h=0;const uint8_t* raw=nullptr;
            std::unique_ptr<void,decltype(&art3m1s_texture_study_free)> image(art3m1s_texture_study_decode(png.data(),png.size(),&w,&h,&raw),art3m1s_texture_study_free);
            const auto c=now();
            if(!image||w!=s.w||h!=s.h){pass=false;log("[cache-study-files] decode failed %s round=%u",s.name,round);break;}
            const size_t n=size_t(w)*h*4;uint64_t stats[4]{};
            const auto selStart=now();bool ok=art3m1s_cache_study_select(raw,n,1,stats)!=0;const auto selUs=now()-selStart;
            const auto packed=pack(7,raw,n,128*1024);ok&=packed.ok&&(n<512*1024||packed.bytes==stats[2]);
            std::vector<uint8_t> proof(32+size_t((w+63)/64)*((h+63)/64));
            ok&=prepare_opacity(w,h,raw,proof.data(),proof.size());
            Result rgba,zsp;
            if(ok){
                // Alternate upload order; exact comparisons happen inside upload
                // after the measured interval and before the allocation is freed.
                if((round+done)&1){rgba=upload(s,raw,proof,0,nullptr,"raw");zsp=upload(s,raw,proof,7,&packed,"gpu_spans");}
                else {zsp=upload(s,raw,proof,7,&packed,"gpu_spans");rgba=upload(s,raw,proof,0,nullptr,"raw");}
                ok&=rgba.ok&&zsp.ok;
            }
            pass&=ok;
            std::fprintf(csv,"%s,%u,%u,%d,%d,%u,%u,%u,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%u,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%d\n",
                filename.c_str(),w,h,scePowerGetArmClockFrequency(),scePowerGetGpuClockFrequency(),round,unsigned(png.size()),unsigned(n),
                (unsigned long long)stats[0],(unsigned long long)stats[1],(unsigned long long)stats[2],(unsigned long long)stats[3],
                (unsigned long long)(b-a),(unsigned long long)(c-b),(unsigned long long)selUs,(unsigned long long)packed.encodeUs,unsigned(packed.bytes),
                (unsigned long long)rgba.total,(unsigned long long)zsp.total,(unsigned long long)rgba.alloc,(unsigned long long)zsp.alloc,
                (unsigned long long)rgba.copy,(unsigned long long)zsp.decode,(unsigned long long)rgba.seal,(unsigned long long)zsp.seal,int(ok));
            std::fflush(csv);
        }
        log("[cache-study-files] image=%s complete=%u/%u pass=%d",s.name,done,unsigned(samples.size()),int(pass));
    }
    std::fclose(csv);log("[cache-study-files] complete pass=%d images=%u",int(pass),done);return pass;
}
bool png_zero_benchmark(const std::string& root,const std::vector<Sample>& samples){
    FILE* csv=std::fopen((root+"/cache-zero.csv").c_str(),"w");if(!csv)return false;
    std::fprintf(csv,"sample,width,height,cpu_mhz,gpu_mhz,round,route,png_bytes,raw_bytes,zero_pixels,pixels,read_us,decode_us,total_us,ok\n");
    scePowerSetArmClockFrequency(333);scePowerSetGpuClockFrequency(111);
    log("[cache-study-zero] start images=%u cpu=%d gpu=%d",unsigned(samples.size()),scePowerGetArmClockFrequency(),scePowerGetGpuClockFrequency());
    bool pass=true;unsigned done=0;
    for(const auto& s:samples){
        const auto filename=std::string(s.name)+".png";
        progress(s.name,scePowerGetArmClockFrequency(),++done,unsigned(samples.size()));
        const auto referencePng=read(root+"/"+filename);uint32_t rw=0,rh=0;const uint8_t* reference=nullptr;
        std::unique_ptr<void,decltype(&art3m1s_texture_study_free)> ref(art3m1s_texture_study_decode(referencePng.data(),referencePng.size(),&rw,&rh,&reference),art3m1s_texture_study_free);
        if(!ref||rw!=s.w||rh!=s.h){pass=false;break;}
        const size_t n=size_t(rw)*rh*4;uint64_t expected=0;
        // Exact independent reference count, outside every measured interval.
        for(size_t p=0;p<n;p+=4)expected+=word32(reference+p)==0;
        for(unsigned round=0;round<6;++round)for(unsigned turn=0;turn<3;++turn){
            if(cancel()){std::fclose(csv);return false;}
            sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
            const unsigned route=(round+turn+done)%3;
            const char* name=route==0?"production":route==1?"row_plain":"row_count";
            const auto start=now();const auto png=read(root+"/"+filename);const auto readEnd=now();
            uint32_t w=0,h=0;const uint8_t* raw=nullptr;uint64_t zeros=0;
            void* handle=route==0?art3m1s_texture_study_decode(png.data(),png.size(),&w,&h,&raw)
                :art3m1s_png_zero_study_decode(png.data(),png.size(),route==2,&w,&h,&raw,&zeros);
            const auto endTime=now();
            const bool ok=handle&&w==rw&&h==rh&&std::memcmp(raw,reference,n)==0&&(route!=2||zeros==expected);
            pass&=ok;
            std::fprintf(csv,"%s,%u,%u,%d,%d,%u,%s,%u,%u,%llu,%llu,%llu,%llu,%llu,%d\n",
                filename.c_str(),rw,rh,scePowerGetArmClockFrequency(),scePowerGetGpuClockFrequency(),round,name,unsigned(png.size()),unsigned(n),
                (unsigned long long)(route==2?zeros:expected),(unsigned long long)(n/4),(unsigned long long)(readEnd-start),
                (unsigned long long)(endTime-readEnd),(unsigned long long)(endTime-start),int(ok));std::fflush(csv);
            art3m1s_texture_study_free(handle);
        }
        log("[cache-study-zero] image=%s complete=%u/%u pass=%d",s.name,done,unsigned(samples.size()),int(pass));
    }
    std::fclose(csv);log("[cache-study-zero] complete pass=%d images=%u",int(pass),done);return pass;
}
bool automatic(const std::string& root,const std::vector<Sample>& samples){
    if(!read(root+"/cache-study.zero").empty())return png_zero_benchmark(root,samples);
    if(!read(root+"/cache-study.files").empty())return file_benchmark(root,samples);
    if(!read(root+"/cache-study.scan").empty()){
        FILE* csv=std::fopen((root+"/cache-scan.csv").c_str(),"w");if(!csv)return false;
        std::fprintf(csv,"sample,raw_bytes,cpu_mhz,round,gated,iterations,elapsed_us,zero_bytes,zero_runs,estimated_bytes,eligible,ok\n");
        scePowerSetArmClockFrequency(333);scePowerSetGpuClockFrequency(111);bool pass=true;unsigned done=0;
        for(const auto& s:samples){
            progress(s.name,scePowerGetArmClockFrequency(),++done,unsigned(samples.size()));
            if(cancel()){std::fclose(csv);return false;}
            const auto png=read(root+"/"+s.name+".png");uint32_t w=0,h=0;const uint8_t* raw=nullptr;
            std::unique_ptr<void,decltype(&art3m1s_texture_study_free)> image(art3m1s_texture_study_decode(png.data(),png.size(),&w,&h,&raw),art3m1s_texture_study_free);
            if(!image||w!=s.w||h!=s.h){pass=false;continue;}const size_t n=size_t(w)*h*4;
            uint64_t reference[4]{};bool valid=art3m1s_cache_study_select(raw,n,0,reference)!=0;
            // Verify the exact size prediction outside timing, without retaining
            // an encoded cache or doing any GPU texture upload in this test.
            {const auto p=pack(7,raw,n,128*1024);valid&=p.ok&&p.bytes==reference[2];}
            for(unsigned round=0;round<6;++round)for(unsigned turn=0;turn<2;++turn){
                const unsigned gated=(round+turn)%2,iterations=gated&&n<512*1024?1024:1;
                uint64_t stats[4]{};bool ok=valid;const auto a=now();
                for(unsigned i=0;i<iterations;++i)ok&=art3m1s_cache_study_select(raw,n,int(gated),stats)!=0;
                const auto elapsed=now()-a;
                if(gated&&n<512*1024)ok&=stats[0]==0&&stats[1]==0&&stats[2]==n&&stats[3]==0;
                else ok&=std::memcmp(stats,reference,sizeof(stats))==0;
                pass&=ok;
                std::fprintf(csv,"%s,%u,%d,%u,%u,%u,%llu,%llu,%llu,%llu,%llu,%d\n",s.name,unsigned(n),scePowerGetArmClockFrequency(),round,gated,iterations,
                    (unsigned long long)elapsed,(unsigned long long)stats[0],(unsigned long long)stats[1],(unsigned long long)stats[2],(unsigned long long)stats[3],int(ok));
                std::fflush(csv);
            }
        }
        std::fclose(csv);log("[cache-study-scan] complete pass=%d samples=%u",int(pass),done);return pass;
    }
    const bool coverage=!read(root+"/cache-study.profile").empty();
    const bool threaded=!read(root+"/cache-study.threads").empty();
    const bool grid=threaded&&!read(root+"/cache-study.grid").empty();
    const bool sparse=threaded&&!read(root+"/cache-study.sparse").empty();
    DecodeWorkers workers;
    for(unsigned j=0;j<(grid?3u:sparse?2u:threaded?1u:0u);++j){
        workers.emplace_back(new DecodeWorker);
        if(!workers.back()->valid()){log("[cache-study] worker creation failed");return false;}
    }
    const std::vector<unsigned> clocks=coverage?std::vector<unsigned>{333u}:std::vector<unsigned>{333u,444u};
    // Resume only complete, successful groups from this same fixed demo input.
    // A group is four rounds of four baselines plus 3*3*4*2 cache measurements.
    std::map<std::pair<unsigned,std::string>,std::vector<std::string>> prior;
    const auto inputKey=read(root+"/cache-study.key"),savedKey=read(root+"/cache-results.key");
    if(!inputKey.empty()&&inputKey==savedKey)if(FILE* old=std::fopen((root+"/cache-results.csv").c_str(),"r")){
        char line[1024];while(std::fgets(line,sizeof(line),old)){
            unsigned scene,w,h,cpu;char name[32];
            if(std::sscanf(line,"%u,%31[^,],%u,%u,%u,",&scene,name,&w,&h,&cpu)==5)
                prior[{cpu,name}].push_back(line);
        }std::fclose(old);
    }
    for(auto it=prior.begin();it!=prior.end();){
        FILE* native=std::fopen((root+"/"+it->first.second+".dds").c_str(),"rb");
        const unsigned expected=sparse?28:grid?100:threaded?36:coverage?28:(native?112:108);if(native)std::fclose(native);
        bool valid=it->second.size()==expected;
        for(const auto& line:it->second)valid=valid&&line.size()>=3&&line.compare(line.size()-3,3,",1\n")==0;
        if(!valid)it=prior.erase(it);else ++it;
    }
    FILE* csv=std::fopen((root+"/cache-results.csv").c_str(),"w");if(!csv)return false;
    if(FILE* key=std::fopen((root+"/cache-results.key").c_str(),"wb")){
        std::fwrite(inputKey.data(),1,inputKey.size(),key);std::fclose(key);
    }
    std::fprintf(csv,"scene,sample,width,height,cpu_mhz,codec,route,chunk_bytes,round,png_bytes,raw_bytes,cached_payload_bytes,encode_us,read_us,alloc_us,decode_us,copy_us,seal_us,total_us,max_block_us,scratch_bytes,gpu_alloc_bytes,ok\n");
    for(const auto& group:prior)for(const auto& line:group.second)std::fputs(line.c_str(),csv);
    std::fflush(csv);log("[cache-study] resume_complete_groups=%u",unsigned(prior.size()));
    bool pass=true;unsigned done=0;
    for(unsigned cpu:clocks){
        scePowerSetArmClockFrequency(cpu);scePowerSetGpuClockFrequency(111);
        const unsigned actualCpu=scePowerGetArmClockFrequency();
        log("[cache-study] clock requested=%u actual=%u gpu=%d",cpu,actualCpu,scePowerGetGpuClockFrequency());
        for(const auto& s:samples){
            progress(s.name,actualCpu,++done,unsigned(samples.size()*clocks.size()));
            if(prior.count({actualCpu,s.name}))continue;
            if(cancel()){std::fclose(csv);return false;}
            auto png=read(root+"/"+s.name+".png");uint32_t w=0,h=0;const uint8_t* raw=nullptr;
            std::unique_ptr<void,decltype(&art3m1s_texture_study_free)> image(art3m1s_texture_study_decode(png.data(),png.size(),&w,&h,&raw),art3m1s_texture_study_free);
            if(!image||w!=s.w||h!=s.h){pass=false;log("[cache-study] PNG failed %s",s.name);continue;}
            std::vector<uint8_t> proof(32+size_t((w+63)/64)*((h+63)/64));
            if(!prepare_opacity(w,h,raw,proof.data(),proof.size())){pass=false;continue;}
            const size_t n=size_t(w)*h*4;
            auto dds=read(root+"/"+s.name+".dds");
            // Identical saved opacity proof is used for every path. PNG timing
            // below excludes proof generation, so it is a conservative baseline.
            for(unsigned round=0;round<4;++round){
                auto r=upload(s,raw,proof,0,nullptr,"raw");pass&=r.ok;emit(csv,s,actualCpu,0,"raw",0,round,png.size(),n,0,0,r);
                if(threaded)continue;
                if(!coverage&&!dds.empty()){
                    auto dr=native_upload(s,dds);pass&=dr.ok;emit(csv,s,actualCpu,5,"dds_ram",0,round,png.size(),dds.size(),0,0,dr);
                }
                for(bool disk:{false,true}){
                    const auto a=now();auto bytes=disk?read(root+"/"+s.name+".png"):std::vector<uint8_t>();const auto b=now();
                    const auto* src=disk?bytes.data():png.data();size_t len=disk?bytes.size():png.size();
                    uint32_t pw=0,ph=0;const uint8_t* pixels=nullptr;
                    void* decoded=art3m1s_texture_study_decode(src,len,&pw,&ph,&pixels);const auto c=now();
                    Result pr;if(decoded&&pw==w&&ph==h){pr=upload(s,pixels,proof,0,nullptr,"raw");pr.decode=c-b;pr.total+=c-a;pr.scratch=n;pr.ok&=std::memcmp(raw,pixels,n)==0;}
                    if(decoded)art3m1s_texture_study_free(decoded);pass&=pr.ok;
                    emit(csv,s,actualCpu,4,disk?"png_file":"png_ram",0,round,png.size(),png.size(),0,b-a,pr);
                }
            }
            const std::vector<size_t> chunks=sparse?std::vector<size_t>{128*1024}:grid?std::vector<size_t>{16*1024,32*1024,64*1024,128*1024,256*1024,512*1024}:coverage?std::vector<size_t>{64*1024}:std::vector<size_t>{n,256*1024,64*1024};
            const std::vector<unsigned> codecs=sparse?std::vector<unsigned>{3u,7u}:grid?std::vector<unsigned>{3u}:std::vector<unsigned>{1u,2u,3u,6u};
            for(unsigned mode:codecs)for(size_t chunk:chunks){
                progress(s.name,actualCpu,done,unsigned(samples.size()*clocks.size()));
                if(threaded){
                    // Compare restoration of the identical packed blocks. Encoding and
                    // one-time worker startup are outside the threaded timing comparison.
                    const auto p=pack(mode,raw,n,chunk);if(!p.ok){pass=false;continue;}
                    // Worker buffers are reserved outside restore timing, as for the
                    // existing paired test. Count all selected buffers in scratch_bytes.
                    for(auto& worker:workers)worker->pixels.resize(std::min(chunk,n));
                    for(unsigned round=0;round<4;++round){
                        if(cancel()){std::fclose(csv);return false;}
                        const unsigned variants=sparse?3:grid?4:2;
                        for(unsigned turn=0;turn<variants;++turn){
                            const unsigned index=(round+turn)%variants;
                            const char* route=sparse?(index==0?"staging":index==1?"staging3":"gpu_spans"):(index==0?"staging":index==1?"staging2":index==2?"staging3":"staging4");
                            const auto r=upload(s,raw,proof,mode,&p,route,nullptr,&workers);pass&=r.ok;
                            emit(csv,s,actualCpu,mode,route,unsigned(chunk),round,png.size(),p.bytes,(grid||sparse)?p.encodeUs:0,0,r);
                        }
                    }
                    continue;
                }
                for(unsigned round=0;round<4;++round){
                    progress(s.name,actualCpu,done-1,unsigned(samples.size()*clocks.size()));
                    if(cancel()){std::fclose(csv);return false;}
                    const auto p=pack(mode,raw,n,chunk);if(!p.ok){pass=false;continue;}
                    for(const char* route:{"staging","direct"}){
                        if(coverage&&std::strcmp(route,"direct")==0)continue;
                        const auto r=upload(s,raw,proof,mode,&p,route);pass&=r.ok;
                        emit(csv,s,actualCpu,mode,route,unsigned(chunk),round,png.size(),p.bytes,p.encodeUs,0,r);
                    }
                }
            }
            log("[cache-study] sample=%s cpu=%u complete cumulative_pass=%d",s.name,actualCpu,int(pass));
        }
    }
    std::fclose(csv);log("[cache-study] complete pass=%d csv=%s/cache-results.csv",int(pass),root.c_str());return pass;
}
void draw_image(Texture* t,float offset,float scale,unsigned w,unsigned h,float alpha=1){
    const float x=offset+(440-w*scale)*.5f,y=80;
    Vertex q[]={{x,y,0,0,1,1,1,alpha},{x+w*scale,y,1,0,1,1,1,alpha},{x,y+h*scale,0,1,1,1,1,alpha},{x+w*scale,y+h*scale,1,1,1,1,alpha}};draw_quad(t,q);
}
void manual(const std::string& root,const std::vector<Sample>& samples){
    unsigned scene=0,codec=0,variant=0;bool staging=true;uint32_t prev=SCE_CTRL_CIRCLE;bool dirty=true;
    std::vector<Texture*> reference,restored;size_t rawBytes=0,cached=0;uint64_t enc=0,restore=0;bool ok=true;
    auto clear=[&]{wait();for(auto* t:reference)destroy(t);for(auto* t:restored)destroy(t);reference.clear();restored.clear();};
    for(;;){
        sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);SceCtrlData pad{};sceCtrlPeekBufferPositive(0,&pad,1);const auto pressed=pad.buttons&~prev;prev=pad.buttons;
        if(pressed&SCE_CTRL_CROSS)break;
        if(pressed&SCE_CTRL_RIGHT){codec=(codec+1)%5;dirty=true;}if(pressed&SCE_CTRL_LEFT){codec=(codec+4)%5;dirty=true;}
        if(pressed&SCE_CTRL_DOWN){scene=(scene+1)%3;dirty=true;}if(pressed&SCE_CTRL_UP){scene=(scene+2)%3;dirty=true;}
        if(pressed&SCE_CTRL_CIRCLE){variant^=1;dirty=true;}if(pressed&SCE_CTRL_SQUARE){staging=!staging;dirty=true;}
        if(dirty){
            clear();rawBytes=cached=enc=restore=0;ok=true;
            for(const auto& s:samples)if(s.scene==scene&&(s.role<2||s.role==2+variant)){
                auto png=read(root+"/"+s.name+".png");uint32_t w=0,h=0;const uint8_t* raw=nullptr;void* image=art3m1s_texture_study_decode(png.data(),png.size(),&w,&h,&raw);
                if(!image||w!=s.w||h!=s.h){if(image)art3m1s_texture_study_free(image);ok=false;continue;}
                std::vector<uint8_t> proof(32+size_t((w+63)/64)*((h+63)/64));ok&=prepare_opacity(w,h,raw,proof.data(),proof.size());
                reference.push_back(texture(w,h,raw,proof.data(),proof.size()));Texture* t=nullptr;const size_t n=size_t(w)*h*4;
                if(codec){const unsigned mode=codec==4?6:codec;auto p=pack(mode,raw,n,256*1024);auto r=upload(s,raw,proof,mode,&p,staging?"staging":"direct",&t);cached+=p.bytes;enc+=p.encodeUs;restore+=r.total;ok&=p.ok&&r.ok;}
                else {auto r=upload(s,raw,proof,0,nullptr,"raw",&t);cached+=n;restore+=r.total;ok&=r.ok;}
                rawBytes+=n;restored.push_back(t);art3m1s_texture_study_free(image);
            }
            dirty=false;
        }
        begin();for(unsigned y=80;y<380;y+=24)for(unsigned x=0;x<960;x+=24)rect(float(x),float(y),24,24,((x/24+y/24)&1)?0xff908070:0xff504030);
        unsigned k=0;for(const auto& s:samples)if(s.scene==scene&&(s.role<2||s.role==2+variant)){
            const float scale=std::min(420.f/s.w,300.f/s.h);if(k<reference.size()&&reference[k])draw_image(reference[k],10,scale,s.w,s.h);
            if(k<restored.size()&&restored[k])draw_image(restored[k],490,scale,s.w,s.h);++k;
        }
        char b[160];std::snprintf(b,sizeof(b),"Scene %u   %s   %s",scene+1,modes[codec],staging?"256 KiB staging":"direct GPU decode");menu_text(14,10,22,b);
        menu_text(14,45,20,"PNG reference");menu_text(494,45,20,"Restored cache");
        std::snprintf(b,sizeof(b),"RGBA %.2f MiB -> cache %.2f MiB   exact RGBA: %s",rawBytes/1048576.,cached/1048576.,ok?"PASS":"FAIL");menu_text(14,390,20,b);
        std::snprintf(b,sizeof(b),"Encode %.2f ms   restore+GPU %.2f ms",enc/1000.,restore/1000.);menu_text(14,420,20,b);
        menu_text(14,458,18,"Left/Right: codec   Up/Down: scene   Circle: expression");
        menu_text(14,488,18,"Square: staging/direct   Cross: exit   No production cache changes");end();wait();
    }
    clear();
}
} // namespace cache_study
bool cache_compression_study(const std::string& root,bool automatic){
    const bool coverage=!cache_study::read(root+"/cache-study.profile").empty();
    FILE* f=std::fopen((root+"/cache-study.scene").c_str(),"r");if(!f)return false;
    unsigned n=0;bool valid=std::fscanf(f,"%u",&n)==1&&n>0&&n<=(coverage?512u:24u);std::vector<cache_study::Sample> samples(valid?n:0);
    for(auto& s:samples){valid=valid&&std::fscanf(f,"%u %u %31s %u %u",&s.scene,&s.role,s.name,&s.w,&s.h)==5;
        valid=valid&&s.scene<(coverage?512u:3u)&&s.role<4&&s.w>0&&s.h>0&&s.w<=4096&&s.h<=4096&&size_t(s.w)*s.h*4<=16*1024*1024&&s.name[0]!='/'&&std::strspn(s.name,"abcdefghijklmnopqrstuvwxyz0123456789_-/")==std::strlen(s.name);}
    std::fclose(f);if(!valid)return false;
    const int cpu=scePowerGetArmClockFrequency(),gpu=scePowerGetGpuClockFrequency();startup_validation_display(true);
    const char* glyphs="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 /:.,+->_";
    for(float size:{18.f,20.f,22.f,26.f})menu_prepare(glyphs,size);
    bool ok=true;if(automatic||coverage)ok=cache_study::automatic(root,samples);else cache_study::manual(root,samples);
    scePowerSetArmClockFrequency(cpu);scePowerSetGpuClockFrequency(gpu);return ok;
}
} // namespace direct
