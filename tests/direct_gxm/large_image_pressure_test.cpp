#include <psp2/kernel/processmgr.h>
#include <psp2/io/stat.h>
#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include <cassert>
#include <iostream>
#include <filesystem>
#include "../../host-direct/src/image_certificate.hpp"
namespace direct {
struct Texture {struct {size_t bytes=0;} allocation;bool opaque=false;uint64_t contentRevision=0;};
static unsigned allocations=0,writes=0,lastWidth=0,lastHeight=0;
static bool allocationOK=false,lastBC1=false;static size_t lastLength=0;
Texture* texture_bc3_prepare(unsigned w,unsigned h,bool bc1){lastWidth=w;lastHeight=h;++allocations;lastBC1=bc1;if(!allocationOK)return nullptr;auto* t=new Texture;t->allocation.bytes=bc1?131072:262144;return t;}
bool texture_bc3_write(Texture*,const uint8_t*,size_t n,bool bc1){++writes;lastBC1=bc1;lastLength=n;return true;}
Texture* texture_unfitted(unsigned,unsigned,const uint8_t*,const uint8_t*,size_t){return nullptr;}
void destroy(Texture* t){delete t;}
int reserve_bc3(unsigned,unsigned,bool){return 0;}
void log(const char*,...){}
}
std::unordered_map<uint64_t,direct::Texture*> textures;
uint64_t textureRevision=0;
direct::Texture* find(uint64_t id){auto it=textures.find(id);return it==textures.end()?nullptr:it->second;}
extern "C" SceUInt64 sceKernelGetProcessTimeWide(){return 1;}
extern "C" int sceIoMkdir(const char*,SceMode){return 0;}
#include "../../host-direct/src/large_image_cache.inl"
static unsigned decodes=0,scans=0;
static std::vector<uint8_t> source(1,255);
namespace direct {std::vector<std::string> large_image_scan_files(const std::string&,bool& failed){++scans;failed=false;return {"image/bg/source.jpg","image/fg/source.png"};}}
extern "C" int art3m1s_prepare_large_image(const uint8_t* data,size_t,uint64_t,int,void* p,int(*cb)(void*,unsigned,unsigned,const uint8_t*,size_t)){
    ++decodes;std::vector<uint8_t> pixels(512*512*4,255);pixels[3]=data[0];return cb(p,512,512,pixels.data(),pixels.size());
}
int read_source(const char*,uint8_t* out,int,int64_t offset){if(offset<0)return source.size();std::copy(source.begin(),source.end(),out);return source.size();}
int main(){
    direct::large_image_configure(true,"/path-not-read",8);
    direct::largeImageReady["image/bg/a.png"]={2048,1024,false};
    // Deliberately unreadable source: failed GPU admission must return before
    // hashing, compression, source-alpha scanning, or cache access.
    std::vector<uint8_t> proof(direct::imageCertificateHeader+32*16,0);
    direct::write_image_certificate(proof.data(),2048,1024,{0,0,2048,1024,true},false);
    for(unsigned i=0;i<5;++i)assert(direct::large_image_upload(1,"image/bg/a.png",2048,1024,reinterpret_cast<const uint8_t*>(1),8*1024*1024,proof.data(),proof.size())==0);
    assert(direct::allocations==5&&direct::writes==0&&textures.empty());
    direct::LargeImageScope scope;scope.automatic=false;
    direct::large_image_configure(true,"/path-not-read",8,scope);
    assert(direct::large_image_upload(1,"image/fg/a.png",2048,1024,reinterpret_cast<const uint8_t*>(1),8*1024*1024)==-1);
    assert(direct::allocations==5);
    auto root=std::filesystem::temp_directory_path()/"art3-bc-selection-test";
    std::filesystem::create_directories(root);
    direct::large_image_configure(true,root.string(),1);
    direct::allocationOK=true;
    std::vector<uint8_t> pixels(512*512*4,255);
    direct::PrepareImage item{"image/bg/opaque.png",0,1,{}};
    assert(direct::prepare_pixels(&item,512,512,pixels.data(),pixels.size())==1);
    assert(direct::large_image_upload(2,"image/bg/opaque.png",512,512,pixels.data(),pixels.size())>0);
    assert(direct::lastBC1&&direct::lastLength==512*512/2&&textures[2]->opaque);
    assert(direct::large_image_upload(2,"image/bg/opaque.png",512,512,pixels.data(),pixels.size())>0);
    pixels[3]=128;
    item.name="image/bg/alpha.png";assert(direct::prepare_pixels(&item,512,512,pixels.data(),pixels.size())==1);
    assert(direct::large_image_upload(2,"image/bg/alpha.png",512,512,pixels.data(),pixels.size())>0);
    assert(!direct::lastBC1&&direct::lastLength==512*512&&!textures[2]->opaque);
    assert(direct::large_image_upload(2,"image/bg/alpha.png",512,512,pixels.data(),pixels.size())>0);
    direct::destroy(textures[2]);textures.clear();
    pixels[3]=123;direct::large_image_configure(true,root.string(),1,scope);
    auto before=std::distance(std::filesystem::directory_iterator(root),std::filesystem::directory_iterator());
    assert(direct::large_image_upload(3,"image/bg/missing.png",512,512,pixels.data(),pixels.size())==-1);
    assert(before==std::distance(std::filesystem::directory_iterator(root),std::filesystem::directory_iterator()));
    unsigned allocations=direct::allocations;
    direct::large_image_configure(true,root.string(),1,scope);
    direct::large_image_pregenerate("unused",read_source);assert(decodes==1);
    direct::large_image_pregenerate("unused",read_source);assert(decodes==1);
    source[0]=127;direct::large_image_pregenerate("unused",read_source);assert(decodes==2);
    direct::large_image_configure(true,root.string(),1);
    auto oldScans=scans;
    direct::large_image_pregenerate("unused",read_source);assert(decodes==2&&scans==oldScans);
    pixels.assign(512*512*4,255);
    assert(direct::large_image_upload(4,"image/bg/opaque.png",512,512,pixels.data(),pixels.size())>0);
    direct::destroy(textures[4]);textures.clear();
    pixels[3]=97;assert(direct::large_image_upload(4,"image/ev/auto.png",512,512,pixels.data(),pixels.size())>0);
    direct::destroy(textures[4]);textures.clear();allocations=direct::allocations;
    direct::large_image_configure(true,root.string(),1,scope);
    for(const auto& entry:std::filesystem::directory_iterator(root))if(entry.path().extension()==".bc3")std::filesystem::resize_file(entry.path(),2);
    direct::large_image_pregenerate("unused",read_source);assert(decodes==3);
    assert(direct::allocations==allocations); // startup never allocates GPU memory
    scope.pregenerate=false;direct::large_image_configure(true,root.string(),1,scope);
    auto beforeLive=scans;direct::large_image_pregenerate("unused",read_source);assert(scans==beforeLive);
    pixels[3]=111;
    assert(direct::large_image_upload(5,"image/fg/not-selected.png",512,512,pixels.data(),pixels.size())==-1);
    assert(direct::large_image_upload(5,"image/bg/realtime.png",512,512,pixels.data(),pixels.size())>0);
    assert(!direct::lastBC1);direct::destroy(textures[5]);textures.clear();
    direct::large_image_configure(true,root.string(),1);
    pixels.assign(4100*128*4,255);pixels[3]=0;
    assert(direct::large_image_upload(6,"image/bg/tall.png",4100,128,pixels.data(),pixels.size())>0);
    assert(direct::lastWidth==4096&&direct::lastHeight==127&&!direct::lastBC1);
    assert(direct::lastLength==size_t(1024)*32*16);
    const auto cache=direct::cache_path(4100,128,direct::large_image_hash(pixels.data(),pixels.size()),false);
    const auto modified=std::filesystem::last_write_time(cache);
    direct::large_image_configure(true,root.string(),1); // new launch, no in-memory index
    assert(direct::large_image_upload(6,"image/bg/tall.png",4100,128,pixels.data(),pixels.size())>0);
    assert(std::filesystem::last_write_time(cache)==modified);
    direct::destroy(textures[6]);textures.clear();
    direct::large_image_configure(true,root.string(),1);
    unsigned info[3]={};const uint8_t encoded[]={1,2,3,4};
    assert(direct::large_image_probe(7,"IMAGE\\BG\\indexed.png",encoded,sizeof(encoded),info)==-1);
    assert(direct::large_image_upload(7,"image/bg/indexed.png",4100,128,pixels.data(),pixels.size())>0);
    direct::large_image_configure(true,root.string(),1);
    assert(direct::large_image_probe(7,"image/bg/indexed.png",encoded,sizeof(encoded),info)>0);
    assert(info[0]==4100&&info[1]==128&&info[2]==0);
    const uint8_t changed[]={1,2,3,5};
    assert(direct::large_image_probe(7,"image/bg/indexed.png",changed,sizeof(changed),info)==-1);
    assert(direct::large_image_probe(7,"image/bg/other.png",encoded,sizeof(encoded),info)==-1);
    assert(direct::large_image_probe(7,"../image/bg/indexed.png",encoded,sizeof(encoded),info)==-1);
    direct::allocationOK=false;
    assert(direct::large_image_probe(7,"image/bg/indexed.png",encoded,sizeof(encoded),info)==0);
    direct::allocationOK=true;
    std::filesystem::resize_file(direct::path_index("image/bg/indexed.png"),3);
    assert(direct::large_image_probe(7,"image/bg/indexed.png",encoded,sizeof(encoded),info)==-1);
    direct::destroy(textures[7]);textures.clear();
    const auto sourceRoot=root/"source";std::filesystem::create_directory(sourceRoot);
    const auto archive=sourceRoot/"root.pfs";
    {FILE* f=std::fopen(archive.string().c_str(),"wb");std::fwrite(encoded,1,sizeof(encoded),f);std::fclose(f);}
    direct::large_image_configure(true,root.string(),1,{},sourceRoot.string());
    assert(direct::large_image_probe(8,"image/bg/meta.png",nullptr,0,info)==-1);
    assert(direct::large_image_upload(8,"image/bg/meta.png",4100,128,pixels.data(),pixels.size())>0);
    direct::large_image_configure(false,root.string(),64,{},sourceRoot.string());
    assert(direct::large_image_probe(8,"image/bg/meta.png",nullptr,0,info)>0); // oversized fallback ignores ordinary settings
    std::filesystem::resize_file(archive,5);
    assert(direct::large_image_probe(8,"image/bg/meta.png",nullptr,0,info)==-1);
    direct::destroy(textures[8]);textures.clear();
    std::filesystem::remove(archive);std::filesystem::remove(sourceRoot);
    direct::large_image_configure(false,root.string(),1);
    direct::large_image_pregenerate("unused",read_source);assert(decodes==3);
    for(const auto& entry:std::filesystem::directory_iterator(root))std::filesystem::remove(entry.path());
    std::filesystem::remove(root);
    std::cout<<"PASS startup scope, source invalidation, cache repair, no GPU allocation, runtime generation and startup-only mode isolation\n";
}
