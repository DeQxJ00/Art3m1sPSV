// Included once by bridge.cpp, after the texture registry.
#include <cstring>
#define STB_DXT_STATIC
#define STB_DXT_IMPLEMENTATION
#include "../third_party/stb_dxt.h"
#include "large_image_cache.hpp"
#include "image_certificate.hpp"
#include <cstdio>
#include <cctype>
#include <new>
#include <memory>
#include <sys/stat.h>
#include <dirent.h>
#include <psp2/kernel/processmgr.h>
namespace direct {
namespace { struct SourceIdentity {uint64_t hash;size_t bytes;};std::unordered_map<std::string,SourceIdentity> largeImageSources; struct PreparedImage {unsigned w,h;bool bc1;};std::unordered_map<std::string,PreparedImage> largeImageReady;bool largeImageEnabled=true;std::string largeImageRoot,largeImageSourceRoot;unsigned largeImageThreshold=30;LargeImageScope largeImageScope; }
void large_image_configure(bool enabled,const std::string& root,unsigned thresholdMiB,const LargeImageScope& scope,const std::string& sourceRoot){largeImageSourceRoot=sourceRoot;largeImageReady.clear();largeImageSources.clear();largeImageScope=scope;largeImageThreshold=std::max(1u,std::min(64u,thresholdMiB));largeImageEnabled=enabled;largeImageRoot=root;if(!root.empty())sceIoMkdir(root.c_str(),0777);}
bool large_image_allowed(unsigned w,unsigned h){return w&&h&&w<=16384&&h<=16384&&uint64_t(w)*h<=64*1024*1024&&((w>4096||h>4096)||(largeImageEnabled&&uint64_t(w)*h*4>=uint64_t(largeImageThreshold)*1024*1024));}
bool large_image_selected(const char* name,unsigned w,unsigned h){
    if(!large_image_allowed(w,h)||!name||largeImageRoot.empty())return false;
    if(w<=4096&&h<=4096&&!large_image_scope_matches(largeImageScope,name))return false;
    std::string lower=name;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});
    // Generated glyph/video/model surfaces never enter this static-image API.
    // Keep data textures lossless even when they happen to be large.
    for(const char* role:{"mask","rule","font","glyph","emote",".psb"})if(lower.find(role)!=std::string::npos)return false;
    return true;
}
int large_image_reserve(const char* name,unsigned w,unsigned h){
    if(!large_image_selected(name,w,h))return -1;
    auto found=largeImageReady.find(cpu_cache_folder(name));
    if(found==largeImageReady.end()||found->second.w!=w||found->second.h!=h)return -1;
    const auto size=image_texture_size(w,h);return reserve_bc3(size.w,size.h,found->second.bc1);
}

