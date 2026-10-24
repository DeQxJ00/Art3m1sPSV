#pragma once
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>
namespace direct {
enum class HintButton { Circle, Square, Triangle, Cross, Direction, Pages, Start, Select, L, R };
struct ControlHint { HintButton button; std::string label; };
// Only application hints call this parser; game titles and script text never do.
inline std::vector<ControlHint> control_hints(const char* text){
    if(!text)return {};
    struct Token {const char* text;HintButton button;};
    static const Token tokens[]={{"↑↓",HintButton::Direction},{"←→",HintButton::Direction},{"○",HintButton::Circle},{"□",HintButton::Square},{"△",HintButton::Triangle},{"×",HintButton::Cross},{"L/R",HintButton::Pages},{"START",HintButton::Start},{"SELECT",HintButton::Select}};
    if(std::strcmp(text,"L  ‹")==0)return {{HintButton::L,""}};
    if(std::strcmp(text,"›  R")==0)return {{HintButton::R,""}};
    auto token_at=[&](size_t at)->const Token*{for(const auto& t:tokens){size_t n=std::strlen(t.text);if(std::strncmp(text+at,t.text,n)==0 && (text[at+n]==' '||text[at+n]=='\0'))return &t;}return nullptr;};
    if(!token_at(0))return {};
    std::vector<ControlHint> result;const size_t length=std::strlen(text);size_t at=0;
    while(at<length){
        const auto* token=token_at(at);if(!token)return {};
        size_t begin=at+std::strlen(token->text),end=begin;
        while(end<length && !(text[end-1]==' '&&token_at(end)))++end;
        std::string label(text+begin,end-begin);
        while(!label.empty()&&(label.front()==' '||label.front()=='/'))label.erase(label.begin());
        while(!label.empty()&&(label.back()==' '||label.back()=='/'))label.pop_back();
        // Leave prose (including arithmetic and explanatory button sentences)
        // on the normal translation path instead of guessing its layout.
        static const char* labels[]={"", "选择","确认","确认进入","调整","取消","返回","进入","勾选","切换开关","使用说明","翻页","设置","关于","退出","游戏设置","切换视图","启动","返回设置，或点击屏幕返回","选择 / Select","确认 / OK","退出 / Exit"};
        bool known=false;for(auto allowed:labels)if(label==allowed){known=true;break;}if(!known)return {};
        result.push_back({token->button,std::move(label)});at=end;
    }
    return result;
}
inline float hint_icon_width(HintButton button){return button==HintButton::Pages?52.f:button==HintButton::Start?46.f:button==HintButton::Select?56.f:24.f;}
inline float hint_label_size(float requested){return std::min(requested,20.f);}
}
