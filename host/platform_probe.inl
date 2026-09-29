// Launcher-only inspection. Keep separate handles so this cannot affect an
// active playback stream or trigger the runtime's table-recovery writes.
static int table_probe_platforms(const char *root,const char *save_root,int status[6]) {
    if(!root||!save_root||strlen(root)>=512||strlen(save_root)>=512)return -1;
    DIR *dir=opendir(root);if(!dir)return -1;
    char *names[64];int count=0,failed=0;struct dirent *ent;
    while((ent=readdir(dir))){
        const char *suffix=strstr(ent->d_name,".pfs");if(!suffix)continue;
        suffix+=4;
        if(*suffix&&(strlen(suffix)!=4||suffix[0]!='.'||strspn(suffix+1,"0123456789")!=3))continue;
        if(count==64){failed=1;break;}
        names[count]=strdup(ent->d_name);if(!names[count]){failed=1;break;}++count;
    }
    closedir(dir);qsort(names,count,sizeof(*names),compare_names);
    void *handles[64];TableResourceFiles files={root,save_root,handles,0};
    for(int i=0;i<count;++i){
        char path[1024];snprintf(path,sizeof(path),"%s/%s",root,names[i]);
        void *handle=pfs_open_single(path,"auto");free(names[i]);
        if(handle)handles[files.count++]=handle;else failed=1;
    }
    if(!failed){
        // A fallback uses PFS sources, exactly as the runtime does. Native
        // loose tables may override them and must be checked independently.
        int fallback=0;
        for(unsigned i=0;i<sizeof(table_platforms)/sizeof(*table_platforms);++i){
            char path[128];snprintf(path,sizeof(path),"system/table/list_%s.tbl",table_platforms[i]);
            if(table_resource_read(&files,path,NULL,0,0)>0&&
                table_resources_match_in(&files,path,0,NULL)>0){fallback=1;break;}
        }
        static const char *platforms[]={"vita","windows","switch","android","ios","ps4"};
        for(int i=0;i<6;++i){
            char path[128];snprintf(path,sizeof(path),"system/table/list_%s.tbl",platforms[i]);
            if(table_resource_read(&files,path,NULL,0,1)<0){
                status[i]=fallback?HOST_PLATFORM_FALLBACK:HOST_PLATFORM_NO_TABLE;continue;
            }
            int generated=0,result=table_resources_match_in(&files,path,1,&generated);
            status[i]=result>0?(generated?HOST_PLATFORM_FALLBACK:HOST_PLATFORM_MATCHED):
                result<0?HOST_PLATFORM_UNVERIFIED:HOST_PLATFORM_NO_IMAGES;
            // Report the current table honestly; a generated-looking header is
            // not sufficient proof that startup can safely repair a user edit.
        }
    }
    while(files.count)pfs_close(handles[--files.count]);
    return failed?-1:0;
}
