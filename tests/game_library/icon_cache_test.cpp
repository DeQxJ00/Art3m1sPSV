#include "launcher_icon_cache.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>
#include <utime.h>
static int reads=0;
extern "C" int art3m1s_launcher_decode_icon(const uint8_t* p,size_t n,uint8_t* out,size_t count){
    if(n<8||p[0]!='P')return 0;std::fill(out,out+count,77);return 1;
}
// Exercise selection/caching independently of the already-tested PE decoder.
extern "C" int art3m1s_launcher_extract_exe_icon(const uint8_t* p,size_t n,uint8_t* out,size_t count){
    ++reads;if(n<64||p[0]!='I')return 0;std::fill(out,out+count,p[1]);return 1;
}
static void fixture(const std::string& path,char kind,char value=10){
    std::string bytes(64,0);bytes[0]=kind;bytes[1]=value;std::ofstream f(path,std::ios::binary);f<<bytes;
}
int main(){
    namespace fs=std::filesystem;using namespace direct;
    char root[]="/tmp/art3-icon-XXXXXX";assert(mkdtemp(root));
    art3m1s::GameEntry g;g.id="Title with spaces";g.path=std::string(root)+"/game";fs::create_directory(g.path);
    const auto a=g.path+"/Game.EXE",b=g.path+"/helper.exe",c=g.path+"/other.exe";
    fixture(a,'I',10);fixture(b,'N');g.exe_candidates={b,a};
    std::array<uint8_t,launcherIconBytes> rgba{};
    assert(launcher_icon_generate_cache(g,root));assert(reads==2);
    assert(launcher_icon_read_cache(g,rgba,nullptr,root)&&rgba[0]==10);
    assert(launcher_icon_generate_cache(g,root)&&reads==2); // no reparse on cache hit
    std::reverse(g.exe_candidates.begin(),g.exe_candidates.end());
    assert(launcher_icon_generate_cache(g,root)&&reads==2);
    fixture(c,'I',20);g.exe_candidates.push_back(c);
    assert(!launcher_icon_generate_cache(g,root)); // two usable icons: no guess or stale cache
    assert(!launcher_icon_read_cache(g,rgba,nullptr,root));
    g.matching_exe=a;
    assert(launcher_icon_generate_cache(g,root));assert(launcher_icon_read_cache(g,rgba,nullptr,root)&&rgba[0]==10);
    g.matching_exe=b; // matching EXE without icon does not trigger arbitrary fallback
    assert(!launcher_icon_generate_cache(g,root));assert(!launcher_icon_read_cache(g,rgba,nullptr,root));
    g.matching_exe.clear();g.exe_candidates={a,b};assert(launcher_icon_generate_cache(g,root));
    struct stat st{};assert(!stat(a.c_str(),&st));utimbuf times{st.st_atime,st.st_mtime};assert(!utime(c.c_str(),&times));
    g.exe_candidates={c,b};assert(launcher_icon_generate_cache(g,root)); // same size/time, different source
    assert(launcher_icon_read_cache(g,rgba,nullptr,root)&&rgba[0]==20);
    g.exe_candidates={a,c};fixture(g.path+"/icon.png",'P');int before=reads;
    assert(!launcher_icon_generate_cache(g,root)&&reads==before); // PNG has priority, no EXE scan
    assert(launcher_icon_read_png(g,rgba)&&rgba[0]==77);
    fs::remove(g.path+"/icon.png");g.exe_candidates={b};
    assert(!launcher_icon_generate_cache(g,root));assert(!launcher_icon_read_cache(g,rgba,nullptr,root));
    // Missing/unreadable candidates must not establish false uniqueness.
    g.exe_candidates={a,g.path+"/missing.exe"};assert(!launcher_icon_generate_cache(g,root));
    fs::remove_all(root);
    std::cout<<"PASS: PNG/matching/unique precedence, ambiguous/no-icon fallback, cache reuse and source-set invalidation\n";
}
