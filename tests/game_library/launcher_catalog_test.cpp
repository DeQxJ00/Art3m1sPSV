#include "../../host-direct/src/launcher_catalog.hpp"
#include "../../host-direct/src/launcher_page_repeat.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>

extern "C" int art3m1s_launcher_decode_icon(const uint8_t* source,size_t,uint8_t* out,size_t size){
    std::fill_n(out,size,source[0]);
    return 1;
}
extern "C" int art3m1s_launcher_decode_cover(const uint8_t*,size_t,uint8_t* out,size_t size){
    std::fill_n(out,size,255);
    return 1;
}

static void wait_for_publish(direct::LauncherCatalog& catalog,uint64_t now){
    bool progressShown=false;
    for(int frame=0;frame<500;++frame){
        if(catalog.poll(now)){
            assert(progressShown);
            return;
        }
        progressShown=progressShown||catalog.loading().active;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    assert(false);
}

int main(){
    direct::LauncherPageRepeat repeat;
    assert(repeat.update(1,true,0)==1);
    assert(repeat.update(1,false,299999)==0);
    assert(repeat.update(1,false,300000)==1);
    assert(repeat.update(1,false,389999)==0);
    assert(repeat.update(1,false,390000)==1);
    assert(repeat.update(0,false,500000)==0);
    assert(repeat.update(-1,true,510000)==-1);
    repeat={};
    assert(repeat.update(-1,false,900000)==0);

    namespace fs=std::filesystem;
    const auto previous=fs::current_path();
    const auto root=fs::temp_directory_path()/("launcher-catalog-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    fs::current_path(root);
    const auto sample=fs::path(art3m1s::kGamesRoot)/"sample01";
    fs::create_directories(sample);
    std::ofstream(sample/"title.txt")<<"First title";
    std::ofstream(sample/"icon.png")<<"abcdefgh";
    std::ofstream(sample/"cover.png")<<"abcdefgh";
    direct::LauncherCatalog catalog;
    assert(catalog.loading().active);
    assert(!catalog.poll(0));
    wait_for_publish(catalog,0);
    assert(!catalog.loading().active);
    assert(catalog.games().size()==1&&catalog.games()[0].title=="First title");
    const auto first=*catalog.artwork("sample01");
    assert(first.icon->front()=='a'&&!first.cover->empty());

    std::ofstream(sample/"title.txt")<<"Second title";
    assert(!catalog.poll(4000000));
    assert(catalog.games()[0].title=="First title");
    wait_for_publish(catalog,4000000);
    assert(catalog.games()[0].title=="Second title");
    auto renamed=catalog.artwork("sample01");
    assert(renamed->icon==first.icon&&renamed->cover==first.cover);

    std::ofstream(sample/"icon.png")<<"bcdefghij";
    fs::remove(sample/"cover.png");
    wait_for_publish(catalog,8000000);
    auto updated=catalog.artwork("sample01");
    assert(updated->icon->front()=='b'&&!updated->cover);
    catalog.suspend();
    assert(catalog.loaded()&&catalog.artwork("sample01")->icon->front()=='b');

    fs::remove_all(sample);
    wait_for_publish(catalog,12000000);
    assert(catalog.games().empty()&&!catalog.artwork("sample01"));
    catalog.suspend();
    fs::current_path(previous);
    fs::remove_all(root);
}
