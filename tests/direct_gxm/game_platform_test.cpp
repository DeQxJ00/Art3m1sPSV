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
    const std::string ini="[WINDOWS]\nBOOT=x\n[VITA]\nBOOT=x\n";
    int probes=0;
    auto table=[&]{++probes;return true;};
    auto read=[&]{std::ifstream f(marker);return std::string(std::istreambuf_iterator<char>(f),{});};
    assert(!direct::has_vita_section("; [VITA]\n[VITA_EXTRA]\nvalue=[VITA]\n[VITA]junk\n"));
    assert(direct::has_vita_section("\xef\xbb\xbf \t[VITA] ; native\r\n"));
    auto untouched=direct::resolve_game_platform(root.string(),false,ini,table);
    assert(std::string(untouched.name)=="WINDOWS"&&!untouched.inferred&&!fs::exists(marker)&&probes==0);
    auto r=direct::resolve_game_platform(root.string(),true,"[WINDOWS]\n",table);
    assert(std::string(r.name)=="WINDOWS"&&!r.inferred&&!fs::exists(marker)&&probes==0);
    r=direct::resolve_game_platform(root.string(),true,ini,[]{return false;});
    assert(!r.inferred&&!fs::exists(marker));
    r=direct::resolve_game_platform(root.string(),true,ini,table);
    assert(std::string(r.name)=="VITA"&&r.inferred&&r.saved&&r.error==0&&read()=="VITA\n");
    assert(!fs::exists(root/"platform.txt.auto.tmp"));
    const int before=probes;
    r=direct::resolve_game_platform(root.string(),true,ini,table);
    assert(std::string(r.name)=="VITA"&&!r.inferred&&probes==before);
    for(const auto text:{"WINDOWS\r\n","","OTHER\n","vita\r\n"}) {
        {std::ofstream f(marker);f<<text;}
        r=direct::resolve_game_platform(root.string(),true,ini,table);
        assert(!r.inferred&&read()==text&&probes==before);
        assert(std::string(r.name)==(std::string(text)=="vita\r\n"?"VITA":"WINDOWS"));
    }
    fs::remove(marker);
    fs::create_directory(root/"platform.txt.auto.tmp");
    r=direct::resolve_game_platform(root.string(),true,ini,table);
    assert(std::string(r.name)=="VITA"&&r.inferred&&!r.saved&&r.error!=0&&!fs::exists(marker));
    fs::remove(root/"platform.txt.auto.tmp");
    r=direct::resolve_game_platform(root.string(),true,ini,table);
    assert(r.saved&&read()=="VITA\n");
    fs::remove(marker);fs::remove(root);
    std::cout<<"platform recovery, explicit settings, detection and failed-write retry passed\n";
}