namespace {
std::string cache_path(unsigned w,unsigned h,uint64_t source,bool bc1){
    char leaf[80];std::snprintf(leaf,sizeof(leaf),"/v1-%016llx-%ux%u.%s",(unsigned long long)source,w,h,bc1?"bc1":"bc3");return largeImageRoot+leaf;
}
bool read_cache(unsigned w,unsigned h,uint64_t source,bool bc1,std::vector<uint8_t>& bytes){
    if(!w||!h||w>16384||h>16384||uint64_t(w)*h>64*1024*1024)return false;
    const size_t size=large_image_blocks(w,h,bc1)+40;bool hit=false;
    if(FILE* f=std::fopen(cache_path(w,h,source,bc1).c_str(),"rb")){
        if(!std::fseek(f,0,SEEK_END)&&std::ftell(f)==long(size)&&!std::fseek(f,0,SEEK_SET)){
            bytes.resize(size);hit=std::fread(bytes.data(),1,size,f)==size&&large_image_valid(bytes,w,h,source,bc1);
        }std::fclose(f);
    }return hit;
}
bool save_cache(const std::string& path,const std::vector<uint8_t>& header,const std::vector<uint8_t>& blocks={}){
    const auto tmp=path+".tmp";bool saved=false;
    if(FILE* f=std::fopen(tmp.c_str(),"wb")){
        saved=std::fwrite(header.data(),1,header.size(),f)==header.size()&&
            (blocks.empty()||std::fwrite(blocks.data(),1,blocks.size(),f)==blocks.size());
        saved=std::fclose(f)==0&&saved;
        if(saved){std::remove(path.c_str());saved=std::rename(tmp.c_str(),path.c_str())==0;}
        if(!saved)std::remove(tmp.c_str());
    }return saved;
}
bool source_identity(const std::string& key,SourceIdentity& identity){
    if(largeImageSourceRoot.empty())return false;
    std::vector<std::string> records;struct stat root{};
    if(stat(largeImageSourceRoot.c_str(),&root))return false;
    auto add=[&](const std::string& path){struct stat st{};if(stat(path.c_str(),&st)||!S_ISREG(st.st_mode))return false;
        records.push_back(path+"\n"+std::to_string(st.st_size)+":"+std::to_string(st.st_mtime)+":"+std::to_string(st.st_ctime));return true;};
    std::string directory=largeImageSourceRoot;
    if(S_ISREG(root.st_mode)){add(directory);directory=directory.substr(0,directory.find_last_of('/'));}
    else if(DIR* dir=opendir(directory.c_str())){
        while(auto* entry=readdir(dir)){std::string name=entry->d_name;auto lower=name;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});
            if(lower.size()>=4&&lower.substr(lower.size()-4)==".pfs")add(directory+"/"+name);}
        closedir(dir);
    }else return false;
    // Record all supported overrides. Creating/removing one changes identity,
    // including overrides that change which extension wins source resolution.
    for(const char* suffix:{".png","",".jpg",".jpeg",".dds",".pvr"})add(directory+"/"+key+suffix);
    auto dot=key.find_last_of('.');if(dot!=std::string::npos){add(directory+"/"+key.substr(0,dot)+".dds");add(directory+"/"+key.substr(0,dot)+".pvr");}
    if(records.empty())return false;
    std::sort(records.begin(),records.end());std::string description;for(const auto& record:records){description+=record;description+='\n';}
    identity={large_image_hash(reinterpret_cast<const uint8_t*>(description.data()),description.size()),description.size()};return true;
}
std::string path_index(const std::string& key){
    char leaf[64];std::snprintf(leaf,sizeof(leaf),"/path-v2-%016llx.idx",(unsigned long long)large_image_hash(reinterpret_cast<const uint8_t*>(key.data()),key.size()));return largeImageRoot+leaf;
}
void save_path_index(const char* name,const std::vector<uint8_t>& cached){
    const auto key=cpu_cache_folder(name);
    if(!largeImageSources.count(key)){SourceIdentity current{};if(source_identity(key,current))largeImageSources[key]=current;}
    auto source=largeImageSources.find(key);
    if(key.empty()||source==largeImageSources.end()||cached.size()<40)return;
    std::vector<uint8_t> index(cached.begin(),cached.begin()+40);
    large_image_put(index,source->second.hash,8);large_image_put(index,source->second.bytes,8);
    index.insert(index.end(),key.begin(),key.end());save_cache(path_index(key),index);
}
struct PrepareImage {const char* name;unsigned index,total;std::vector<uint8_t> header;};
int prepare_pixels(void* context,unsigned w,unsigned h,const uint8_t* rgba,size_t length) noexcept {
    try {
        auto& item=*static_cast<PrepareImage*>(context);
        if(!large_image_selected(item.name,w,h)||uint64_t(w)*h*4!=length)return 0;
        bool bc1=true;for(size_t i=3;i<length;i+=4)if(rgba[i]!=255){bc1=false;break;}
        auto source=large_image_hash(rgba,length);std::vector<uint8_t> cached;
        if(read_cache(w,h,source,bc1,cached)){item.header.assign(cached.begin(),cached.begin()+40);save_path_index(item.name,cached);largeImageReady[cpu_cache_folder(item.name)]={w,h,bc1};return 1;}
        std::vector<uint8_t>().swap(cached);
        const auto size=image_texture_size(w,h);
        std::vector<uint8_t> blocks(large_image_blocks(w,h,bc1));uint8_t tile[64];
        for(unsigned by=0;by<(size.h+3)/4;++by){
            for(unsigned bx=0;bx<(size.w+3)/4;++bx){
                for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)
                    image_texture_sample(rgba,w,h,size,bx*4+x,by*4+y,tile+(y*4+x)*4);
                stb_compress_dxt_block(blocks.data()+(size_t(by)*((size.w+3)/4)+bx)*(bc1?8:16),tile,bc1?0:1,STB_DXT_HIGHQUAL);
            }
            if((by&15)==0&&largeImageProgress)largeImageProgress(largeImageProgressContext,item.name,item.index*1000+(by+1)*1000/((size.h+3)/4),item.total*1000,1);
        }
        item.header=large_image_header(w,h,source,blocks,bc1);
        if(largeImageProgress)largeImageProgress(largeImageProgressContext,item.name,item.index,item.total,2);
        if(!save_cache(cache_path(w,h,source,bc1),item.header,blocks))return 0;
        save_path_index(item.name,item.header);
        largeImageReady[cpu_cache_folder(item.name)]={w,h,bc1};return 1;
    }catch(const std::bad_alloc&){return 0;}
}
}
extern "C" int art3m1s_prepare_large_image(const uint8_t*,size_t,uint64_t,void*,int(*)(void*,unsigned,unsigned,const uint8_t*,size_t));
void large_image_pregenerate(const std::string& root,LargeImageRead read) try {
    if(!largeImageEnabled||largeImageScope.automatic||!largeImageScope.pregenerate)return;
    if(largeImageProgress)largeImageProgress(largeImageProgressContext,"",0,0,0);
    bool scanFailed=false;auto paths=large_image_scan_files(root,scanFailed);
    paths.erase(std::remove_if(paths.begin(),paths.end(),[](const std::string& p){return !large_image_selected(p.c_str(),4096,4096);}),paths.end());
    unsigned ready=0,skipped=0,failed=0;
    for(unsigned i=0;i<paths.size();++i)try {
        const auto& name=paths[i];if(largeImageProgress)largeImageProgress(largeImageProgressContext,name.c_str(),i,paths.size(),0);
        const int n=read(name.c_str(),nullptr,0,-1);if(n<=0||n>64*1024*1024){++failed;continue;}
        std::vector<uint8_t> encoded(n);if(read(name.c_str(),encoded.data(),n,0)!=n){++failed;continue;}
        // Fingerprint the mounted source, not archive timestamps. This also detects
        // loose-file overrides and edits within an unchanged archive filename.
        char leaf[128];std::snprintf(leaf,sizeof(leaf),"/pre-v2-%016llx-%016llx-%u.idx",
            (unsigned long long)large_image_hash(reinterpret_cast<const uint8_t*>(name.data()),name.size()),
            (unsigned long long)large_image_hash(encoded.data(),encoded.size()),largeImageThreshold);
        const auto indexPath=largeImageRoot+leaf;std::vector<uint8_t> header(40);bool indexed=false;
        if(FILE* f=std::fopen(indexPath.c_str(),"rb")){indexed=std::fread(header.data(),1,40,f)==40&&std::fgetc(f)==EOF;std::fclose(f);}
        if(indexed&&!std::memcmp(header.data(),"SKIP",4)){++skipped;continue;}
        if(indexed&&!std::memcmp(header.data(),"BCI1",4)){
            std::vector<uint8_t> cached;const auto version=large_image_get(header.data()+4,4);
            if((version>=1&&version<=4)&&read_cache(large_image_get(header.data()+8,4),large_image_get(header.data()+12,4),large_image_get(header.data()+16,8),(version==2||version==4),cached)){largeImageReady[cpu_cache_folder(name)]={unsigned(large_image_get(header.data()+8,4)),unsigned(large_image_get(header.data()+12,4)),(version==2||version==4)};++ready;continue;}
        }
        PrepareImage item{name.c_str(),i,unsigned(paths.size()),{}};
        const int result=art3m1s_prepare_large_image(encoded.data(),encoded.size(),uint64_t(largeImageThreshold)*1024*1024,&item,prepare_pixels);
        if(result==2){header.assign(40,0);std::memcpy(header.data(),"SKIP",4);save_cache(indexPath,header);++skipped;}
        else if(result==1){save_cache(indexPath,item.header);++ready;}
        else ++failed;
    }catch(const std::bad_alloc&){++failed;}
    if(largeImageProgress)largeImageProgress(largeImageProgressContext,"",paths.size(),paths.size(),3);
    log("[large-image-prebuild] files=%u ready=%u skipped=%u failed=%u scan_failed=%d",unsigned(paths.size()),ready,skipped,failed,int(scanFailed));
}catch(const std::bad_alloc&){log("[large-image-prebuild] out of CPU memory; uncached images retain ordinary loading");}

