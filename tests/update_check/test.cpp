#include "update_check.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <fstream>
#include <thread>

using namespace direct;
static std::string release(const std::string& tag){return "{\"tag_name\":\""+tag+"\",\"draft\":false,\"prerelease\":false,\"assets\":[{\"name\":\"Art3m1sPSV.vpk\",\"state\":\"uploaded\"}]}";}
static std::atomic<int> calls{0};
static std::atomic<bool> gate{false},finished{false};
static UpdateResponse slow(const std::atomic<bool>& cancel){
    ++calls;
    while(!gate.load()&&!cancel.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
    finished=true;return {200,release("v1.4.0"),{}};
}
static UpdateResponse limited(const std::atomic<bool>&){return {403,release("v9.0.0"),{}};}
static UpdateResponse offline(const std::atomic<bool>&){return {0,{},"offline"};}
namespace direct {UpdateResponse fetch_github_release(const std::atomic<bool>&){return {};}}
static void complete(UpdateCheck& check){
    for(int i=0;i<1000&&!check.completed();++i){check.tick(false);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    assert(check.completed());
}
int main(int argc,char** argv){
    if(argc==2){std::ifstream f(argv[1]);std::string body((std::istreambuf_iterator<char>(f)),{});assert(!body.empty());assert(!newer_release(body,"v0.0.0").empty());std::cout<<"PASS live GitHub release payload: "<<newer_release(body,"v0.0.0")<<"\n";return 0;}
    assert(newer_release(release("v1.3.12"),"v1.3.11")=="v1.3.12");
    assert(newer_release(release("v1.3.12")+"\n", "v1.3.11")=="v1.3.12");
    assert(newer_release(release("v1.3.10"),"v1.3.9")=="v1.3.10");
    assert(newer_release(release("v2.0.0"),"v1.99.99")=="v2.0.0");
    assert(newer_release(release("v1.3.11"),"v1.3.11-dev.4+g123.dirty").empty());
    assert(newer_release(release("v1.3.10"),"v1.3.11").empty());
    for(auto tag:{"v1.4.0-beta.1","beta1.4.0","v01.4.0","v1.4","v1.4.0junk","v1000000.0.0","v1.4.0+build"})assert(newer_release(release(tag),"v1.3.11").empty());
    for(auto body:{"null","{}","[]","{","{}garbage","{\"tag_name\":null}"})assert(newer_release(body,"v1.3.11").empty());
    for(const auto* flag:{"draft","prerelease"}){
        auto s=release("v1.4.0");auto at=s.find(std::string("\"")+flag+"\":false");s.replace(at,std::string(flag).size()+8,std::string("\"")+flag+"\":true");
        assert(newer_release(s,"v1.3.11").empty());
    }
    auto s=release("v1.4.0");s.replace(s.find(".vpk"),4,".zip");assert(newer_release(s,"v1.3.11").empty());
    s=release("v1.4.0");s.replace(s.find("uploaded"),8,"starter");assert(newer_release(s,"v1.3.11").empty());
    assert(newer_release(std::string(updateMaxBody+1,' '),"v1.3.11").empty());
    s=release("v1.4.0")+std::string("\0junk",5);assert(newer_release(s,"v1.3.11").empty());
    assert(newer_release(release("v1.4.0"),"garbage").empty());
    UpdateCheck check("v1.3.11",slow);
    const auto start=std::chrono::steady_clock::now();check.start();check.start();
    for(int i=0;i<1000;++i){check.tick(true);assert(!check.visible(true));}
    assert(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(100));
    gate=true;complete(check);assert(calls==1);
    assert(!check.visible(false));check.tick(false);assert(!check.visible(true));
    check.tick(true);assert(check.visible(true));assert(!check.visible(false));
    check.tick(true);assert(check.visible(true));
    check.start();check.tick(false);check.tick(true);assert(check.visible(true));assert(calls==1);
    for(auto fetch:{limited,offline}){UpdateCheck bad("v1.3.11",fetch);bad.start();complete(bad);bad.tick(true);assert(!bad.visible(true));}
    gate=false;finished=false;
    {UpdateCheck cancel("v1.3.11",slow);cancel.start();}
    for(int i=0;i<1000&&!finished;++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(finished); // Destruction cancels without waiting or touching freed UI state.
    std::cout<<"PASS version/release validation, async start, offline/rate limit, deferred persistent notice, once per launch, cancellation\n";
}
