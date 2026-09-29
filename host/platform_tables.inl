// Called under files_mutex, only after the normal virtual-file lookup misses.
// Generate loose Lua tables from PFS entries; never rewrite an archive or an
// existing override. Language suffixes are preserved rather than hardcoded.
static int archive_table_read(const char *path,uint8_t *out,int cap) {
    for(int i=archive_count-1;i>=0;--i){
        int size=pfs_file_size(archives[i],path);
        if(size>=0)return out?pfs_read(archives[i],path,0,out,cap):size;
    }
    return -1;
}
#include "table_resources.inl"
static const char *table_platforms[]={"windows","vita","switch","android","ios","ps4"};
static int table_select_source(const char *target,char *out,size_t cap) {
    for(unsigned i=0;i<sizeof(table_platforms)/sizeof(*table_platforms);++i){
        snprintf(out,cap,"system/table/list_%s.tbl",table_platforms[i]);
        if(strcmp(out,target)&&table_resources_match(out))return 1;
    }
    out[0]=0;return 0;
}

static int table_vita_width=960,table_vita_height=540;
static const char table_header[]="-- art3m1s platform-table fallback: ";
static const char table_shader[]="\nif init and init.system and init.system.blur_exp == '.glsl' then\n"
    " init.system.blur_path='pc/'\n init.system.blur_exp='.hlsl'\nend\n";
