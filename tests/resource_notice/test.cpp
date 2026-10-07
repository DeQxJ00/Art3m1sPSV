#include "resource_notice.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

int main(){
    using direct::ResourceNotice;
    std::vector<art3m1s::GameEntry> games;
    assert(ResourceNotice::needed(games));
    art3m1s::GameEntry demo;demo.bundled=true;demo.has_pfs=true;
    games.push_back(demo);
    assert(ResourceNotice::needed(games)); // Shipped demo must not hide setup help.
    art3m1s::GameEntry external;external.has_system_ini=true;
    games.push_back(external);assert(!ResourceNotice::needed(games));
    games.back().has_system_ini=false;games.back().has_pfs=true;
    assert(!ResourceNotice::needed(games));
    games.back().has_pfs=false;assert(ResourceNotice::needed(games));
    ResourceNotice notice;
    assert(!notice.acknowledge()&&notice.failed); // Missing parent: allow retry.
    std::filesystem::create_directories("ux0:data/art3m1s-gxm");
    std::ofstream(ResourceNotice::path)<<"";
    assert(ResourceNotice::needed(games)); // An empty/failed write is not an acknowledgement.
    assert(notice.acknowledge()&&!notice.failed);
    assert(!ResourceNotice::needed(games));
    games.clear();assert(!ResourceNotice::needed(games)); // Persists even across a fresh scan.
    direct::uiLanguage=direct::UiLanguage::English;
    assert(std::string(direct::ui_translate(ResourceNotice::title))=="Prepare game resources");
    std::cout<<"PASS external resources, bundled demo, persistence, save retry and English notice\n";
}
