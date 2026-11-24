#include "../../host-direct/src/launcher_about.hpp"
#include "../../host-direct/src/launcher_icons.hpp"
#include "../../host-direct/src/launcher_library.hpp"
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>

namespace direct {
static unsigned made=0,destroyed=0,prepared=0,extracted=0;
Texture* texture(unsigned,unsigned,const uint8_t*,const uint8_t*,size_t){++made;return new Texture;}
void destroy(Texture* image){++destroyed;delete image;}
void menu_prepare(const char*,float){++prepared;}
float menu_text_width(const char*,float){return 100;}
}
extern "C" int art3m1s_launcher_decode_icon(const uint8_t* png,size_t length,uint8_t* rgba,size_t output){
    if(length!=8||png[0]!=0x89||output!=48*48*4)return 0;
    std::fill(rgba,rgba+output,255);return 1;
}
extern "C" int art3m1s_launcher_extract_exe_icon(const uint8_t* exe,size_t length,uint8_t* rgba,size_t output){
    ++direct::extracted;
    if(length<64||exe[0]!='M'||exe[1]!='Z'||output!=48*48*4)return 0;
    std::fill(rgba,rgba+output,127);return 1;
}

int main(){
    namespace fs=std::filesystem;
    using namespace direct;
    LauncherLibrary library;
    library.covers=false;
    size_t selection=3;
    const auto move=[&](uint32_t held,uint32_t pressed,uint64_t now){
        library.navigate(library.repeat_buttons(held,pressed,now),selection,30);
    };
    move(SCE_CTRL_DOWN,SCE_CTRL_DOWN,0);
    assert(selection==4);
    move(SCE_CTRL_DOWN,0,299999);
    assert(selection==4);
    move(SCE_CTRL_DOWN,0,300000);
    assert(selection==5);
    move(SCE_CTRL_DOWN,0,390000);
    assert(selection==6);
    move(0,0,480000);
    assert(selection==6);
    move(SCE_CTRL_UP,SCE_CTRL_UP,490000);
    move(SCE_CTRL_UP,0,790000);
    assert(selection==4);
    move(SCE_CTRL_LEFT,SCE_CTRL_LEFT,800000);
    move(SCE_CTRL_LEFT,0,1100000);
    assert(selection==4);

    library.covers=true;
    move(SCE_CTRL_RIGHT,SCE_CTRL_RIGHT,1200000);
    move(SCE_CTRL_RIGHT,0,1500000);
    assert(selection==6);
    move(SCE_CTRL_DOWN,SCE_CTRL_DOWN,1600000);
    move(SCE_CTRL_DOWN,0,1900000);
    assert(selection==12);
    move(SCE_CTRL_LEFT,SCE_CTRL_LEFT,2000000);
    move(SCE_CTRL_LEFT,0,2300000);
    assert(selection==10);
    move(SCE_CTRL_UP,SCE_CTRL_UP,2400000);
    move(SCE_CTRL_UP,0,2700000);
    assert(selection==4);

    move(SCE_CTRL_RTRIGGER,SCE_CTRL_RTRIGGER,2800000);
    assert(selection==10);
    move(SCE_CTRL_RTRIGGER|SCE_CTRL_RIGHT,SCE_CTRL_RIGHT,3100000);
    assert(selection==11);
    move(SCE_CTRL_RTRIGGER,0,3190000);
    assert(selection==17);

    const auto root=fs::temp_directory_path()/"art3m1s-launcher-ui-test";
    fs::create_directories(root);
    std::vector<art3m1s::GameEntry> games(6);
    for(size_t i=0;i<games.size();++i){
        games[i].id="game_"+std::to_string(i);
        games[i].title="Game "+std::to_string(i);
        games[i].path=(root/games[i].id).string();fs::create_directories(games[i].path);
    }
    const unsigned char sample[8]={0x89,'P','N','G',13,10,26,10};
    {std::ofstream file(games[0].path+"/icon.png",std::ios::binary);
        file.write(reinterpret_cast<const char*>(sample),sizeof(sample));}
    {std::ofstream file(games[1].path+"/icon.png",std::ios::binary);
        file.write("invalid!",8);}
    {std::ofstream file(games[2].path+"/saveicon.png",std::ios::binary);
        file.write(reinterpret_cast<const char*>(sample),sizeof(sample));}
    LauncherIcons icons;
    std::array<std::array<uint8_t,launcherIconBytes>,3> decoded{};
    std::array<const uint8_t*,6> pixels{};
    for(size_t i=0;i<decoded.size();++i)
        if(launcher_icon_read_png(games[i],decoded[i]))pixels[i]=decoded[i].data();
    icons.prepare(games,0,5,pixels);
    assert(icons.images[0]&&icons.images[1]==nullptr&&icons.images[2]&&icons.first==0);
    assert(icons.initials[0]=="G"&&made==2&&prepared==5);
    icons.prepare(games,4,5,pixels);assert(made==2&&destroyed==0);
    icons.prepare(games,5,5,{});assert(icons.first==5&&destroyed==2&&!icons.images[0]);
    icons.clear();assert(icons.first==size_t(-1));

    LauncherAbout about;SceTouchData touch{};
    assert(!about.input(0,false,touch));
    assert(about.input(SCE_CTRL_CROSS,false,touch)==1);
    assert(about.input(SCE_CTRL_CIRCLE,false,touch)==1);
    assert(about.input(SCE_CTRL_SELECT,false,touch)==1);
    touch.report[0].x=800*2;touch.report[0].y=500*2;
    assert(about.input(0,true,touch)==1);
    touch.report[0].x=100*2;assert(!about.input(0,true,touch));
    const auto exe=root/"game_5.exe";
    games[5].matching_exe=exe.string();
    {std::ofstream file(exe,std::ios::binary);std::string bytes(128,'x');bytes[0]='M';bytes[1]='Z';file.write(bytes.data(),bytes.size());}
    const std::string cacheRoot=root.string();
    std::array<uint8_t,launcherIconBytes> cached{};LauncherIconCacheHeader header{};
    assert(launcher_icon_generate_cache(games[5],cacheRoot.c_str()));
    assert(extracted==1&&launcher_icon_read_cache(games[5],cached,&header,cacheRoot.c_str()));
    assert(header.sourceSize==128&&cached[0]==127);
    assert(launcher_icon_generate_cache(games[5],cacheRoot.c_str())&&extracted==1);
    {std::ofstream file(exe,std::ios::binary|std::ios::app);file.put('y');}
    assert(launcher_icon_generate_cache(games[5],cacheRoot.c_str())&&extracted==2);
    assert(launcher_icon_read_cache(games[5],cached,&header,cacheRoot.c_str())&&header.sourceSize==129);
    {std::ofstream file(games[5].path+"/icon.png",std::ios::binary);
        file.write(reinterpret_cast<const char*>(sample),sizeof(sample));}
    assert(!launcher_icon_generate_cache(games[5],cacheRoot.c_str())&&extracted==2);
    fs::remove(games[5].path+"/icon.png");
    fs::remove(exe);
    fs::remove(launcher_icon_cache_path(games[5],cacheRoot.c_str()));
    fs::remove(root/"icon-cache");
    fs::remove(games[0].path+"/icon.png");fs::remove(games[1].path+"/icon.png");
    fs::remove(games[2].path+"/saveicon.png");
    for(const auto& game:games)fs::remove(game.path);
    fs::remove(root);
}
