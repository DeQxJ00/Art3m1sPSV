#include "../../host-direct/src/launcher_catalog.hpp"
#include "../../host-direct/src/launcher_page_repeat.hpp"
#include "../../host-direct/src/menu_glyph_cache.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>

static unsigned iconDecodes=0,coverDecodes=0;
static std::vector<uint8_t> iconOrder;

extern "C" int art3m1s_launcher_decode_icon(const uint8_t* source,size_t,uint8_t* out,size_t size){
    ++iconDecodes;
    iconOrder.push_back(source[0]);
    std::fill_n(out,size,source[0]);
    return 1;
}
extern "C" int art3m1s_launcher_decode_cover(const uint8_t*,size_t,uint8_t* out,size_t size){
    ++coverDecodes;
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

static void verify_unchanged(direct::LauncherCatalog& catalog,uint64_t now){
    for(int frame=0;frame<30;++frame){
        assert(!catalog.poll(now)&&!catalog.loading().active);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    catalog.suspend();
}

static void wait_for_artwork(direct::LauncherCatalog& catalog,const std::string& id,bool cover){
    for(int frame=0;frame<500;++frame){
        catalog.collect_artwork();
        const auto* art=catalog.artwork(id);
        if(art&&art->iconLoaded&&(!cover||art->coverLoaded))return;
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
    {
        direct::MenuGlyphCache glyphs;
        const uint64_t key=(uint64_t(24)<<32)|0x6e38;
        assert(glyphs.insert(key,{2,2,-1,3,16,{0,80,180,255}}));
        assert(glyphs.save("glyphs.bin","glyphs.tmp",42));
        const auto restored=direct::MenuGlyphCache::load("glyphs.bin",42);
        const auto* glyph=restored.find(key);
        assert(glyph&&glyph->left==-1&&glyph->top==3&&glyph->advance==16);
        assert((glyph->alpha==std::vector<uint8_t>{0,80,180,255}));
        assert(!direct::MenuGlyphCache::load("glyphs.bin",43).find(key));
        fs::resize_file("glyphs.bin",fs::file_size("glyphs.bin")-1);
        assert(!direct::MenuGlyphCache::load("glyphs.bin",42).find(key));
    }
    const auto sample=fs::path(art3m1s::kGamesRoot)/"sample01";
    fs::create_directories(sample);
    std::ofstream(sample/"title.txt")<<"First title";
    std::ofstream(sample/"icon.png")<<"abcdefgh";
    std::ofstream(sample/"cover.png")<<"abcdefgh";
    direct::LauncherCatalog catalog;
    assert(catalog.loading().active);
    assert(!catalog.poll(0));
    wait_for_publish(catalog,0);
    assert(catalog.reindexed()&&!catalog.loading().active);
    assert(catalog.games().size()==1&&catalog.games()[0].title=="First title");
    assert(iconDecodes==0&&coverDecodes==0);
    catalog.prioritize_artwork(catalog.games(),0,5,true);
    wait_for_artwork(catalog,"sample01",true);
    const auto first=*catalog.artwork("sample01");
    assert(first.icon.front()=='a'&&!first.cover.empty());
    catalog.suspend();
    {
        // A new launcher process can show the previous snapshot before it reconciles disk changes.
        fs::rename(sample,sample.parent_path()/"sample02");
        direct::LauncherCatalog restarted;
        wait_for_publish(restarted,0);
        assert(!restarted.reindexed());
        assert(restarted.games()[0].id=="sample01");
        assert(!restarted.artwork("sample01"));
        assert(iconDecodes==1&&coverDecodes==1);
        wait_for_publish(restarted,4000000);
        assert(restarted.reindexed());
        assert(restarted.games().size()==1&&restarted.games()[0].id=="sample02");
        fs::rename(sample.parent_path()/"sample02",sample);
    }

    // Build a fresh folder baseline after restoring the original directory name.
    wait_for_publish(catalog,4000000);
    const auto folders=art3m1s::game_folder_stamp();
    std::ofstream(sample/"title.txt")<<"Second title";
    assert(folders&&art3m1s::game_folder_stamp()==folders);
    verify_unchanged(catalog,8000000);
    assert(catalog.games()[0].title=="First title");
    std::ofstream(sample/"system.ini")<<"[VITA]";
    wait_for_publish(catalog,12000000);
    assert(catalog.games()[0].title=="Second title");
    assert(catalog.games()[0].ready());
    assert(!catalog.artwork("sample01"));

    std::ofstream(sample/"icon.png")<<"bcdefghij";
    fs::remove(sample/"cover.png");
    wait_for_publish(catalog,16000000);
    catalog.prioritize_artwork(catalog.games(),0,5,true);
    wait_for_artwork(catalog,"sample01",true);
    auto updated=catalog.artwork("sample01");
    assert(updated->icon.front()=='b'&&updated->cover.empty());
    catalog.suspend();
    assert(catalog.loaded()&&catalog.artwork("sample01")->icon.front()=='b');
    catalog.release_for_game();
    assert(!catalog.loaded()&&catalog.games().empty()&&!catalog.artwork("sample01"));
    assert(catalog.loading().active);
    std::ofstream(sample/"title.txt")<<"Changed while game was open";
    wait_for_publish(catalog,17000000);
    assert(catalog.games().size()==1&&catalog.games()[0].title=="Second title");
    assert(!catalog.artwork("sample01"));
    catalog.prioritize_artwork(catalog.games(),0,5,false);
    wait_for_artwork(catalog,"sample01",false);
    assert(catalog.artwork("sample01")->icon.front()=='b');

    fs::remove_all(sample);
    wait_for_publish(catalog,20000000);
    assert(catalog.games().empty()&&!catalog.artwork("sample01"));
    catalog.suspend();
    std::ofstream(std::string(art3m1s::kDataRoot)+"/library-catalog.bin",std::ios::trunc)<<"bad cache";
    {
        direct::LauncherCatalog rebuilt;
        wait_for_publish(rebuilt,0);
        assert(rebuilt.games().empty()&&!rebuilt.loading().active);
    }

    for(char i='0';i<='4';++i){
        auto path=sample.parent_path()/std::string("sample0").append(1,i);
        fs::create_directory(path);
        std::ofstream(path/"icon.png")<<i<<"fixture";
    }
    wait_for_publish(catalog,24000000);
    iconOrder.clear();
    catalog.prioritize_artwork(catalog.games(),2,1,false);
    wait_for_artwork(catalog,"sample00",false);
    catalog.suspend();
    assert((iconOrder==std::vector<uint8_t>{'2','3','1','4','0'}));
    fs::current_path(previous);
    fs::remove_all(root);
}