// The path selects the index; source metadata invalidates replacements
// without reading or decoding pixels. The complete canonical path also guards hash collisions.
int large_image_probe(uint64_t id,const char* name,const uint8_t* encoded,size_t length,unsigned* info) try {
    if(largeImageRoot.empty()||!name)return -1;
    const auto key=cpu_cache_folder(name);if(key.empty())return -1;
    SourceIdentity source{};
    if(!source_identity(key,source)){
        if(!encoded||!length)return -1;
        source={large_image_hash(encoded,length),length};
    }
    largeImageSources[key]=source;
    std::vector<uint8_t> index(56+key.size());
    FILE* f=std::fopen(path_index(key).c_str(),"rb");if(!f)return -1;
    const bool valid=std::fread(index.data(),1,index.size(),f)==index.size()&&std::fgetc(f)==EOF;std::fclose(f);
    if(!valid||std::memcmp(index.data(),"BCI1",4)||large_image_get(index.data()+40,8)!=source.hash||large_image_get(index.data()+48,8)!=source.bytes||
       std::memcmp(index.data()+56,key.data(),key.size()))return -1;
    const unsigned w=large_image_get(index.data()+8,4),h=large_image_get(index.data()+12,4),version=large_image_get(index.data()+4,4);
    if(version<1||version>4||!large_image_selected(name,w,h))return -1;
    const bool bc1=version==2||version==4;std::vector<uint8_t> cached;
    if(!read_cache(w,h,large_image_get(index.data()+16,8),bc1,cached)||!std::equal(index.begin(),index.begin()+40,cached.begin()))return -1;
    const auto size=image_texture_size(w,h);
    info[0]=w;info[1]=h;info[2]=bc1;
    std::unique_ptr<Texture,void(*)(Texture*)> prepared(texture_bc3_prepare(size.w,size.h,bc1),destroy);if(!prepared)return 0;
    if(!texture_bc3_write(prepared.get(),cached.data()+40,cached.size()-40,bc1))return 0;
    auto* t=prepared.get();t->opaque=bc1;t->contentRevision=++textureRevision;
    auto* old=find(id);textures[id]=t;prepared.release();destroy(old);largeImageReady[key]={w,h,bc1};
    log("[large-image-path-hit] size=%ux%u texture=%ux%u gpu=%u source_check_bytes=%u decode_skipped=1",w,h,size.w,size.h,unsigned(t->allocation.bytes),unsigned(source.bytes));
    return int(t->allocation.bytes);
}catch(const std::bad_alloc&){return -1;}

