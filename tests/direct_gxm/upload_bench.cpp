// One-shot hardware diagnostic. No game files, saves, clocks or GPU state.
#include "texture_opacity.hpp"
#include <psp2/kernel/clib.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/stat.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" { unsigned int _newlib_heap_size_user=64*1024*1024; }
static constexpr size_t count=960*540,bytes=count*4;
static volatile unsigned consumed;
static uint8_t scratch[16384] __attribute__((aligned(64)));
__attribute__((noinline)) static bool scan(const uint8_t* s){return direct::pixels_are_opaque(s,count);}
__attribute__((noinline)) static bool scalar(const uint8_t* s){
    uint32_t bits=~0u;
    for(size_t i=0;i<count;++i){uint32_t word;std::memcpy(&word,s+i*4,4);bits&=word;}
    return (bits&0xff000000u)==0xff000000u;
}
__attribute__((noinline)) static bool chunked(uint8_t* d,const uint8_t* s,bool cached){
    bool opaque=true;
    for(size_t at=0;at<bytes;at+=sizeof(scratch)){
        const size_t n=std::min(sizeof(scratch),bytes-at);
        if(cached){sceClibMemcpy(scratch,s+at,n);opaque &= direct::pixels_are_opaque(scratch,n/4);sceClibMemcpy(d+at,scratch,n);}
        else {sceClibMemcpy(d+at,s+at,n);opaque &= direct::pixels_are_opaque(s+at,n/4);}
    }return opaque;
}
int main(){
    sceIoMkdir("ux0:data/art3m1s-upload-bench",0777);
    FILE* f=std::fopen("ux0:data/art3m1s-upload-bench/result.log","w");if(!f)return 1;
    auto* source=static_cast<uint8_t*>(std::malloc(bytes));
    auto uid=sceKernelAllocMemBlock("upload-bench",SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,0x200000,nullptr);
    uint8_t* dest=nullptr;if(uid>=0)sceKernelGetMemBlockBase(uid,reinterpret_cast<void**>(&dest));
    if(!source||!dest){std::fprintf(f,"allocation failed %08x\n",unsigned(uid));std::fclose(f);return 2;}
    for(size_t i=0;i<bytes;++i)source[i]=(i%4==3)?255:uint8_t(i*31);
    for(auto* p:{source,scratch,dest}){
        SceKernelMemBlockInfo info{};info.size=sizeof(info);
        int r=sceKernelGetMemBlockInfoByAddr(p,&info);
        std::fprintf(f,"memory address=%p query=%d type=%08x\n",p,r,unsigned(info.type));
    }
    for(unsigned repetition=0;repetition<4;++repetition){
        for(unsigned method=0;method<7;++method){
            asm volatile("":::"memory");
            auto start=sceKernelGetProcessTimeWide();bool result=true;
            switch(method){
            case 0:sceClibMemcpy(dest,source,bytes);break;
            case 1:result=scan(source);break;
            case 2:result=scalar(source);break;
            case 3:result=chunked(dest,source,false);break;
            case 4:result=chunked(dest,source,true);break;
            case 5:sceClibMemcpy(dest,source,bytes);result=scan(source);break;
            case 6:result=direct::certify_texture_opacity(source,count);break;
            }
            auto elapsed=sceKernelGetProcessTimeWide()-start;consumed+=unsigned(result);
            std::fprintf(f,"trial=%u method=%u us=%llu opaque=%d\n",repetition,method,
                (unsigned long long)elapsed,int(result));std::fflush(f);
        }
    }
    std::fprintf(f,"DONE checksum=%u\n",unsigned(consumed));std::fclose(f);
    sceKernelFreeMemBlock(uid);std::free(source);sceKernelExitProcess(0);return 0;
}
