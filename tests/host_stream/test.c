#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sched.h>
#include "psp2/io/fcntl.h"
#include "../../host/files.h"
static int opened,live,lookups;
static atomic_int large_reads,max_large_chunk,audio_between,fail_large_read;
enum { LARGE_SIZE=2*1024*1024+123 };
void host_loading_show(int stage,const char *detail){(void)stage;(void)detail;}
int sceIoOpen(const char*p,int flags,int mode){opened++;int fd=open(p,flags,mode);if(fd>=0)live++;return fd;}
int sceIoClose(int fd){live--;return close(fd);}
int64_t sceIoLseek(int fd,int64_t o,int w){return lseek(fd,o,w);}
int sceIoRead(int fd,void*p,int n){return read(fd,p,n);}
int sceIoWrite(int fd,const void*p,int n){return write(fd,p,n);}
int sceIoMkdir(const char*p,int m){return mkdir(p,m);}
int sceIoRemove(const char*p){return unlink(p);}
int sceIoRename(const char*a,const char*b){return rename(a,b);}
int sceIoGetstat(const char*p,SceIoStat*s){struct stat st;int r=stat(p,&st);if(!r)s->st_size=st.st_size;return r;}
void *pfs_open_single(const char*p,const char*enc){assert(!strcmp(enc,"auto"));return (void*)(strstr(p,".001")?"PATCHED":"BASE");}
static const char *table_data(const char *p){
    if(!strcmp(p,"system/table/list_android.tbl"))return "init={system={image_path='image/_hd/',blur_path='mb/',blur_exp='.glsl'}}";
    if(!strcmp(p,"system/table/list_android_cn.tbl"))return "lang={title='CN'}";
    if(!strcmp(p,"system/table/list_android_custom.tbl"))return "lang={title='CUSTOM'}";
    if(!strcmp(p,"system/table/list_android_blocked.tbl"))return "lang={title='BLOCKED'}";
    if(!strcmp(p,"system/table/list_android_writefail.tbl"))return "lang={title='WRITEFAIL'}";
    if(!strcmp(p,"system/table/list_windows_native.tbl"))return "lang={title='NATIVE'}";
    if(!strcmp(p,"system/shader/pc/reset.hlsl"))return "shader source";
    return NULL;
}
int pfs_file_size(void*a,const char*p){lookups++;const char *t=table_data(p);if(t)return strlen(t);if(!strcmp(p,"large.png"))return LARGE_SIZE;return !strcmp(p,"music.ogg")?strlen(a):-1;}
int pfs_read(void*a,const char*p,uint64_t offset,uint8_t*out,uint32_t n){
    const char *t=table_data(p);if(t){size_t size=strlen(t);if(offset>=size)return 0;if(n>size-offset)n=size-offset;memcpy(out,t+offset,n);return n;}
    if(!strcmp(p,"large.png")){
        if(atomic_load(&fail_large_read)&&offset>=32768)return -1;
        atomic_fetch_add(&large_reads,1);if((int)n>atomic_load(&max_large_chunk))atomic_store(&max_large_chunk,n);
        if(offset>=LARGE_SIZE)return 0;if(n>LARGE_SIZE-offset)n=LARGE_SIZE-offset;
        for(uint32_t i=0;i<n;i++)out[i]=(offset+i)%251;
        usleep(1000);return n;
    }
    int chunks=atomic_load(&large_reads);if(chunks>0&&chunks<65)atomic_fetch_add(&audio_between,1);
    size_t size=strlen(a);if(offset>=size)return 0;if(n>size-offset)n=size-offset;memcpy(out,(char*)a+offset,n);return n;
}
void pfs_close(void*a){}
static void writefile(const char*p,const char*s){FILE*f=fopen(p,"wb");assert(f);fputs(s,f);fclose(f);}
static void verify(HostReadStream*s,const char*expect){char b[32]={0};assert(host_stream_read(s,(uint8_t*)b,31,0)==(int)strlen(expect));assert(!strcmp(b,expect));assert(host_stream_read(s,(uint8_t*)b,1,strlen(expect))==0);assert(host_stream_read(s,(uint8_t*)b,1,-1)==-1);}
static void *worker(void*p){HostReadStream*s=p;for(int i=0;i<1000;i++){char b;assert(host_stream_read(s,(uint8_t*)&b,1,i%7)==1);assert(b=="PATCHED"[i%7]);}return NULL;}
static void *audio_during_image(void*p){while(!atomic_load(&large_reads))sched_yield();return worker(p);}
int main(void){
    assert(!mkdir("game",0700));assert(!mkdir("saves",0700));
    writefile("game/root.pfs","");writefile("game/root.pfs.001","");
    assert(host_files_open("game","saves")==2);
#ifdef DIRECT_BUILTIN_EFFECTS
    assert(host_files_prepare_platform_tables("WINDOWS",960,540)==0);
    uint8_t table[4096]={0};const char *main_table="system/table/list_windows.tbl";
    int table_size=host_read(main_table,NULL,0,-1);assert(table_size>0&&table_size<4096);
    assert(host_read(main_table,table,table_size,0)==table_size);
    assert(strstr((char*)table,"list_android.tbl")&&strstr((char*)table,"image/_hd/")&&strstr((char*)table,"blur_path='pc/'"));
    assert(strstr((char*)table,"init.game_scale={960,540}")&&strstr((char*)table,"init.game_width=960")&&strstr((char*)table,"init.game_height=540"));
    // Reproduce the previous generated format, then upgrade its dimensions.
    char *footer=strstr((char*)table,"\n-- art3m1s vita-resolution");assert(footer);*footer=0;
    writefile("game/system/table/list_windows.tbl",(char*)table);
    assert(host_files_prepare_platform_tables("WINDOWS",960,544)==1);
    assert(host_files_prepare_platform_tables("WINDOWS",960,544)==0);
    memset(table,0,sizeof(table));assert(host_read(main_table,table,4095,0)>0);
    assert(strstr((char*)table,"init.game_scale={960,544}")&&strstr((char*)table,"init.game_height=544"));
    assert(!mkdir("game/system/table/list_windows.tbl.art3m1s-table.tmp",0700));
    assert(host_files_prepare_platform_tables("WINDOWS",960,540)==-1);
    memset(table,0,sizeof(table));assert(host_read(main_table,table,4095,0)>0);
    assert(strstr((char*)table,"init.game_scale={960,544}"));
    assert(!rmdir("game/system/table/list_windows.tbl.art3m1s-table.tmp"));
    assert(host_files_prepare_platform_tables("WINDOWS",960,540)==1);
    assert(host_read("system/table/list_windows_cn.tbl",table,4095,0)>0);
    assert(strstr((char*)table,"lang={title='CN'}"));
    writefile("game/system/table/list_windows.tbl","USER EDIT");
    assert(host_files_prepare_platform_tables("WINDOWS",960,544)==0);
    assert(host_read(main_table,NULL,0,-1)==9);
    writefile("game/system/table/list_windows_custom.tbl","KEEP");
    assert(host_read("system/table/list_windows_custom.tbl",NULL,0,-1)==4);
    assert(host_read("system/table/list_windows_native.tbl",NULL,0,-1)==(int)strlen("lang={title='NATIVE'}"));
    assert(access("game/system/table/list_windows_native.tbl",F_OK)!=0);
    assert(!mkdir("game/system/table/list_windows_blocked.tbl",0700));
    host_read("system/table/list_windows_blocked.tbl",table,4095,0);
    struct stat blocked;assert(!stat("game/system/table/list_windows_blocked.tbl",&blocked)&&S_ISDIR(blocked.st_mode));
    assert(!mkdir("game/system/table/list_windows_writefail.tbl.art3m1s-table.tmp",0700));
    assert(host_read("system/table/list_windows_writefail.tbl",NULL,0,-1)<0);
    assert(access("game/system/table/list_windows_writefail.tbl",F_OK)!=0);
    assert(!rmdir("game/system/table/list_windows_writefail.tbl.art3m1s-table.tmp"));
    assert(host_read("system/table/list_windows_writefail.tbl",NULL,0,-1)>0);
    assert(host_read("system/table/list_windows_missing.tbl",NULL,0,-1)<0);
    assert(access("game/system/table/list_windows_missing.tbl",F_OK)!=0);
    assert(host_read("system/table/list_other.tbl",NULL,0,-1)<0);
    assert(host_read("system/table/list_windows_../escape.tbl",NULL,0,-1)<0);
    assert(access("game/system/table/list_windows.tbl.art3m1s-table.tmp",F_OK)!=0);
    assert(host_files_prepare_platform_tables("VITA",960,540)==0);
    // The runtime now requests Vita tables for every game by default.
    memset(table,0,sizeof(table));
    assert(host_read("system/table/list_vita.tbl",table,4095,0)>0);
    assert(strstr((char*)table,"list_android.tbl")&&strstr((char*)table,"image/_hd/"));
    assert(strstr((char*)table,"init.game_scale={960,540}"));
    memset(table,0,sizeof(table));
    assert(host_read("system/table/list_vita_cn.tbl",table,4095,0)>0);
    assert(strstr((char*)table,"list_android_cn.tbl")&&strstr((char*)table,"title='CN'"));
    assert(!strstr((char*)table,"game_scale"));
    writefile("game/system/table/list_vita.tbl","-- art3m1s platform-table fallback: system/table/list_android.tbl\nUSER MODIFIED");
    assert(host_files_prepare_platform_tables("VITA",960,544)==0);
    memset(table,0,sizeof(table));assert(host_read("system/table/list_vita.tbl",table,4095,0)>0);
    assert(strstr((char*)table,"USER MODIFIED")&&!strstr((char*)table,"game_scale"));
    assert(host_files_prepare_platform_tables("OTHER",960,540)==-1);
    assert(host_files_prepare_platform_tables("VITA",0,540)==-1);
#endif
    int64_t size;HostReadStream*a=host_stream_open("music.ogg",&size);assert(a&&size==7);verify(a,"PATCHED");
    HostReadStream*b=host_stream_open("music.ogg",&size);assert(b);
    int opens=opened,finds=lookups;pthread_t x,y;pthread_create(&x,NULL,worker,a);pthread_create(&y,NULL,worker,b);pthread_join(x,NULL);pthread_join(y,NULL);
    assert(opened==opens&&lookups==finds);
    uint8_t *image=malloc(LARGE_SIZE+100);assert(image);
    opens=opened;finds=lookups;int live_before=live;
    pthread_create(&x,NULL,audio_during_image,a);
    assert(host_read("large.png",image,LARGE_SIZE+100,0)==LARGE_SIZE);
    pthread_join(x,NULL);
    assert(opened-opens==2&&lookups-finds==1&&live==live_before);
    for(int i=0;i<LARGE_SIZE;i++)assert(image[i]==i%251);
    assert(atomic_load(&max_large_chunk)<=32768&&atomic_load(&audio_between)>0);
    assert(host_read("large.png",image,70000,17)==70000);
    for(int i=0;i<70000;i++)assert(image[i]==(i+17)%251);
    assert(host_read("large.png",image,70000,LARGE_SIZE)==0);
    assert(host_read("large.png",image,70000,LARGE_SIZE+99)==0);
    atomic_store(&fail_large_read,1);
    assert(host_read("large.png",image,70000,0)==-1&&live==live_before);
    atomic_store(&fail_large_read,0);
    assert(host_read("missing",image,70000,0)==-1);
    // A large-capacity read must retain overlay precedence and close its
    // private descriptor even when the selected loose file ends immediately.
    writefile("game/large.png","LOOSE IMAGE");
    opens=opened;finds=lookups;
    assert(host_read("large.png",image,70000,0)==11&&!memcmp(image,"LOOSE IMAGE",11));
    assert(opened-opens==2&&lookups==finds&&live==live_before);
    FILE *large=fopen("game/large.png","wb");assert(large);
    for(int i=0;i<100003;i++)assert(fputc(i%239,large)!=EOF);
    assert(!fclose(large));opens=opened;
    assert(host_read("large.png",image,120000,17)==99986);
    for(int i=0;i<99986;i++)assert(image[i]==(i+17)%239);
    assert(opened-opens==2&&lookups==finds&&live==live_before);
    writefile("saves/large.png","SAVE IMAGE");
    opens=opened;
    assert(host_read("large.png",image,70000,0)==10&&!memcmp(image,"SAVE IMAGE",10));
    assert(opened-opens==1&&lookups==finds&&live==live_before);
    assert(host_read("large.png",image,70000,INT64_MAX)==0);
    assert(host_read("../large.png",image,70000,0)==-1);
#ifdef DIRECT_BUILTIN_EFFECTS
    // The streaming fast path must not bypass generated platform tables.
    assert(!unlink("game/system/table/list_windows_cn.tbl"));
    int recovered=host_read("system/table/list_windows_cn.tbl",image,70000,0);
    assert(recovered>0&&recovered<70000);image[recovered]=0;
    assert(strstr((char*)image,"lang={title='CN'}"));
#endif
    assert(live==live_before);free(image);
    host_stream_close(a);host_stream_close(b);
    writefile("game/music.ogg","LOOSE");a=host_stream_open("music.ogg",&size);verify(a,"LOOSE");host_stream_close(a);
    writefile("saves/music.ogg","SAVE");a=host_stream_open("music.ogg",&size);verify(a,"SAVE");
    // Existing playback keeps its open file; a newly opened stream sees the save replacement.
    assert(host_write("music.ogg",(const uint8_t*)"NEW",3)==3);verify(a,"SAVE");
    b=host_stream_open("music.ogg",&size);verify(b,"NEW");host_stream_close(a);host_stream_close(b);
    assert(host_delete("music.ogg")==0);a=host_stream_open("music.ogg",&size);verify(a,"LOOSE");host_stream_close(a);
    assert(!host_stream_open("../music.ogg",&size));assert(!host_stream_open("abs:music.ogg",&size));assert(!host_stream_open("missing",&size));
    host_stream_close(NULL);host_files_close();assert(live==0);
    puts("PASS: archive precedence, save/loose precedence, EOF/seek, concurrent archive reads, save replacement, invalid paths, zero descriptor leaks; large image lookup once, bytes/offsets/EOF/error/overlay/table recovery verified with interleaved audio and a 32 KiB maximum lock read.");
}
