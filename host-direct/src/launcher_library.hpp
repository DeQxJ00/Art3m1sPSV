#pragma once
#include "launcher_icons.hpp"
#include "launcher_buttons.hpp"
#include "launcher_cover_loader.hpp"
#include <psp2/ctrl.h>

namespace direct {
// Measure the prepared font and remove whole UTF-8 codepoints only.
inline std::string library_label(const std::string& text,float size,float width){
    menu_prepare(text.c_str(),size);menu_prepare("…",size);
    if(menu_text_width(text.c_str(),size)<=width)return text;
    std::string result=text;
    while(!result.empty()){
        size_t end=result.size()-1;
        while(end&&(static_cast<unsigned char>(result[end])&0xc0)==0x80)--end;
        result.resize(end);
        if(menu_text_width((result+"…").c_str(),size)<=width)break;
    }
    return result+"…";
}
struct LauncherLibrary {
    bool covers=false;size_t first=size_t(-1);
    LauncherCoverLoader coverLoader;std::array<bool,6> coverPending{};
    LauncherIcons icons;std::array<Texture*,6> images{};
    Texture* buttons=nullptr;Texture* keycaps=nullptr;
    std::array<std::string,6> labels{};std::string heading,page;
    const std::string preference=std::string(art3m1s::kDataRoot)+"/library-view.txt";
    LauncherLibrary(){FILE* f=std::fopen(preference.c_str(),"rb");if(f){covers=std::fgetc(f)=='1';std::fclose(f);}}
    size_t count()const{return covers?6:5;}
    void clear_page(){coverLoader.cancel();coverPending.fill(false);for(auto*& t:images){if(t)destroy(t);t=nullptr;}icons.clear();first=size_t(-1);}
    void clear(){coverLoader.stop();clear_page();if(buttons)destroy(buttons);buttons=nullptr;if(keycaps)destroy(keycaps);keycaps=nullptr;for(auto& label:labels)std::string().swap(label);std::string().swap(heading);std::string().swap(page);}
    void toggle(){clear();covers=!covers;FILE* f=std::fopen(preference.c_str(),"wb");if(f){std::fputs(covers?"1\n":"0\n",f);std::fclose(f);}}
    std::array<float,8> footerX{},footerWidth{};
    std::array<float,9> footerEdge{};
    bool footer(int x,int y,int action)const{return action>=0&&action<8&&y>=492&&y<544&&x>=footerEdge[action]&&x<footerEdge[action+1];}
    float page_midpoint()const{return footerX[4]+26;}
    void prepare_footer(){
        const char* actions[]={"选择","启动","游戏设置","切换视图","翻页","设置","关于","退出"};
        const float offsets[]={30,30,30,30,58,52,62,30};
        float total=0;
        for(int i=0;i<8;++i){ui_prepare(actions[i],16);footerWidth[i]=offsets[i]+menu_text_width(ui_translate(actions[i]),16);total+=footerWidth[i];}
        const float gap=std::max(0.f,(896-total)/7);
        float x=32;footerEdge[0]=32;
        for(int i=0;i<8;++i){footerX[i]=x;x+=footerWidth[i]+gap;if(i<7)footerEdge[i+1]=x-gap/2;}
        footerEdge[8]=928;
    }
    void navigate(uint32_t pressed,size_t& selected,size_t size)const{
        if(!size)return;int delta=0;
        if(pressed&SCE_CTRL_DOWN)delta=covers?3:1;
        if(pressed&SCE_CTRL_UP)delta=covers?-3:-1;
        if(covers&&(pressed&SCE_CTRL_RIGHT))delta=1;
        if(covers&&(pressed&SCE_CTRL_LEFT))delta=-1;
        if(pressed&SCE_CTRL_RTRIGGER)delta=int(count());
        if(pressed&SCE_CTRL_LTRIGGER)delta=-int(count());
        selected=size_t(std::clamp<int>(int(selected)+delta,0,int(size)-1));
    }
    size_t hit(int x,int y,size_t selected,size_t size)const{
        const size_t base=selected/count()*count();
        if(covers){
            if(x<32||x>=928||y<64||y>=464)return size;
            int col=(x-32)/304,row=(y-64)/202;
            if(col>=3||row>=2||(x-32)%304>=288||(y-64)%202>=194)return size;
            return std::min(size,base+row*3+col);
        }
        if(x<32||x>=928||y<64||y>=464||(y-64)%80>=72)return size;
        return std::min(size,base+(y-64)/80);
    }
    void prepare(const std::vector<art3m1s::GameEntry>& games,size_t selected){
        const size_t base=selected/count()*count();
        if(first!=base){clear_page();first=base;icons.prepare(games,selected,count());
            for(size_t i=0;i<count()&&base+i<games.size();++i){
                if(covers)coverPending[i]=coverLoader.request(games[base+i],i);
                labels[i]=library_label(games[base+i].title,covers?20:26,covers?280:800);
            }
        }
        // Bound GPU upload work to one thumbnail per frame.
        LauncherCoverLoader::Result ready;
        if(coverLoader.take(ready)&&ready.slot<images.size()){
            coverPending[ready.slot]=false;
            if(!ready.pixels.empty())images[ready.slot]=texture_compressed(288,162,1,true,ready.pixels.data(),ready.pixels.size(),true);
        }
        heading=games.empty()?ui_translate("选择游戏"):library_label(games[selected].title,24,690);
        if(!buttons)buttons=make_launcher_buttons();
        if(!keycaps)keycaps=make_launcher_keycaps();
        char number[48];std::snprintf(number,sizeof(number),"%u / %u",games.empty()?0:unsigned(base/count()+1),unsigned((games.size()+count()-1)/count()));
        page=std::string(ui_translate(covers?"封面":"列表"))+"  "+number;
        menu_prepare(heading.c_str(),24);menu_prepare(page.c_str(),18);
        for(const auto& label:labels)menu_prepare(label.c_str(),covers?20:26);
        prepare_footer();
        for(const char* text:{"封面载入中","未设置封面","资源不完整","未找到游戏，请复制到 games 目录。"})ui_prepare(text,18);
        for(const char* text:{"L","R","START","SELECT"})menu_prepare(text,13);
    }
    void draw(const std::vector<art3m1s::GameEntry>& games,size_t selected)const{
        menu_text(32,39,24,heading.c_str());menu_text(754,39,18,page.c_str(),0xb5c4d4ff);
        const size_t base=selected/count()*count();
        for(size_t j=0;j<count()&&base+j<games.size();++j){
            const auto& game=games[base+j];const bool active=base+j==selected;
            const float x=covers?32+(j%3)*304:32,y=covers?64+(j/3)*202:64+j*80;
            if(covers){
                rect(x-3,y-3,294,168,active?0x6acbe9ff:0x1c2838ff);rect(x,y,288,162,0x1c2838ff);
                if(images[j]){Vertex v[]={{x,y,0,0,1,1,1,1},{x+288,y,1,0,1,1,1,1},{x,y+162,0,1,1,1,1,1},{x+288,y+162,1,1,1,1,1,1}};draw_quad(images[j],v);}
                else{icons.draw(game,j,x+120,y+40);ui_text(x+90,y+119,18,coverPending[j]?"封面载入中":"未设置封面",0xb5c4d4ff);}
                menu_text(x+4,y+188,20,labels[j].c_str(),game.ready()?0xffffffff:0x8895a5ff);
            }else{
                rect(x,y,896,72,active?0x286482ff:0x1c2838ff);icons.draw(game,j,x+12,y+12);
                menu_text(x+78,y+46,26,labels[j].c_str(),game.ready()?0xffffffff:0x8895a5ff);
            }
        }
        if(games.empty())ui_text(32,180,18,"未找到游戏，请复制到 games 目录。");
        rect(32,488,896,1,0x354256ff);
        const char* actions[]={"选择","启动","游戏设置","切换视图","翻页","设置","关于","退出"};
        for(int i=0;i<8;++i){const float x=footerX[i];float label=x+30;
            if(i==0||i<4||i==7)launcher_face_button(buttons,i==0?4:i==7?3:i-1,x,500);
            else if(i==4){launcher_key_button(keycaps,0,x,498,"L");launcher_key_button(keycaps,1,x+28,498,"R");label=x+58;}
            else{launcher_key_button(keycaps,i==5?2:3,x,498,i==5?"START":"SELECT");label=x+(i==5?52:62);}
            ui_text(label,519,16,actions[i]);}
    }
};
}
