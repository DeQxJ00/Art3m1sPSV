#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "psp2/io/fcntl.h"
#include "../../host/files.h"
static int opened,live,lookups;
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
void *pfs_open_single(const char*p,const char*enc){return (void*)(strstr(p,".001")?"PATCHED":"BASE");}
int pfs_file_size(void*a,const char*p){lookups++;return !strcmp(p,"music.ogg")?strlen(a):-1;}
int pfs_read(void*a,const char*p,uint64_t offset,uint8_t*out,uint32_t n){size_t size=strlen(a);if(offset>=size)return 0;if(n>size-offset)n=size-offset;memcpy(out,(char*)a+offset,n);return n;}
void pfs_close(void*a){}
static void writefile(const char*p,const char*s){FILE*f=fopen(p,"wb");assert(f);fputs(s,f);fclose(f);}
static void verify(HostReadStream*s,const char*expect){char b[32]={0};assert(host_stream_read(s,(uint8_t*)b,31,0)==(int)strlen(expect));assert(!strcmp(b,expect));assert(host_stream_read(s,(uint8_t*)b,1,strlen(expect))==0);assert(host_stream_read(s,(uint8_t*)b,1,-1)==-1);}
static void *worker(void*p){HostReadStream*s=p;for(int i=0;i<1000;i++){char b;assert(host_stream_read(s,(uint8_t*)&b,1,i%7)==1);assert(b=="PATCHED"[i%7]);}return NULL;}
int main(void){
    assert(!mkdir("game",0700));assert(!mkdir("saves",0700));
    writefile("game/root.pfs","");writefile("game/root.pfs.001","");
    assert(host_files_open("game","saves")==2);
    int64_t size;HostReadStream*a=host_stream_open("music.ogg",&size);assert(a&&size==7);verify(a,"PATCHED");
    HostReadStream*b=host_stream_open("music.ogg",&size);assert(b);
    int opens=opened,finds=lookups;pthread_t x,y;pthread_create(&x,NULL,worker,a);pthread_create(&y,NULL,worker,b);pthread_join(x,NULL);pthread_join(y,NULL);
    assert(opened==opens&&lookups==finds);host_stream_close(a);host_stream_close(b);
    writefile("game/music.ogg","LOOSE");a=host_stream_open("music.ogg",&size);verify(a,"LOOSE");host_stream_close(a);
    writefile("saves/music.ogg","SAVE");a=host_stream_open("music.ogg",&size);verify(a,"SAVE");
    // Existing playback keeps its open file; a newly opened stream sees the save replacement.
    assert(host_write("music.ogg",(const uint8_t*)"NEW",3)==3);verify(a,"SAVE");
    b=host_stream_open("music.ogg",&size);verify(b,"NEW");host_stream_close(a);host_stream_close(b);
    assert(host_delete("music.ogg")==0);a=host_stream_open("music.ogg",&size);verify(a,"LOOSE");host_stream_close(a);
    assert(!host_stream_open("../music.ogg",&size));assert(!host_stream_open("abs:music.ogg",&size));assert(!host_stream_open("missing",&size));
    host_stream_close(NULL);host_files_close();assert(live==0);
    puts("PASS: archive precedence, save/loose precedence, EOF/seek, concurrent archive reads, save replacement, invalid paths, zero descriptor leaks; 2000 chunk reads used no additional opens or archive lookups.");
}
