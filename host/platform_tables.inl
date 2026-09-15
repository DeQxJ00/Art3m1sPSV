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
// Upgrade our previous generated main table only when its entire body still
// matches the PFS source and generated suffix. User-edited tables are preserved.
static int table_update_resolution(const char *path) {
    int size=disk_read(game_root,path,NULL,0,-1);
    if(size<=0||size>2*1024*1024+1024)return 0;
    char *data=calloc(1,(size_t)size+1);if(!data)return -1;
    if(disk_read(game_root,path,(uint8_t*)data,size,0)!=size){free(data);return -1;}
    if(strncmp(data,table_header,sizeof(table_header)-1)){free(data);return 0;}
    char *begin=data+sizeof(table_header)-1,*end=strchr(begin,'\n');
    if(!end||end-begin>=200){free(data);return 0;}
    char source[256];memcpy(source,begin,end-begin);source[end-begin]=0;
    if(!valid_path(source)||strncmp(source,"system/table/list_",18)){free(data);return 0;}
    int base_size=0;char *base=table_content(source,1,0,&base_size);
    if(!base||size<base_size||memcmp(data,base,base_size)){free(base);free(data);return 0;}
    int verified=size==base_size;
    if(!verified){
        int w=0,h=0,gw=0,gh=0;char expected[384];
        if(sscanf(data+base_size,"\n-- art3m1s vita-resolution\nif init then\n init.game_scale={%d,%d}\n init.game_width=%d\n init.game_height=%d\nend\n",&w,&h,&gw,&gh)==4&&w==gw&&h==gh){
            int n=table_resolution_footer(expected,sizeof(expected),w,h);
            verified=size-base_size==n&&!memcmp(data+base_size,expected,n);
        }
    }
    free(base);
    int next_size=0;char *next=verified?table_content(source,1,1,&next_size):NULL;
    int result=0;
    if(next&&(next_size!=size||memcmp(next,data,size)))result=table_publish(path,next,next_size,1)?1:-1;
    free(next);free(data);return result;
}
static int recover_platform_table(const char *path) {
    static const char prefix[]="system/table/list_";
    static const char *platforms[]={"windows","vita","ps4","switch","android","ios","wasm"};
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
    char source[256]={0};int size=-1;
    for(unsigned i=0;i<sizeof(platforms)/sizeof(*platforms);++i){
        char base[256];snprintf(base,sizeof(base),"%s%s.tbl",prefix,platforms[i]);
        if(archive_table_read(base,NULL,0)<=0)continue;
        snprintf(source,sizeof(source),"%s%s%s",prefix,platforms[i],suffix);
        if(!strcmp(source,path))continue;
        size=archive_table_read(source,NULL,0);
        if(size>0)break;
    }
    int content_size=0;
    char *content=table_content(source,!strcmp(suffix,".tbl"),1,&content_size);
    if(!content)return 0;
    int ok=table_publish(path,content,content_size,0);free(content);
    printf("[table-recovery] %s source=%s target=%s/%s vita=%dx%d\n",ok?"generated":"failed",source,game_root,path,table_vita_width,table_vita_height);
    return ok;
}
