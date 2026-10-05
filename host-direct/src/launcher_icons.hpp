#pragma once
#include "game_library.hpp"
#include "gpu.hpp"
#include "launcher_icon_cache.hpp"
#include <array>
#include <vector>

namespace direct {
struct LauncherIcons {
    static constexpr unsigned side=48;
    std::array<Texture*,5> images{};
    std::array<std::string,5> initials{};
    size_t first=size_t(-1);

    void clear(){
        for(auto*& image:images){if(image){destroy(image);image=nullptr;}}
        for(auto& initial:initials)initial.clear();
        first=size_t(-1);
    }
    void prepare(const std::vector<art3m1s::GameEntry>& games,size_t selected){
        const size_t page=selected/5*5;
        if(page==first)return;
        clear();first=page;
        for(size_t i=0;i<5&&page+i<games.size();++i){
            const auto& game=games[page+i];
            const std::string& label=game.title.empty()?game.id:game.title;
            if(!label.empty()){
                const unsigned char lead=static_cast<unsigned char>(label[0]);
                const size_t length=lead<0x80?1:(lead&0xe0)==0xc0?2:(lead&0xf0)==0xe0?3:4;
                initials[i]=label.substr(0,length);
                menu_prepare(initials[i].c_str(),25);
            }
            // Only five visible icons are decoded and uploaded.
            std::array<uint8_t,launcherIconBytes> rgba{};
            if(launcher_icon_read_png(game,rgba))images[i]=texture(side,side,rgba.data());
            if(!images[i]&&(!game.matching_exe.empty()||!game.exe_candidates.empty())){
                if(launcher_icon_read_cache(game,rgba))images[i]=texture(side,side,rgba.data());
            }
        }
    }
    void draw(const art3m1s::GameEntry& game,size_t row,float x,float y)const{
        if(row<images.size()&&images[row]){
            Vertex vertices[]={{x,y,0,0,1,1,1,1},{x+side,y,1,0,1,1,1,1},
                {x,y+side,0,1,1,1,1,1},{x+side,y+side,1,1,1,1,1,1}};
            draw_quad(images[row],vertices);
        }else{
            unsigned hash=2166136261u;
            for(unsigned char c:game.id)hash=(hash^c)*16777619u;
            const uint32_t color=(uint32_t(55+(hash&31))<<24)
                |(uint32_t(95+((hash>>8)&63))<<16)
                |(uint32_t(135+((hash>>16)&63))<<8)|255u;
            rect(x,y,side,side,color);
            menu_text(x+12,y+34,25,initials[row].empty()?"?":initials[row].c_str());
        }
    }
};
}
