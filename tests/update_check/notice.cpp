#include "update_notice.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

static std::string payload,prepared,drawn;
static unsigned requests=0,rectangles=0;
namespace direct {
UpdateResponse fetch_github_release(const std::atomic<bool>&){++requests;return {200,payload,{}};}
void menu_prepare(const char* text,float size){assert(size==kMenuNoteSize);prepared=text;}
void menu_text(float x,float y,float size,const char* text,uint32_t color){
    assert(x==614&&y==36&&size==kMenuNoteSize&&color==0xffffffff);drawn=text;
}
void rect(float x,float y,float w,float h,uint32_t color){
    assert(x==600&&y==10&&w==348&&h==38&&color==0x142638ee);++rectangles;
}
}
int main(int argc,char** argv){
    assert(argc==2);
    std::ifstream file(argv[1]);payload.assign(std::istreambuf_iterator<char>(file),{});
    assert(!payload.empty());
    const auto tag=direct::newer_release(payload,"v0.0.0");assert(!tag.empty());
    for(const auto& installed:{std::string("v0.0.0"),tag,tag+"-dev.1"}){
        direct::UpdateCheck check(installed);check.start();check.start();
        for(unsigned i=0;i<1000&&!check.completed();++i){check.tick(false);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        assert(check.completed()&&!check.visible(false)&&!check.visible(true));
        check.tick(true);
        if(installed!="v0.0.0"){assert(!check.visible(true));continue;}
        assert(check.visible(true));
        for(auto language:{direct::UiLanguage::Chinese,direct::UiLanguage::English}){
            direct::uiLanguage=language;
            direct::prepare_update_notice(check);direct::draw_update_notice(check);
            const auto expected=std::string(language==direct::UiLanguage::Chinese?"有更新：":"Update: ")+tag;
            assert(prepared==expected&&drawn==expected);
            std::cout<<"PASS rendered text: "<<drawn<<"\n";
        }
        for(unsigned i=0;i<60*60*60;++i){check.tick(true);assert(check.visible(true));}
        // Enter a game/settings: hide immediately; return to the list: restore,
        // without a second HTTP request.
        check.tick(false);assert(!check.visible(false));check.tick(true);assert(check.visible(true));
    }
    assert(requests==3&&rectangles==2);
    std::cout<<"PASS live release payload, old/equal/dev versions, launcher visibility, bilingual draw commands and persistent notice\n";
}