int large_image_upload(uint64_t id,const char* name,unsigned w,unsigned h,const uint8_t* rgba,size_t length,const uint8_t* proof,size_t proofCount) try {
    if(!rgba||uint64_t(w)*h*4!=length||!large_image_selected(name,w,h))return -1;
    if(w>4096||h>4096){
        if(auto* original=texture_unfitted(w,h,rgba,proof,proofCount)){
            original->contentRevision=++textureRevision;auto* old=find(id);textures[id]=original;destroy(old);return int(original->allocation.bytes);
        }
    }
    auto found=largeImageReady.find(cpu_cache_folder(name));
    const bool known=found!=largeImageReady.end()&&found->second.w==w&&found->second.h==h;
    const bool runtimeGenerate=w>4096||h>4096||largeImageScope.automatic||!largeImageScope.pregenerate;
    if(!known&&!largeImageScope.automatic&&!runtimeGenerate)return -1;
    ImageCertificate certificate;const bool certified=read_image_certificate(w,h,proof,proofCount,certificate);
    bool bc1=certified&&certificate.opaque;
    if(!certified){bc1=true;for(size_t i=3;i<length;i+=4)if(rgba[i]!=255){bc1=false;break;}}
    std::vector<uint8_t> cached;uint64_t source=0;
    // Automatic mode does not scan at startup. Probe an existing content cache
    // on first use; only encode a miss after GPU storage is admitted.
    if(!known){
        source=large_image_hash(rgba,length);
        if(!read_cache(w,h,source,bc1,cached)){cached.clear();if(!runtimeGenerate)return -1;}
        if(!cached.empty())largeImageReady[cpu_cache_folder(name)]={w,h,bc1};
    }
    const auto size=image_texture_size(w,h);
    std::unique_ptr<Texture,void(*)(Texture*)> prepared(texture_bc3_prepare(size.w,size.h,bc1),destroy);
    if(!prepared)return 0;
    if(known)source=large_image_hash(rgba,length);
    bool generated=false;
    // Runtime generation is enabled for automatic and manual runtime modes.
    if(cached.empty()&&!read_cache(w,h,source,bc1,cached)){
        if(!runtimeGenerate)return -1;
        generated=true;PrepareImage item{name,0,1,{}};
        if(prepare_pixels(&item,w,h,rgba,length)!=1||!read_cache(w,h,source,bc1,cached))return -1;
    }
    save_path_index(name,cached);
    auto* t=prepared.get();if(!texture_bc3_write(t,cached.data()+40,cached.size()-40,bc1))return 0;
    t->opaque=bc1;t->contentRevision=++textureRevision;auto* old=find(id);textures[id]=t;prepared.release();destroy(old);
    log("[large-image] format=%s size=%ux%u texture=%ux%u gpu=%u cache_hit=%d",bc1?"BC1":"BC3",w,h,size.w,size.h,unsigned(t->allocation.bytes),int(!generated));
    return int(t->allocation.bytes);
}catch(const std::bad_alloc&){return -1;}
}
