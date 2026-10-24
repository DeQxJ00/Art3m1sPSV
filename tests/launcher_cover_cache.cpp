#include "../host-direct/src/launcher_cover_loader.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <atomic>
static std::atomic<int> decodes{0};
extern "C" int art3m1s_launcher_decode_cover(const uint8_t*, size_t, uint8_t* rgba, size_t n){
    ++decodes;
    for(size_t i=0;i<n;i+=4){rgba[i]=255;rgba[i+1]=0;rgba[i+2]=0;rgba[i+3]=0;}
    return 1;
}
static direct::LauncherCoverLoader::Result load(direct::LauncherCoverLoader& loader,const art3m1s::GameEntry& game){
    assert(loader.request(game,3));direct::LauncherCoverLoader::Result result;
    for(int i=0;i<2000;++i){if(loader.take(result)){assert(result.slot==3);return result;}
        std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    assert(false);return {};
}
int main(){
    namespace fs=std::filesystem;
    auto root=fs::temp_directory_path()/ ("cover-cache-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);auto previous=fs::current_path();fs::current_path(root);
    fs::create_directories("ux0:data/art3m1s-gxm");fs::create_directory("source");
    std::ofstream("source/cover.png")<<"fixture-contents";
    art3m1s::GameEntry game;game.path="source";game.id="sample01";
    {
        direct::LauncherCoverLoader loader;
        auto first=load(loader,game);assert(decodes==1);assert(first.pixels.size()==65536);
        assert(fs::file_size("ux0:data/art3m1s-gxm/cover-cache/sample01.bc1")==23632);
        auto second=load(loader,game);assert(decodes==1);assert(first.pixels==second.pixels);
        // Transparent red must be flattened onto the blue-gray card, not red or black.
        unsigned color=first.pixels[0]|(unsigned(first.pixels[1])<<8);
        assert(((color>>11)&31)<8);assert((color&31)>3);
        std::ofstream("source/cover.png",std::ios::app)<<"changed";
        load(loader,game);assert(decodes==2);
        std::ofstream("ux0:data/art3m1s-gxm/cover-cache/sample01.bc1",std::ios::trunc)<<"broken";
        load(loader,game);assert(decodes==3);
        loader.cancel();
        assert(loader.request(game,2));
        loader.stop();
        direct::LauncherCoverLoader::Result discarded;
        assert(!loader.take(discarded));
        // Returning to the launcher can start a fresh worker and use disk cache.
        auto restarted=load(loader,game);assert(restarted.pixels==first.pixels);
        loader.stop();loader.stop();assert(!loader.take(discarded));
    }
    fs::current_path(previous);fs::remove_all(root);
}
