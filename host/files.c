#include "files.h"
#include "launcher.h"
#include "load_timing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <pthread.h>
#include <unistd.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
extern void *pfs_open_single(const char *, const char *);
extern int pfs_file_size(void *, const char *);
extern int pfs_read(void *, const char *, uint64_t, uint8_t *, uint32_t);
extern void pfs_close(void *);
static char game_root[512], saves[512];
static void *archives[64];
static int archive_count;
static pthread_mutex_t files_mutex=PTHREAD_MUTEX_INITIALIZER;
struct HostReadStream { int fd; void *archive; int64_t size; char path[512]; };
static int valid_path(const char *path) {
    return path && *path && !strchr(path, ':') && path[0] != '/' && !strstr(path, "..");
}
static void normalize(char *out, const char *path) {
    snprintf(out, 512, "%s", path);
    for (char *p=out; *p; ++p) if (*p=='\\') *p='/';
}
static int disk_read(const char *root, const char *path, uint8_t *out, int cap, int64_t offset) {
    char full[1024]; snprintf(full,sizeof(full),"%s/%s",root,path);
    FILE *f=fopen(full,"rb"); if (!f) return -1;
    if (offset < 0) { fseek(f,0,SEEK_END); long size=ftell(f); fclose(f); return size; }
    if (!out || cap <= 0 || fseek(f,(long)offset,SEEK_SET)) { fclose(f); return -1; }
    int n=fread(out,1,cap,f); fclose(f); return n;
}
static int compare_names(const void *a,const void *b) { return strcmp(*(char **)a,*(char **)b); }
HostReadStream *host_stream_open(const char *path,int64_t *size) {
    if(!valid_path(path)||strlen(path)>=512||!size)return NULL;
    HostReadStream *s=calloc(1,sizeof(*s));if(!s)return NULL;
    normalize(s->path,path);s->fd=-1;s->size=-1;
    uint64_t started=host_load_clock();
    pthread_mutex_lock(&files_mutex);
    uint64_t acquired=host_load_clock();
    const char *roots[]={saves,game_root};
    for(int i=0;i<2;i++){
        char full[1024];snprintf(full,sizeof(full),"%s/%s",roots[i],s->path);
        int fd=sceIoOpen(full,SCE_O_RDONLY,0);if(fd<0)continue;
        int64_t length=sceIoLseek(fd,0,SCE_SEEK_END);
        if(length>=0){s->fd=fd;s->size=length;break;}
        sceIoClose(fd);
    }
    if(s->fd<0)for(int i=archive_count-1;i>=0;i--){
        int length=pfs_file_size(archives[i],s->path);
        if(length>=0){s->archive=archives[i];s->size=length;break;}
    }
    pthread_mutex_unlock(&files_mutex);
    host_load_report("stream-open",path,started,acquired,host_load_clock(),s->size>=0?0:-1);
    if(s->size<0){free(s);return NULL;}
    *size=s->size;return s;
}
int host_stream_read(HostReadStream *s,uint8_t *out,int cap,int64_t offset){
    if(!s||!out||cap<=0||offset<0||offset>s->size)return -1;
    if(offset==s->size)return 0;
    if((int64_t)cap>s->size-offset)cap=(int)(s->size-offset);
    if(s->fd>=0){
        // Each demuxer owns its descriptor and position; no global archive lock.
        if(sceIoLseek(s->fd,offset,SCE_SEEK_SET)!=offset)return -1;
        return sceIoRead(s->fd,out,cap);
    }
    uint64_t started=host_load_clock();pthread_mutex_lock(&files_mutex);uint64_t acquired=host_load_clock();
    int result=pfs_read(s->archive,s->path,offset,out,cap);
    pthread_mutex_unlock(&files_mutex);
    host_load_report("archive-stream-read",s->path,started,acquired,host_load_clock(),result);return result;
}
void host_stream_close(HostReadStream *s){if(s){if(s->fd>=0)sceIoClose(s->fd);free(s);}}
int host_files_open(const char *root,const char *save_root) {
    snprintf(game_root,sizeof(game_root),"%s",root); snprintf(saves,sizeof(saves),"%s",save_root);
    sceIoMkdir(saves,0777);
    DIR *dir=opendir(root); if (!dir) return -1;
    char *names[64]; int count=0; struct dirent *ent;
    while ((ent=readdir(dir)) && count<64) {
        const char *suffix=strstr(ent->d_name,".pfs");
        if (!suffix) continue;
        suffix+=4;
        if (*suffix) {
            if (strlen(suffix)!=4 || suffix[0]!='.' || strspn(suffix+1,"0123456789")!=3) continue;
        }
        names[count++]=strdup(ent->d_name);
    }
    closedir(dir); qsort(names,count,sizeof(char *),compare_names);
    for (int i=0;i<count;i++) {
        char progress[128];snprintf(progress,sizeof(progress),"PFS %d / %d  %.80s",i+1,count,names[i]);
        host_loading_show(2,progress);
        char full[1024]; snprintf(full,sizeof(full),"%s/%s",root,names[i]);
        void *archive=pfs_open_single(full,"auto");
        if (archive) archives[archive_count++]=archive;
        printf("Archive %s: %s\n",names[i],archive?"opened":"failed"); free(names[i]);
    }
    return archive_count;
}
void host_files_close(void) {
    pthread_mutex_lock(&files_mutex);
    while (archive_count) pfs_close(archives[--archive_count]);
    pthread_mutex_unlock(&files_mutex);
}
#ifdef DIRECT_BUILTIN_EFFECTS
#include "platform_tables.inl"
#endif
static int read_unlocked(const char *path,uint8_t *out,int cap,int64_t offset) {
    if (!valid_path(path)) return -1;
    char normalized[512]; normalize(normalized,path);
    int n=disk_read(saves,normalized,out,cap,offset); if(n>=0)return n;
    n=disk_read(game_root,normalized,out,cap,offset); if(n>=0)return n;
    for(int i=archive_count-1;i>=0;i--) {
        int size=pfs_file_size(archives[i],normalized);
        if(size>=0)return offset<0?size:pfs_read(archives[i],normalized,offset,out,cap);
    }
#ifdef DIRECT_BUILTIN_EFFECTS
    if(recover_platform_table(normalized))return disk_read(game_root,normalized,out,cap,offset);
#endif
    return -1;
}
int host_read(const char *path,uint8_t *out,int cap,int64_t offset) {
    // PF8 owns a seekable reader; audio and rendering must not seek it concurrently.
    // Bound each lock hold during a large image read. Each sub-read supplies its
    // absolute offset, so an intervening audio seek cannot corrupt the image.
    if(offset>=0 && out && cap>32768){
        const uint64_t started=host_load_clock();uint64_t wait_us=0;
        int done=0,result=0;
        while(done<cap){
            int chunk=cap-done;if(chunk>32768)chunk=32768;
            if(offset>INT64_MAX-done){result=-1;break;}
            uint64_t before=host_load_clock();pthread_mutex_lock(&files_mutex);
            wait_us+=host_load_clock()-before;
            int n=read_unlocked(path,out+done,chunk,offset+done);
            pthread_mutex_unlock(&files_mutex);
            if(n<0){result=-1;break;}
            done+=n;result=done;
            if(n<chunk)break;
            // A pure sched_yield can immediately reacquire this mutex and
            // starve the woken audio reader. A minimal timed yield lets it run.
            if(done<cap)usleep(1);
        }
        host_load_report("file-read",path,started,started+wait_us,host_load_clock(),result);
        return result;
    }
    uint64_t started=host_load_clock();pthread_mutex_lock(&files_mutex);uint64_t acquired=host_load_clock();
    int result=read_unlocked(path,out,cap,offset);
    pthread_mutex_unlock(&files_mutex);
    host_load_report(offset<0?"file-size":"file-read",path,started,acquired,host_load_clock(),result);
    return result;
}
int host_write(const char *path,const uint8_t *bytes,int length) {
    if(!valid_path(path)||length<0||(!bytes&&length))return -1;
    char normalized[512],full[1024],temporary[1050],backup[1050]; normalize(normalized,path);
    snprintf(full,sizeof(full),"%s/%s",saves,normalized);
    snprintf(temporary,sizeof(temporary),"%s.art3m1s-tmp",full);
    snprintf(backup,sizeof(backup),"%s.art3m1s-prev",full);
    uint64_t started=host_load_clock();pthread_mutex_lock(&files_mutex);uint64_t acquired=host_load_clock();
    for(char *p=full+strlen(saves)+1;*p;p++)if(*p=='/'){*p=0;sceIoMkdir(full,0777);*p='/';}
    // Write a fresh sibling so a shorter save cannot retain bytes from an older one.
    sceIoRemove(temporary);
    int fd=sceIoOpen(temporary,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);
    int result=-1,written=0;
    if(fd<0)goto done;
    while(written<length){int n=sceIoWrite(fd,bytes+written,length-written);if(n<=0)break;written+=n;}
    int closed=sceIoClose(fd);
    SceIoStat stat;
    if(written!=length||closed<0||sceIoGetstat(temporary,&stat)<0||stat.st_size!=(uint64_t)length)goto done;
    int had_old=sceIoGetstat(full,&stat)>=0;
    if(had_old){sceIoRemove(backup);if(sceIoRename(full,backup)<0)goto done;}
    if(sceIoRename(temporary,full)<0){if(had_old)sceIoRename(backup,full);goto done;}
    result=length;
done:
    if(result<0)sceIoRemove(temporary);
    pthread_mutex_unlock(&files_mutex);
    host_load_report("file-write",path,started,acquired,host_load_clock(),result);
    return result;
}
int host_delete(const char *path) {
    if(!valid_path(path))return -1;
    char normalized[512],full[1024];normalize(normalized,path);
    snprintf(full,sizeof(full),"%s/%s",saves,normalized);
    pthread_mutex_lock(&files_mutex);int result=sceIoRemove(full);pthread_mutex_unlock(&files_mutex);return result;
}
