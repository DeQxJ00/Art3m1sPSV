// Per-game on-device compiler and persistent GXP cache. No game PFS changes.
namespace {
std::string externalCacheRoot,externalCompilerStamp;
bool externalCompilerReady=false;
ShaderSettings externalOptions;
uint64_t shader_hash(const void* bytes,size_t size,uint64_t h=0xcbf29ce484222325ULL){
    const auto* b=static_cast<const uint8_t*>(bytes);for(size_t i=0;i<size;++i)h=(h^b[i])*0x100000001b3ULL;return h;
}
std::string shader_hex(uint64_t h){char b[17];std::snprintf(b,sizeof(b),"%016llx",(unsigned long long)h);return b;}
std::vector<uint8_t> shader_read(const std::string& path,size_t limit){
    auto* f=std::fopen(path.c_str(),"rb");if(!f)return {};
    if(std::fseek(f,0,SEEK_END)){std::fclose(f);return {};}
    const long n=std::ftell(f);if(n<=0||size_t(n)>limit||std::fseek(f,0,SEEK_SET)){std::fclose(f);return {};}
    std::vector<uint8_t> b(n);const bool ok=std::fread(b.data(),1,n,f)==size_t(n);std::fclose(f);if(!ok)b.clear();return b;
}
bool shader_write(const std::string& path,const void* data,size_t size){
    const auto tmp=path+".tmp";auto* f=std::fopen(tmp.c_str(),"wb");if(!f)return false;
    bool ok=std::fwrite(data,1,size,f)==size;ok=std::fclose(f)==0&&ok;
    if(ok)ok=sceIoRename(tmp.c_str(),path.c_str())>=0;
    if(!ok)sceIoRemove(tmp.c_str());return ok;
}
void external_compiler_log(const char* msg,shark_log_level level,int line){log("[shader-compiler] level=%d line=%d %s",int(level),line,msg?msg:"");}
}
void external_shader_options(ShaderSettings settings){externalOptions=settings;}
bool external_conversion_enabled(){return externalOptions.convert;}
bool external_cache_path(const char* path,char* out,size_t capacity){
    if(!path||!out||externalCacheRoot.empty())return false;
    const auto relative=shader_cache_relative(path);if(relative.empty())return false;
    const auto base=externalCacheRoot+"/"+relative;
    if(base.size()+1>capacity)return false;
    for(size_t at=externalCacheRoot.size()+1;(at=base.find('/',at))!=std::string::npos;++at)
        sceIoMkdir(base.substr(0,at).c_str(),0777);
    std::memcpy(out,base.c_str(),base.size()+1);return true;
}
void external_compiler_end(){if(externalCompilerReady){shark_clear_output();shark_end();externalCompilerReady=false;log("[shader-compiler] load batch complete; compiler scratch/module released");}}
void external_cache_root(const std::string& path){
    external_compiler_end();externalCacheRoot=path;externalCompilerStamp.clear();
    const int result=sceIoMkdir(path.c_str(),0777);
    log("[shader-cache] game directory=%s mkdir=%08x",path.c_str(),unsigned(result));
}
unsigned external_compile(const char* id,const char* sourceKey,const char* cg){
    if(active||!id||!sourceKey||!cg||std::strlen(cg)>256*1024||externalCacheRoot.empty())return 0;
    const auto started=sceKernelGetProcessTimeWide();
    if(externalCompilerStamp.empty()){
        auto module=shader_read("ur0:/data/libshacccg.suprx",8*1024*1024);
        externalCompilerStamp=module.empty()?"missing":shader_hex(shader_hash(module.data(),module.size()));
    }
    char path[1024];if(!external_cache_path(id,path,sizeof(path)))return 0;
    const std::string identity=std::string("cg-front-v1|shark-safe-no-fast|sprite-abi1|")+sourceKey+"|"+externalCompilerStamp+"|"+cg;
    const std::string base=path;
    const auto key=shader_hex(shader_hash(identity.data(),identity.size()));
    auto cached=shader_read(base+".gxp",1024*1024);auto checksum=shader_read(base+".hash",64);
    if(!cached.empty()&&std::string(checksum.begin(),checksum.end())==key+" "+shader_hex(shader_hash(cached.data(),cached.size()))){
        if(auto handle=external_register(cached.data(),cached.size())){
            log("[shader-cache] hit id=%s bytes=%u elapsed_us=%llu",id,unsigned(cached.size()),(unsigned long long)(sceKernelGetProcessTimeWide()-started));return handle;
        }
    }
    if(!externalOptions.compile){log("[shader-compiler] disabled; no matching GXP cache id=%s",id);return 0;}
    if(!externalCompilerReady){
        shark_install_log_cb(external_compiler_log);shark_set_warnings_level(SHARK_WARN_HIGH);
        const int r=shark_init(nullptr);if(r<0){log("[shader-compiler] init failed=%08x id=%s; requires ur0:/data/libshacccg.suprx",unsigned(r),id);return 0;}
        externalCompilerReady=true;
    }
    uint32_t bytes=uint32_t(std::strlen(cg));
    auto* program=shark_compile_shader_extended(cg,&bytes,SHARK_FRAGMENT_SHADER,SHARK_OPT_SAFE,SHARK_DISABLE,SHARK_DISABLE,SHARK_DISABLE);
    if(!program){log("[shader-compiler] failed id=%s; no stale program/cache selected",id);shark_clear_output();return 0;}
    const auto handle=external_register(reinterpret_cast<const uint8_t*>(program),bytes);
    if(!handle)shader_write(base+".rejected-gxp",program,bytes);
    bool saved=false;
    if(handle){
        const auto sum=key+" "+shader_hex(shader_hash(program,bytes));
        saved=shader_write(base+".gxp",program,bytes)&&shader_write(base+".hash",sum.data(),sum.size());
        shader_write(base+".cg",cg,std::strlen(cg));
    }
    std::free(program);shark_clear_output();
    log("[shader-compiler] id=%s handle=%u bytes=%u saved=%d elapsed_us=%llu",id,handle,bytes,int(saved),(unsigned long long)(sceKernelGetProcessTimeWide()-started));return handle;
}
