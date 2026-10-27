"""Exercise the production decoder allocator with simulated fragmented CDRAM."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'vendor/ffmpeg/libavcodec/vitadec_video.c').read_text()
params = source[source.index('typedef struct VitaDecodeBufferAllocParams {'):]
params = params[:params.index('} VitaDecodeBufferAllocParams;') + len('} VitaDecodeBufferAllocParams;')]
start = source.index('static bool do_mem_alloc(VitaDecodeBufferAllocParams *p)\n{')
end = source.index('\nstatic void __attribute__', start)
assert 'p.allow_phy_fallback = i == VITA_DECODE_BUFFER_TYPE_CODEC_MEM;' in source
stub = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
typedef int SceUID;
typedef struct { int size,attr,alignment; } SceKernelAllocMemBlockOpt;
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW 2
#define VITA_DECODE_MEM_BLOCK_TYPE 1
#define VITA_DECODE_MEM_BLOCK_SIZE_ALIGN (256*1024)
#define VITA_DECODE_MEM_BLOCK_NAME "test"
#define SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT 1
#define AV_LOG_INFO 1
#define AV_LOG_ERROR 2
#define FFALIGN(x,a) (((x)+(a)-1)&~((a)-1))
#define av_log(...) ((void)0)
static int first,second,base_result,calls,frees,last_size;
static int sceKernelAllocMemBlock(const char* name,int type,int size,SceKernelAllocMemBlockOpt* opt){
    (void)name;last_size=size;++calls;
    if(calls==1){assert(type==1);assert(opt&&opt->alignment==1024*1024);return first;}
    assert(calls==2&&type==2&&opt==NULL&&size%(1024*1024)==0);return second;
}
static int sceKernelGetMemBlockBase(int uid,void** out){assert(uid>0);*out=(void*)0x1000;return base_result;}
static void sceKernelFreeMemBlock(int uid){assert(uid>0);++frees;}
'''
checks = r'''
static void test(int a,int b,int base,bool fallback,int bytes,bool expected,int count,int freed){
    first=a;second=b;base_result=base;calls=frees=0;int uid=-1;void* ptr=NULL;
    VitaDecodeBufferAllocParams p={.mb=&uid,.ptr=&ptr,.size=bytes,.alignment=1024*1024,.allow_phy_fallback=fallback};
    assert(do_mem_alloc(&p)==expected);assert(calls==count&&frees==freed);
    if(expected){assert(uid==(count==1?a:b)&&ptr==(void*)0x1000);assert(p.size==last_size);}
    else assert(uid==-1&&ptr==NULL);
}
int main(void){
    test(7,-1,0,true,6*1024*1024,true,1,0);
    test(-1,8,0,true,6*1024*1024,true,2,0);
    test(-1,8,0,true,5*1024*1024+1,true,2,0);
    test(-1,-2,0,true,6*1024*1024,false,2,0);
    test(-1,8,-1,true,6*1024*1024,false,2,1);
    test(7,8,-1,true,6*1024*1024,false,1,1);
    test(-1,8,0,false,6*1024*1024,false,1,0);
    test(-1,8,0,true,8*1024*1024+1,false,1,0);
    puts("PASS production workspace allocator: CDRAM, PHY fallback, alignment, cap and failure cleanup");
}
'''
with tempfile.TemporaryDirectory() as folder:
    path = Path(folder)
    (path / 'test.c').write_text(stub + params + source[start:end] + checks)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Wno-unused-variable',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
