#include "../../host-direct/src/game_platform.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc,char** argv) {
    namespace fs=std::filesystem;
    assert(argc==2);
    const fs::path root=argv[1];
    assert(fs::create_directory(root));
    const auto marker=root/"platform.txt";
    auto read=[&]{std::ifstream f(marker);return std::string(std::istreambuf_iterator<char>(f),{});};
    assert(!direct::has_vita_section("; [VITA]\n[VITA_EXTRA]\nvalue=[VITA]\n[VITA]junk\n"));
    assert(direct::has_vita_section("\xef\xbb\xbf \t[VITA] ; native\r\n"));
    std::string ini="[WINDOWS]\rWIDTH=960\rHEIGHT=540\rBOOT=x\rCHARSET=UTF-8\r[ANDROID]\nWIDTH=640\n";
    const auto original=ini;
    assert(direct::add_vita_section(ini));
    assert(ini.substr(0,original.size())==original);
    const auto vita=direct::ini_section(ini,"VITA");
    assert(vita.find("WIDTH=960")!=vita.npos&&vita.find("HEIGHT=540")!=vita.npos);
    assert(vita.find("CHARSET=UTF-8")!=vita.npos&&vita.find("640")==vita.npos);
    const auto once=ini;assert(!direct::add_vita_section(ini)&&ini==once);
    ini="[ANDROID]\nWIDTH=800\nHEIGHT=450\nBOOT=system/first.iet\nCHARSET=Shift_JIS\nX=\x81\x40\n";
    assert(direct::add_vita_section(ini));
    assert(direct::ini_section(ini,"VITA").find("X=\x81\x40")!=std::string_view::npos);
    ini="[VITA]\nWIDTH=123\n";assert(!direct::add_vita_section(ini));
    ini="[ vita ]\nWIDTH=456\n";assert(!direct::add_vita_section(ini));
    ini="[UNKNOWN]\nX=1\n";assert(!direct::add_vita_section(ini));
    // Each selectable platform keeps a native BOOT or inherits one in priority order.
    const char* sources[]={"WINDOWS","VITA","SWITCH","ANDROID","IOS","PS4"};
    for(const auto target:direct::kGamePlatforms) {
        ini=std::string("[")+target+"]\nBOOT=native.iet\nWIDTH=960\nHEIGHT=540\n";
        const auto native=ini;
        assert(!direct::add_platform_section(ini,target)&&ini==native);
        for(unsigned start=0;start<6;++start) {
            ini.clear();const char* first=nullptr;
            for(unsigned i=start;i<6;++i)if(std::string(sources[i])!=target) {
                if(!first)first=sources[i];
                ini+=std::string("[")+sources[i]+"]\nBOOT="+sources[i]+".iet\nCHARSET=UTF-8\nWIDTH=1920\nHEIGHT=1080\n";
            }
            const bool added=direct::add_platform_section(ini,target);
            assert(added==(first!=nullptr));
            if(added) {
                const auto selected=direct::ini_section(ini,target);
                assert(selected.find(std::string("BOOT=")+first+".iet")!=selected.npos);
                assert(selected.find("WIDTH=960\nHEIGHT=540")!=selected.npos);
            }
        }
    }
    auto r=direct::resolve_game_platform(root.string());
    assert(std::string(r.name)=="VITA"&&r.inferred&&r.error==0&&!fs::exists(marker));
    assert(direct::save_game_platform(root.string(),true));
    assert(read()=="WINDOWS\n");
    assert(std::string(direct::resolve_game_platform(root.string()).name)=="WINDOWS");
    const auto other=root/"other";fs::create_directory(other);
    assert(std::string(direct::resolve_game_platform(other.string()).name)=="VITA");
    assert(!fs::exists(other/"platform.txt"));fs::remove(other);
    assert(direct::save_game_platform(root.string(),false));
    assert(read()=="VITA\n");
    for(unsigned i=0;i<direct::kGamePlatformCount;++i) {
        assert(direct::save_game_platform(root.string(),i));
        assert(read()==std::string(direct::kGamePlatforms[i])+"\n");
        assert(std::string(direct::resolve_game_platform(root.string()).name)==direct::kGamePlatforms[i]);
        std::string lower=direct::kGamePlatforms[i];
        for(char& c:lower)if(c>='A'&&c<='Z')c+=32;
        {std::ofstream f(marker);f<<lower;}
        assert(std::string(direct::resolve_game_platform(root.string()).name)==direct::kGamePlatforms[i]);
        assert(direct::step_game_platform(direct::step_game_platform(i,false),true)==i);
    }
    const auto saved=read();assert(!direct::save_game_platform(root.string(),6)&&read()==saved);
    assert(direct::step_game_platform(0,true)==5&&direct::step_game_platform(5,false)==0);
    for(const auto text:{"WINDOWS\r\n","windows\n","","OTHER\n","vita\r\n","psvita\n"}) {
        {std::ofstream f(marker);f<<text;}
        r=direct::resolve_game_platform(root.string());
        assert(!r.inferred&&read()==text);
        assert(std::string(r.name)==((std::string(text)=="WINDOWS\r\n"||std::string(text)=="windows\n")?"WINDOWS":"VITA"));
    }
    assert(direct::save_game_platform(root.string(),true));
    fs::create_directory(root/"platform.txt.tmp");
    assert(!direct::save_game_platform(root.string(),false));assert(read()=="WINDOWS\n");
    fs::remove(root/"platform.txt.tmp");
    fs::rename(marker,root/"platform.txt.bak");
    assert(std::string(direct::resolve_game_platform(root.string()).name)=="WINDOWS");
    assert(direct::save_game_platform(root.string(),false));
    assert(!fs::exists(root/"platform.txt.bak")&&read()=="VITA\n");
    fs::remove(marker);fs::create_directory(marker);
    assert(!direct::save_game_platform(root.string(),true)&&fs::is_directory(marker));
    fs::remove(marker);fs::remove(root);
    auto size=direct::vita_resolution("[WINDOWS]\nWIDTH=1920\nHEIGHT=1080\n");
    assert(size.width==960&&size.height==540);
    size=direct::vita_resolution("[VITA]\rwidth = 960\rHEIGHT=544\r");
    assert(size.width==960&&size.height==544);
    size=direct::vita_resolution("[VITA]\nWIDTH=-1\nHEIGHT=999999\n");
    assert(size.width==960&&size.height==540);
    std::cout<<"PASS: optional Vita default, per-game platform choice, explicit settings, ini compatibility and write recovery\n";
}
