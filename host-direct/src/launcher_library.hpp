#pragma once
#include "launcher_icons.hpp"
#include "title_initial.hpp"
#include "library_sort.hpp"
#include "launcher_buttons.hpp"
#include "launcher_catalog.hpp"
#include "launcher_page_repeat.hpp"
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
    bool covers=false;bool descending=false;size_t first=size_t(-1);
    const char* sort_label()const{return descending?"排序 Z-A":"排序 A-Z";}
    void toggle_sort(std::vector<art3m1s::GameEntry>& games,size_t& selected){clear_page();descending=!descending;sort_library(games,selected,descending);}
    std::array<bool,6> coverPending{},coverUploaded{},iconUploaded{};
    LauncherPageRepeat pageRepeat,directionRepeat;
    LauncherIcons icons;std::array<Texture*,6> images{};
    Texture* buttons=nullptr;Texture* keycaps=nullptr;
    std::array<std::string,6> labels{};std::string heading,page;
    unsigned labelFontRevision=0;
    const std::string preference=std::string(art3m1s::kDataRoot)+"/library-view.txt";
    LauncherLibrary(){FILE* f=std::fopen(preference.c_str(),"rb");if(f){covers=std::fgetc(f)=='1';std::fclose(f);}}
    size_t count()const{return covers?6:5;}
    void clear_page(){coverPending.fill(false);coverUploaded.fill(false);iconUploaded.fill(false);labels.fill({});for(auto*& t:images){if(t)destroy(t);t=nullptr;}icons.clear();first=size_t(-1);}
    void reset_navigation(){pageRepeat={};directionRepeat={};}
    void clear(){clear_page();reset_navigation();if(buttons)destroy(buttons);buttons=nullptr;if(keycaps)destroy(keycaps);keycaps=nullptr;for(auto& label:labels)std::string().swap(label);std::string().swap(heading);std::string().swap(page);}
    void toggle(){clear_page();covers=!covers;FILE* f=std::fopen(preference.c_str(),"wb");if(f){std::fputs(covers?"1\n":"0\n",f);std::fclose(f);}}
    std::array<float,8> footerX{},footerWidth{};
    std::array<float,9> footerEdge{};
    bool footer(int x,int y,int action)const{return action>=0&&action<8&&y>=492&&y<544&&x>=footerEdge[action]&&x<footerEdge[action+1];}
    float page_midpoint()const{return footerX[4]+26;}
    void prepare_footer(){
        const char* actions[]={"选择","启动","游戏设置","切换视图","翻页","设置","关于",sort_label()};
        const float offsets[]={30,30,30,30,58,52,62,30};
        float total=0;
        for(int i=0;i<8;++i){ui_prepare(actions[i],16);footerWidth[i]=offsets[i]+menu_text_width(ui_translate(actions[i]),16);total+=footerWidth[i];}
        const float gap=std::max(0.f,(896-total)/7);
        float x=32;footerEdge[0]=32;
        for(int i=0;i<8;++i){footerX[i]=x;x+=footerWidth[i]+gap;if(i<7)footerEdge[i+1]=x-gap/2;}
        footerEdge[8]=928;
    }
    uint32_t repeat_buttons(uint32_t held,uint32_t pressed,uint64_t now){
        const uint32_t shoulders=SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER;
        uint32_t directions=SCE_CTRL_UP|SCE_CTRL_DOWN;
        if(covers)directions|=SCE_CTRL_LEFT|SCE_CTRL_RIGHT;
        const uint32_t edges=pressed&shoulders;
        const uint32_t buttons=edges?edges:held&shoulders;
        const int direction=buttons==shoulders&&pageRepeat.direction()?pageRepeat.direction():
            (buttons&SCE_CTRL_LTRIGGER)?-1:(buttons&SCE_CTRL_RTRIGGER)?1:0;
        const int pageTurn=pageRepeat.update(direction,edges!=0,now);

        const uint32_t directionEdges=pressed&directions;
        const uint32_t activeDirection=uint32_t(directionRepeat.direction());
        const uint32_t candidates=directionEdges?directionEdges:held&directions;
        uint32_t move=!directionEdges?(candidates&activeDirection):0;
        if(!move){
            for(uint32_t button:{SCE_CTRL_LEFT,SCE_CTRL_RIGHT,SCE_CTRL_UP,SCE_CTRL_DOWN}){
                if(candidates&button){move=button;break;}
            }
        }
        const auto repeatedMove=uint32_t(directionRepeat.update(int(move),directionEdges!=0,now));

        // A fresh press takes precedence over a repeat due on the same frame.
        if(pressed&(shoulders|directions))return pressed;
        const uint32_t repeatedPage=pageTurn<0?SCE_CTRL_LTRIGGER:pageTurn>0?SCE_CTRL_RTRIGGER:0;
        return pressed|repeatedPage|repeatedMove;
    }
    void navigate(uint32_t pressed,size_t& selected,size_t size)const{
        if(!size)return;int delta=0;
        if(pressed&SCE_CTRL_DOWN)delta=covers?3:1;
        if(pressed&SCE_CTRL_UP)delta=covers?-3:-1;
        if(covers&&(pressed&SCE_CTRL_RIGHT))delta=1;
        if(covers&&(pressed&SCE_CTRL_LEFT))delta=-1;
        if(pressed&SCE_CTRL_LTRIGGER){
            if(selected/count()==0)return;
            delta=-int(count());
        }else if(pressed&SCE_CTRL_RTRIGGER){
            if(selected/count()==(size-1)/count())return;
            delta=int(count());
        }
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
    bool prepare(const std::vector<art3m1s::GameEntry>& games,size_t selected,LauncherCatalog& catalog){
        const size_t base=selected/count()*count();
        const bool pageChanged=first!=base;
        if(pageChanged){
            clear_page();first=base;
            icons.prepare(games,selected,count(),{});
        }
        const auto fontRevision=menu_font_revision();
        if(pageChanged||fontRevision!=labelFontRevision){
            for(const auto& initial:icons.initials)menu_prepare(initial.c_str(),25);
            for(size_t i=0;i<count()&&base+i<games.size();++i){
                const auto& game=games[base+i];
                labels[i]=library_label(title_with_initial(game.title),covers?20:26,covers?280:800);
            }
            labelFontRevision=fontRevision;
        }
        catalog.collect_artwork();
        for(size_t i=0;i<count()&&base+i<games.size();++i){
            const auto* art=catalog.artwork(games[base+i].id);
            coverPending[i]=covers&&(!art||!art->coverLoaded);
            if(!art)continue;
            if(covers&&art->coverLoaded&&!coverUploaded[i]){
                coverUploaded[i]=true;
                if(!art->cover.empty()){
                    images[i]=texture_compressed(288,162,1,true,art->cover.data(),art->cover.size(),true);
                    if(images[i]&&icons.images[i]){destroy(icons.images[i]);icons.images[i]=nullptr;}
                }
            }
            if(!images[i]&&art->iconLoaded&&!iconUploaded[i]){
                iconUploaded[i]=true;
                if(!art->icon.empty())icons.images[i]=texture(LauncherIcons::side,LauncherIcons::side,art->icon.data());
            }
        }
        heading=games.empty()?ui_translate("选择游戏"):library_label(title_with_initial(games[selected].title),24,690);
        if(!buttons)buttons=make_launcher_buttons();
        if(!keycaps)keycaps=make_launcher_keycaps();
        char number[48];std::snprintf(number,sizeof(number),"%u / %u",games.empty()?0:unsigned(base/count()+1),unsigned((games.size()+count()-1)/count()));
        page=std::string(ui_translate(covers?"封面":"列表"))+"  "+number;
        menu_prepare(heading.c_str(),24);menu_prepare(page.c_str(),18);
        for(const auto& label:labels)menu_prepare(label.c_str(),covers?20:26);
        prepare_footer();
        for(const char* text:{"封面载入中","未设置封面","资源不完整","未找到游戏，请复制到 games 目录。"})ui_prepare(text,18);
        for(const char* text:{"L","R","START","SELECT"})menu_prepare(text,13);
        return pageChanged;
    }
    static void prepare_loading(){
        ui_prepare("正在加载游戏列表",24);
    }
    static void draw_loading(LauncherCatalog::Progress progress,uint64_t now){
        rect(244,211,472,120,0x101b2bff);
        ui_text(268,253,24,"正在加载游戏列表");
        rect(268,278,424,12,0x354256ff);
        if(progress.total)rect(268,278,424.f*std::min(progress.done,progress.total)/progress.total,12,0x6acbe9ff);
        else rect(268+float((now/3000)%337),278,88,12,0x6acbe9ff);
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
        const char* actions[]={"选择","启动","游戏设置","切换视图","翻页","设置","关于",sort_label()};
        for(int i=0;i<8;++i){const float x=footerX[i];float label=x+30;
            if(i==0||i<4||i==7)launcher_face_button(buttons,i==0?4:i==7?3:i-1,x,500);
            else if(i==4){launcher_key_button(keycaps,0,x,498,"L");launcher_key_button(keycaps,1,x+28,498,"R");label=x+58;}
            else{launcher_key_button(keycaps,i==5?2:3,x,498,i==5?"START":"SELECT");label=x+(i==5?52:62);}
            ui_text(label,519,16,actions[i]);}
    }
};
}
