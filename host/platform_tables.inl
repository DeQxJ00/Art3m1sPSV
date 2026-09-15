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
    if(size<=0||size>2*1024*1024)return 0;
    uint8_t *data=malloc((size_t)size);if(!data)return 0;
    if(archive_table_read(source,data,size)!=size||memchr(data,0,size)){free(data);return 0;}
    char full[1024],temporary[1060],directory[1024];
    snprintf(full,sizeof(full),"%s/%s",game_root,path);
    snprintf(temporary,sizeof(temporary),"%s.art3m1s-table.tmp",full);
    snprintf(directory,sizeof(directory),"%s/system",game_root);sceIoMkdir(directory,0777);
    snprintf(directory,sizeof(directory),"%s/system/table",game_root);sceIoMkdir(directory,0777);
    // A directory or unreadable existing file must not be replaced either.
    SceIoStat stat;
    if(sceIoGetstat(full,&stat)>=0){free(data);return 0;}
    FILE *f=fopen(temporary,"wb");if(!f){free(data);return 0;}
    int ok=fprintf(f,"-- art3m1s platform-table fallback: %s\n",source)>0;
    size_t skip=size>=3&&!memcmp(data,"\xef\xbb\xbf",3)?3:0;
    ok=fwrite(data+skip,1,(size_t)size-skip,f)==(size_t)size-skip&&ok;
    // GXM uses the PC HLSL companions when the package includes them. Keep
    // image/movie paths and all layout values from the source platform table.
    if(!strcmp(suffix,".tbl")&&archive_table_read("system/shader/pc/reset.hlsl",NULL,0)>0){
        ok=fputs("\nif init and init.system and init.system.blur_exp == '.glsl' then\n"
                 " init.system.blur_path='pc/'\n init.system.blur_exp='.hlsl'\nend\n",f)>=0&&ok;
    }
    ok=fclose(f)==0&&ok;free(data);
    if(ok&&sceIoGetstat(full,&stat)<0)ok=sceIoRename(temporary,full)>=0;
    else ok=0;
    if(!ok)sceIoRemove(temporary);
    printf("[table-recovery] %s source=%s target=%s\n",ok?"generated":"failed",source,full);
    return ok;
}
