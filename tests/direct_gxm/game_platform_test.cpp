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
    auto r=direct::resolve_game_platform(root.string(),false);
    assert(std::string(r.name)=="VITA"&&r.inferred&&!r.saved&&!fs::exists(marker));
    r=direct::resolve_game_platform(root.string());
    assert(std::string(r.name)=="VITA"&&r.inferred&&r.saved&&r.error==0&&read()=="VITA\n");
    assert(!fs::exists(root/"platform.txt.auto.tmp"));
    r=direct::resolve_game_platform(root.string());
    assert(std::string(r.name)=="VITA"&&!r.inferred);
    for(const auto text:{"WINDOWS\r\n","windows\n","","OTHER\n","vita\r\n","psvita\n"}) {
        {std::ofstream f(marker);f<<text;}
        r=direct::resolve_game_platform(root.string());
        assert(!r.inferred&&read()==text);
        assert(std::string(r.name)==((std::string(text)=="WINDOWS\r\n"||std::string(text)=="windows\n")?"WINDOWS":"VITA"));
    }
    fs::remove(marker);
    fs::create_directory(root/"platform.txt.auto.tmp");
    r=direct::resolve_game_platform(root.string());
    assert(std::string(r.name)=="VITA"&&r.inferred&&!r.saved&&r.error!=0&&!fs::exists(marker));
    fs::remove(root/"platform.txt.auto.tmp");
    r=direct::resolve_game_platform(root.string());
    assert(r.saved&&read()=="VITA\n");
    fs::remove(marker);fs::remove(root);
    std::cout<<"PASS: universal VITA default, read-only assets, existing settings, ini compatibility and write retry\n";
}
