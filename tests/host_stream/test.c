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
static atomic_int large_reads,max_large_chunk,audio_between;
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
int pfs_file_size(void*a,const char*p){lookups++;if(!strcmp(p,"large.png"))return LARGE_SIZE;return !strcmp(p,"music.ogg")?strlen(a):-1;}
int pfs_read(void*a,const char*p,uint64_t offset,uint8_t*out,uint32_t n){
    if(!strcmp(p,"large.png")){
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
    int64_t size;HostReadStream*a=host_stream_open("music.ogg",&size);assert(a&&size==7);verify(a,"PATCHED");
    HostReadStream*b=host_stream_open("music.ogg",&size);assert(b);
    int opens=opened,finds=lookups;pthread_t x,y;pthread_create(&x,NULL,worker,a);pthread_create(&y,NULL,worker,b);pthread_join(x,NULL);pthread_join(y,NULL);
    assert(opened==opens&&lookups==finds);
    uint8_t *image=malloc(LARGE_SIZE+100);assert(image);
    pthread_create(&x,NULL,audio_during_image,a);
    assert(host_read("large.png",image,LARGE_SIZE+100,0)==LARGE_SIZE);
    pthread_join(x,NULL);
    for(int i=0;i<LARGE_SIZE;i++)assert(image[i]==i%251);
    assert(atomic_load(&max_large_chunk)<=32768&&atomic_load(&audio_between)>0);
    assert(host_read("large.png",image,70000,17)==70000);
    for(int i=0;i<70000;i++)assert(image[i]==(i+17)%251);
    assert(host_read("large.png",image,70000,LARGE_SIZE)==0);
    assert(host_read("missing",image,70000,0)==-1);free(image);
    host_stream_close(a);host_stream_close(b);
    writefile("game/music.ogg","LOOSE");a=host_stream_open("music.ogg",&size);verify(a,"LOOSE");host_stream_close(a);
    writefile("saves/music.ogg","SAVE");a=host_stream_open("music.ogg",&size);verify(a,"SAVE");
    // Existing playback keeps its open file; a newly opened stream sees the save replacement.
    assert(host_write("music.ogg",(const uint8_t*)"NEW",3)==3);verify(a,"SAVE");
    b=host_stream_open("music.ogg",&size);verify(b,"NEW");host_stream_close(a);host_stream_close(b);
    assert(host_delete("music.ogg")==0);a=host_stream_open("music.ogg",&size);verify(a,"LOOSE");host_stream_close(a);
    assert(!host_stream_open("../music.ogg",&size));assert(!host_stream_open("abs:music.ogg",&size));assert(!host_stream_open("missing",&size));
    host_stream_close(NULL);host_files_close();assert(live==0);
    puts("PASS: archive precedence, save/loose precedence, EOF/seek, concurrent archive reads, save replacement, invalid paths, zero descriptor leaks; large image bytes/offsets/EOF verified with interleaved audio and a 32 KiB maximum lock read.");
}