static int table_resolution_footer(char *out,int cap,int width,int height) {
    return snprintf(out,cap,"\n-- art3m1s vita-resolution\nif init then\n init.game_scale={%d,%d}\n"
        " init.game_width=%d\n init.game_height=%d\nend\n",width,height,width,height);
}
static char *table_content(const char *source,int main_table,int resolution,int *length) {
    int size=archive_table_read(source,NULL,0);
    if(size<=0||size>2*1024*1024)return NULL;
    uint8_t *data=malloc((size_t)size);if(!data)return NULL;
    if(archive_table_read(source,data,size)!=size||memchr(data,0,size)){free(data);return NULL;}
    char *out=malloc((size_t)size+1024);if(!out){free(data);return NULL;}
    int n=snprintf(out,512,"%s%s\n",table_header,source);
    size_t skip=size>=3&&!memcmp(data,"\xef\xbb\xbf",3)?3:0;
    memcpy(out+n,data+skip,(size_t)size-skip);n+=size-(int)skip;free(data);
    if(main_table&&archive_table_read("system/shader/pc/reset.hlsl",NULL,0)>0){
        memcpy(out+n,table_shader,sizeof(table_shader)-1);n+=sizeof(table_shader)-1;
    }
    if(main_table&&resolution)n+=table_resolution_footer(out+n,384,table_vita_width,table_vita_height);
    out[n]=0;*length=n;return out;
}
static int table_publish(const char *path,const char *data,int size,int replace) {
    char full[1024],temporary[1060],backup[1060],directory[1024];
    snprintf(full,sizeof(full),"%s/%s",game_root,path);
    snprintf(temporary,sizeof(temporary),"%s.art3m1s-table.tmp",full);
    snprintf(backup,sizeof(backup),"%s.art3m1s-table.bak",full);
    snprintf(directory,sizeof(directory),"%s/system",game_root);sceIoMkdir(directory,0777);
    snprintf(directory,sizeof(directory),"%s/system/table",game_root);sceIoMkdir(directory,0777);
    SceIoStat stat;
    if(!replace&&sceIoGetstat(full,&stat)>=0)return 0;
    FILE *f=fopen(temporary,"wb");if(!f)return 0;
    int ok=fwrite(data,1,size,f)==(size_t)size;
    if(fclose(f)!=0)ok=0;
    if(ok&&replace){
        // Only byte-verified generated files reach this path. Keep a rollback.
        sceIoRemove(backup);
        if(sceIoRename(full,backup)<0)ok=0;
        else if(sceIoRename(temporary,full)<0){sceIoRename(backup,full);ok=0;}
        else sceIoRemove(backup);
    }else if(ok&&sceIoGetstat(full,&stat)<0)ok=sceIoRename(temporary,full)>=0;
    else ok=0;
    if(!ok)sceIoRemove(temporary);
    return ok;
}
// Only exact generated files may be migrated. A header alone is insufficient.
static int table_generated_source(const char *path,int main_table,char *source) {
    int size=disk_read(game_root,path,NULL,0,-1);
    if(size<=0||size>2*1024*1024+1024)return 0;
    char *data=calloc(1,(size_t)size+1);if(!data)return 0;
    if(disk_read(game_root,path,(uint8_t*)data,size,0)!=size||
        strncmp(data,table_header,sizeof(table_header)-1)){free(data);return 0;}
    char *begin=data+sizeof(table_header)-1,*end=strchr(begin,'\n');
    if(!end||end-begin>=200){free(data);return 0;}
    memcpy(source,begin,end-begin);source[end-begin]=0;
    if(!valid_path(source)||strncmp(source,"system/table/list_",18)){free(data);return 0;}
    int base_size=0;char *base=table_content(source,main_table,0,&base_size);
    if(!base||size<base_size||memcmp(data,base,base_size)){free(base);free(data);return 0;}
    int verified=size==base_size;
    if(!verified&&main_table){
        int w=0,h=0,gw=0,gh=0;char expected[384];
        if(sscanf(data+base_size,"\n-- art3m1s vita-resolution\nif init then\n init.game_scale={%d,%d}\n init.game_width=%d\n init.game_height=%d\nend\n",&w,&h,&gw,&gh)==4&&w==gw&&h==gh){
            int n=table_resolution_footer(expected,sizeof(expected),w,h);
            verified=size-base_size==n&&!memcmp(data+base_size,expected,n);
        }
    }
    free(base);free(data);return verified;
}
static int table_replace_generated(const char *path,const char *source,int main_table) {
    int n=0;char *next=table_content(source,main_table,1,&n);if(!next)return -1;
    int size=disk_read(game_root,path,NULL,0,-1),same=0;
    if(size==n){
        char *old=malloc((size_t)n);
        if(old){same=disk_read(game_root,path,(uint8_t*)old,n,0)==n&&!memcmp(old,next,n);free(old);}
    }
    int result=same?0:table_publish(path,next,n,1)?1:-1;
    if(result)printf("[table-recovery] %s source=%s target=%s\n",result>0?"updated":"update-failed",source,path);
    free(next);return result;
}
static int table_update_resolution(const char *path) {
    char source[256];
    if(!table_generated_source(path,1,source))return 0;
    if(!table_resources_match(source)&&!table_select_source(path,source,sizeof(source))){
        printf("[table-recovery] no resource-compatible replacement for %s\n",path);return -1;
    }
    int result=table_replace_generated(path,source,1);
    if(result<0)return result;
    // Repair only our unmodified language tables, using the selected main source.
    char directory[1024];snprintf(directory,sizeof(directory),"%s/system/table",game_root);
    DIR *dir=opendir(directory);if(!dir)return result;
    const char *name=strrchr(path,'/')+1;size_t stem=strlen(name)-4;
    struct dirent *ent;
    while((ent=readdir(dir))){
        size_t len=strlen(ent->d_name);
        if(len<=stem+4||strncmp(ent->d_name,name,stem)||ent->d_name[stem]!='_'||strcmp(ent->d_name+len-4,".tbl"))continue;
        char language[256],old_source[256],new_source[512];
        if(snprintf(language,sizeof(language),"system/table/%s",ent->d_name)>=(int)sizeof(language))continue;
        if(!table_generated_source(language,0,old_source))continue;
        snprintf(new_source,sizeof(new_source),"%.*s%s",(int)strlen(source)-4,source,ent->d_name+stem);
        if(archive_table_read(new_source,NULL,0)<=0){
            // Obsolete generated language files must not mix platform layouts.
            char full[1024];snprintf(full,sizeof(full),"%s/%s",game_root,language);
            if(sceIoRemove(full)<0)result=-1;
            printf("[table-recovery] removed obsolete generated language=%s\n",language);
        }else if(table_replace_generated(language,new_source,0)<0)result=-1;
    }
    closedir(dir);return result;
}
static int recover_platform_table(const char *path) {
    static const char prefix[]="system/table/list_";
    static const char *platforms[]={"windows","vita","switch","android","ios","ps4"};
    const size_t prefix_len=sizeof(prefix)-1, length=strlen(path);
    if(strncmp(path,prefix,prefix_len)||length< prefix_len+5||length>=200||strcmp(path+length-4,".tbl"))return 0;
    const char *target=path+prefix_len,*suffix=NULL;
    for(unsigned i=0;i<sizeof(platforms)/sizeof(*platforms);++i){
        size_t n=strlen(platforms[i]);
        if(!strncmp(target,platforms[i],n)&&(target[n]=='_'||target[n]=='.')){suffix=target+n;break;}
    }
    if(!suffix)return 0;
    for(const char *p=suffix;p<path+length-4;++p)
        if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_'))return 0;
    char source[256]={0},base[256],selected[256];
    int main_table=!strcmp(suffix,".tbl");
    snprintf(base,sizeof(base),"%.*s.tbl",(int)(suffix-path),path);
    // A generated main table fixes the platform for every language suffix.
    if(!main_table&&table_generated_source(base,1,selected)){
        if(!table_resources_match(selected))return 0;
    }else if(!table_select_source(base,selected,sizeof(selected)))return 0;
    snprintf(source,sizeof(source),"%.*s%s",(int)strlen(selected)-4,selected,suffix);
    if(archive_table_read(source,NULL,0)<=0)return 0;
    int content_size=0;
    char *content=table_content(source,!strcmp(suffix,".tbl"),1,&content_size);
    if(!content)return 0;
    int ok=table_publish(path,content,content_size,0);free(content);
    printf("[table-recovery] %s source=%s target=%s/%s vita=%dx%d\n",ok?"generated":"failed",source,game_root,path,table_vita_width,table_vita_height);
    return ok;
}
