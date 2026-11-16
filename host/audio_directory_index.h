#pragma once
#include <errno.h>

/* Presence only: file sizes and payloads still come from the ordinary reader.
   A complete parent listing rules out missing asset siblings, including
   character movies and coordinate metadata. Bounded to eight 32 KiB listings.
   Generated tables and extensionless directory probes keep ordinary stat semantics.
   Call under files_lock; invalidate on writes and project changes. */
typedef struct {
    char parent[1024];
    char *names;
    size_t used;
    unsigned age;
    int complete;
} AudioDirectoryIndex;
static AudioDirectoryIndex audio_directories[8];
static unsigned audio_directory_clock;
static void audio_directory_clear(void){
    for(unsigned i=0;i<8;i++)free(audio_directories[i].names);
    memset(audio_directories,0,sizeof(audio_directories));
    audio_directory_clock=0;
}
static int audio_directory_missing(const char *full){
    const char *slash=strrchr(full,'/');
    if(!slash || slash==full || (size_t)(slash-full)>=1024)return 0;
    const char *dot=strrchr(slash+1,'.');
    if(!dot || (strcasecmp(dot,".wav") && strcasecmp(dot,".ogg") &&
        strcasecmp(dot,".oga") && strcasecmp(dot,".mp3") &&
        strcasecmp(dot,".m4a") && strcasecmp(dot,".at9") &&
        strcasecmp(dot,".png") && strcasecmp(dot,".jpg") &&
        strcasecmp(dot,".jpeg") && strcasecmp(dot,".dds") &&
        strcasecmp(dot,".pvr") && strcasecmp(dot,".ogv") &&
        strcasecmp(dot,".mja") && strcasecmp(dot,".csv") &&
        strcasecmp(dot,".mode") && strcasecmp(dot,".lnq") &&
        strcasecmp(dot,".lua")))return 0;
    char parent[1024];memcpy(parent,full,slash-full);parent[slash-full]=0;
    AudioDirectoryIndex *entry=NULL,*oldest=&audio_directories[0];
    for(unsigned i=0;i<8;i++){
        AudioDirectoryIndex *item=&audio_directories[i];
        if(!strcmp(item->parent,parent)){entry=item;break;}
        if(item->age<oldest->age)oldest=item;
    }
    if(!entry){
        entry=oldest;free(entry->names);memset(entry,0,sizeof(*entry));
        strcpy(entry->parent,parent);
        DIR *dir=opendir(parent);
        if(!dir){
            /* Permission/I/O failures are not evidence of absence. */
            if(errno==ENOENT || errno==ENOTDIR)entry->complete=1;
        }else{
            entry->names=malloc(32768);
            if(entry->names){
                for(;;){
                    errno=0;struct dirent *item=readdir(dir);
                    if(!item){entry->complete=(errno==0);break;}
                    size_t n=strlen(item->d_name)+1;
                    if(n>32768-entry->used)break;
                    memcpy(entry->names+entry->used,item->d_name,n);entry->used+=n;
                }
            }
            closedir(dir);
        }
    }
    entry->age=++audio_directory_clock;
    if(!entry->complete)return 0; /* Incomplete listing always falls back. */
    for(size_t pos=0;pos<entry->used;pos+=strlen(entry->names+pos)+1)
        if(!strcasecmp(entry->names+pos,slash+1))return 0;
    return 1;
}
