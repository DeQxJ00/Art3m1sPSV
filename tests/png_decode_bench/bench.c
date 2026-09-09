#include <png.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/stat.h>
#include <psp2/power.h>
#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <setjmp.h>
unsigned int _newlib_heap_size_user=192*1024*1024;
unsigned int sceUserMainThreadStackSize=512*1024;
static uint32_t* display_pixels;
static void progress(unsigned done,unsigned total,int finished){
    sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
    sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_OLED_OFF);
    if(!display_pixels)return;
    unsigned width=total?800*done/total:0;
    for(unsigned y=230;y<310;y++)for(unsigned x=80;x<880;x++)
        display_pixels[y*1024+x]=x<80+width?(finished?0xff55bb44:0xffffaa33):0xff383028;
}
static void display_init(void){
    int uid=sceKernelAllocMemBlock("png-bench-display",SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,0x240000,NULL);
    if(uid<0)return;
    if(sceKernelGetMemBlockBase(uid,(void**)&display_pixels)<0)return;
    for(unsigned i=0;i<1024*544;i++)display_pixels[i]=0xff201810;
    SceDisplayFrameBuf frame={0};frame.size=sizeof(frame);frame.base=display_pixels;frame.pitch=1024;
    frame.pixelformat=SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;frame.width=960;frame.height=544;
    sceDisplaySetFrameBuf(&frame,SCE_DISPLAY_SETBUF_NEXTFRAME);
}
extern int bench_rust_decode(const uint8_t*,size_t,uint8_t**,size_t*,uint32_t*,uint32_t*,size_t*,int);
extern void bench_rust_free(uint8_t*,size_t,size_t);
typedef struct {const uint8_t* data;size_t size,pos,live,peak;int track;uint8_t* output;} State;
typedef union {size_t size;long double alignment;} Header;
static void* allocate(png_structp png,png_alloc_size_t size){
    State* s=png_get_mem_ptr(png);Header* p=malloc(sizeof(Header)+size);if(!p)return NULL;p->size=size;
    if(s->track){s->live+=size;if(s->live>s->peak)s->peak=s->live;}return p+1;
}
static void deallocate(png_structp png,void* data){if(!data)return;State* s=png_get_mem_ptr(png);Header* p=(Header*)data-1;if(s->track)s->live-=p->size;free(p);}
static void read_data(png_structp png,png_bytep out,png_size_t count){State*s=png_get_io_ptr(png);if(count>s->size-s->pos)png_error(png,"truncated");memcpy(out,s->data+s->pos,count);s->pos+=count;}
static void error_fn(png_structp png,png_const_charp message){(void)message;png_longjmp(png,1);}
static void warning_fn(png_structp png,png_const_charp message){(void)png;(void)message;}
static int libpng_decode(const uint8_t* data,size_t size,uint8_t** out,uint32_t* w,uint32_t* h,size_t* peak,int track){
    State *s=calloc(1,sizeof(*s));if(!s)return -1;s->data=data;s->size=size;s->track=track;
    png_structp png=png_create_read_struct_2(PNG_LIBPNG_VER_STRING,NULL,error_fn,warning_fn,s,allocate,deallocate);if(!png){free(s);return -1;}
    png_infop info=png_create_info_struct(png);if(!info){png_destroy_read_struct(&png,NULL,NULL);free(s);return -1;}
    if(setjmp(png_jmpbuf(png))){free(s->output);png_destroy_read_struct(&png,&info,NULL);free(s);return -1;}
    png_set_read_fn(png,s,read_data);png_read_info(png,info);
    *w=png_get_image_width(png,info);*h=png_get_image_height(png,info);
    if(!*w||!*h||*w>8192||*h>8192||(uint64_t)*w**h*4>32*1024*1024)png_error(png,"dimensions");
    int color=png_get_color_type(png,info),depth=png_get_bit_depth(png,info);
    int trns=png_get_valid(png,info,PNG_INFO_tRNS);
    if(color==PNG_COLOR_TYPE_PALETTE)png_set_palette_to_rgb(png);
    if(color==PNG_COLOR_TYPE_GRAY&&depth<8)png_set_expand_gray_1_2_4_to_8(png);
    if(trns)png_set_tRNS_to_alpha(png);
    if(depth==16)png_set_scale_16(png);
    if(color==PNG_COLOR_TYPE_GRAY||color==PNG_COLOR_TYPE_GRAY_ALPHA)png_set_gray_to_rgb(png);
    if(!(color&PNG_COLOR_MASK_ALPHA)&&!trns)png_set_add_alpha(png,255,PNG_FILLER_AFTER);
    int passes=png_set_interlace_handling(png);png_read_update_info(png,info);
    size_t stride=(size_t)*w*4,bytes=stride**h;
    if(png_get_rowbytes(png,info)!=stride)png_error(png,"stride");
    s->output=calloc(1,bytes);if(!s->output)png_error(png,"output");
    if(track){s->live+=bytes;if(s->live>s->peak)s->peak=s->live;}
    for(int pass=0;pass<passes;pass++)for(uint32_t y=0;y<*h;y++)png_read_row(png,s->output+y*stride,NULL);
    png_read_end(png,info);*out=s->output;*peak=s->peak;png_destroy_read_struct(&png,&info,NULL);free(s);return 0;
}
static uint8_t* source(const char* name,size_t* size){char path[256];snprintf(path,sizeof(path),"ux0:data/art3m1s-png-bench/%s",name);FILE*f=fopen(path,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);if(n<=0||n>16*1024*1024){fclose(f);return NULL;}uint8_t*p=malloc(n);if(!p){fclose(f);return NULL;}if(fread(p,1,n,f)!=(size_t)n){free(p);p=NULL;}fclose(f);*size=n;return p;}
int main(void){
    int suspend_lock=sceKernelPowerLock(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
    int oled_lock=sceKernelPowerLock(SCE_KERNEL_POWER_TICK_DISABLE_OLED_OFF);
    display_init();progress(0,1,0);
    sceIoMkdir("ux0:data/art3m1s-png-bench",0777);
    FILE*f=fopen("ux0:data/art3m1s-png-bench/result.log","w");if(!f)return 1;setvbuf(f,NULL,_IONBF,0);
    scePowerSetArmClockFrequency(333);scePowerSetBusClockFrequency(222);scePowerSetGpuClockFrequency(111);
    fprintf(f,"BEGIN libpng=%s zlib=%s cpu=%d bus=%d gpu=%d tracking=separate-pass timing_us=decode-no-IO-no-hash-no-output-free\n",PNG_LIBPNG_VER_STRING,ZLIB_VERSION,scePowerGetArmClockFrequency(),scePowerGetBusClockFrequency(),scePowerGetGpuClockFrequency());
    fprintf(f,"DISPLAY active=%d suspend_lock=%d oled_lock=%d\n",display_pixels!=NULL,suspend_lock,oled_lock);
    const char* names[]={"pal8-opaque.png","pal8-trns.png","pal8-large.png","rgba32.png","rgba16.png","background.png","portrait.png","rgba32-game.png"};int failures=0;
    for(unsigned file=0;file<sizeof(names)/sizeof(names[0]);file++){
        size_t n=0;uint8_t* data=source(names[file],&n);if(!data){fprintf(f,"MISSING %s\n",names[file]);failures++;continue;}
        uint8_t *r=NULL,*p=NULL;uint32_t rw=0,rh=0,pw=0,ph=0;size_t capacity=0,rpeak=0,ppeak=0;
        int re=bench_rust_decode(data,n,&r,&capacity,&rw,&rh,&rpeak,1);int pe=libpng_decode(data,n,&p,&pw,&ph,&ppeak,1);
        int equal=!re&&!pe&&rw==pw&&rh==ph&&!memcmp(r,p,(size_t)rw*rh*4);
        fprintf(f,"PIXELS %s equal=%d rust_error=%d png_error=%d width=%u height=%u encoded=%u rust_peak=%u libpng_peak=%u\n",names[file],equal,re,pe,rw,rh,(unsigned)n,(unsigned)rpeak,(unsigned)ppeak);
        if(r)bench_rust_free(r,(size_t)rw*rh*4,capacity);free(p);
        if(!equal){failures++;free(data);continue;}
        for(int round=0;round<10;round++)for(int j=0;j<2;j++){
            progress(file*20+round*2+j,160,0); // Outside the measured interval.
            int which=(round+j)&1;uint8_t* output=NULL;size_t cap=0,peak=0;uint32_t w=0,h=0;
            uint64_t start=sceKernelGetProcessTimeWide();
            int code=which?libpng_decode(data,n,&output,&w,&h,&peak,0):bench_rust_decode(data,n,&output,&cap,&w,&h,&peak,0);
            uint64_t us=sceKernelGetProcessTimeWide()-start;
            fprintf(f,"TIME %s round=%d decoder=%s us=%llu ok=%d\n",names[file],round,which?"libpng":"rust",(unsigned long long)us,code==0);
            if(!code){if(which)free(output);else bench_rust_free(output,(size_t)w*h*4,cap);}else failures++;
        }
        free(data);
    }
    progress(1,1,1);fprintf(f,"DONE failures=%d\n",failures);fclose(f);
    // Keep a visible completed screen until the controller restores the application.
    sceKernelDelayThread(30000000);
    if(suspend_lock==0)sceKernelPowerUnlock(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
    if(oled_lock==0)sceKernelPowerUnlock(SCE_KERNEL_POWER_TICK_DISABLE_OLED_OFF);
    sceKernelExitProcess(0);return 0;
}
