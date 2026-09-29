// Conservative, non-executing inspection of literal resource roots in Lua TBLs.
// Unknown/computed roots are not safe automatic fallback candidates.
extern int pfs_entry_count(void *);
extern int pfs_entry_path(void *,int,char *,int);
// Independent read-only context: launcher probes never replace active archives.
typedef struct {
    const char *root,*save_root;
    void **handles;
    int count;
} TableResourceFiles;
static int table_resource_read(const TableResourceFiles *files,const char *path,uint8_t *out,int cap,int loose) {
    if(loose){
        int n=disk_read(files->save_root,path,out,cap,out?0:-1);
        if(n>=0)return n;
        n=disk_read(files->root,path,out,cap,out?0:-1);
        if(n>=0)return n;
    }
    for(int i=files->count-1;i>=0;--i){
        int n=pfs_file_size(files->handles[i],path);
        if(n>=0)return out?pfs_read(files->handles[i],path,0,out,cap):n;
    }
    return -1;
}
static int table_image_name(const char *path) {
    const char *ext=strrchr(path,'.');
    return ext&&(!strcasecmp(ext,".png")||!strcasecmp(ext,".jpg")||
        !strcasecmp(ext,".jpeg")||!strcasecmp(ext,".webp")||!strcasecmp(ext,".dds"));
}
static int table_loose_images(const char *root,const char *prefix,int depth) {
    if(depth>12)return 0;
    char full[1024];snprintf(full,sizeof(full),"%s/%s",root,prefix);
    DIR *dir=opendir(full);if(!dir)return 0;
    int found=0;struct dirent *ent;
    while(!found&&(ent=readdir(dir))){
        if(ent->d_name[0]=='.')continue;
        char child[512];int n=snprintf(child,sizeof(child),"%s%s",prefix,ent->d_name);
        if(n<0||n>=(int)sizeof(child)-2)continue;
        uint8_t byte;
        if(table_image_name(child)&&disk_read(root,child,&byte,1,0)==1)found=1;
        else {strcat(child,"/");found=table_loose_images(root,child,depth+1);}
    }
    closedir(dir);return found;
}
static int table_resource_root(const TableResourceFiles *files,const char *prefix) {
    size_t len=strlen(prefix);
    if(!len||!valid_path(prefix)||prefix[len-1]!='/')return 0;
    for(int a=files->count-1;a>=0;--a){
        int count=pfs_entry_count(files->handles[a]);
        for(int i=0;i<count;++i){
            char path[512],normalized[512];
            int n=pfs_entry_path(files->handles[a],i,path,sizeof(path));
            if(n<=0||n>=(int)sizeof(path)-1)continue;
            normalize(normalized,path);
            if(!strncmp(normalized,prefix,len)&&table_image_name(normalized)&&
                table_resource_read(files,normalized,NULL,0,1)>0)return 1;
        }
    }
    return table_loose_images(files->save_root,prefix,0)||table_loose_images(files->root,prefix,0);
}
// Return the next identifier/string/punctuation, skipping Lua comments and long
// strings (including [=[...]=]). This never evaluates code from a game archive.
static int table_token(const char **cursor,char *out,size_t cap) {
    const char *p=*cursor;out[0]=0;
    for(;;){
        while(*p&&isspace((unsigned char)*p))++p;
        int comment=p[0]=='-'&&p[1]=='-';
        if(comment)p+=2;
        const char *q=p;
        if(*q=='['){
            ++q;while(*q=='=')++q;
            if(*q=='['){
                size_t equals=(size_t)(q-p-1);p=q+1;
                while(*p){
                    if(*p==']'){
                        q=p+1;size_t n=0;while(*q=='='){++q;++n;}
                        if(n==equals&&*q==']'){p=q+1;break;}
                    }
                    ++p;
                }
                if(comment)continue;
                *cursor=p;return 'L';
            }
        }
        if(comment){while(*p&&*p!='\n')++p;continue;}
        break;
    }
    if(!*p){*cursor=p;return 0;}
    int kind=(unsigned char)*p;size_t n=0;
    if(*p=='\''||*p=='"'){
        char quote=*p++;kind='S';
        while(*p&&*p!=quote){
            if(*p=='\\'){++p;if(!*p)break;if(*p!='\\'&&*p!=quote)kind='L';}
            if(n+1<cap)out[n++]=*p;else kind='L';++p;
        }
        if(*p==quote)++p;else kind='L';
    }else if(isalpha((unsigned char)*p)||*p=='_'){
        kind='I';while(isalnum((unsigned char)*p)||*p=='_'){
            if(n+1<cap)out[n++]=*p;++p;
        }
    }else ++p;
    out[n]=0;*cursor=p;return kind;
}
// 1: matching roots, 0: missing images, -1: cannot verify the table.
static int table_resources_match_in(const TableResourceFiles *files,const char *source,int loose,int *generated) {
    int size=table_resource_read(files,source,NULL,0,loose);
    if(size<=0||size>2*1024*1024)return -1;
    char *data=calloc(1,(size_t)size+1);if(!data)return -1;
    if(table_resource_read(files,source,(uint8_t*)data,size,loose)!=size||memchr(data,0,size)){free(data);return -1;}
    static const char generated_header[]="-- art3m1s platform-table fallback: ";
    if(generated)*generated=!strncmp(data,generated_header,sizeof(generated_header)-1);
    const char *p=data;char token[512],roots[2][512]={{0}};int seen[2]={0},kind;
    while((kind=table_token(&p,token,sizeof(token)))){
        int field=!strcmp(token,"ui_path")?0:!strcmp(token,"image_path")?1:-1;
        if(field<0||(kind!='I'&&kind!='S'))continue;
        const char *q=p;int next=table_token(&q,token,sizeof(token));
        if(kind=='S'&&next==']')next=table_token(&q,token,sizeof(token));
        if(next!='=')continue;
        next=table_token(&q,token,sizeof(token));
        seen[field]=1;roots[field][0]=0;
        if(next=='S'){
            normalize(roots[field],token);
            // A literal followed by concatenation/function syntax is computed.
            next=table_token(&q,token,sizeof(token));
            if(next!=','&&next!='}'&&next!=';'&&next!=0)roots[field][0]=0;
        }
    }
    free(data);
    int count=0;
    for(int i=0;i<2;++i)if(seen[i]){
        ++count;
        if(!roots[i][0]||!table_resource_root(files,roots[i])){
            printf("[table-recovery] skip source=%s missing/unresolved %s=%s\n",source,i?"image_path":"ui_path",roots[i]);
            return roots[i][0]?0:-1;
        }
    }
    if(!count)printf("[table-recovery] skip source=%s no verifiable image roots\n",source);
    return count?1:-1;
}

static int table_resources_match(const char *source) {
    TableResourceFiles files={game_root,saves,archives,archive_count};
    return table_resources_match_in(&files,source,0,NULL)>0;
}
