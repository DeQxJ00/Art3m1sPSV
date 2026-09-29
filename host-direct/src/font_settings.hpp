#pragma once
#include <cstdio>
#include <string>

namespace direct {
struct FontSettings {
    bool enabled=false;
    unsigned name=100,dialogue=100;
    bool positionEnabled=false,hideJapanese=false;
    int chineseX=0,chineseY=0,japaneseX=0,japaneseY=0;
};
inline bool parse_font_settings(const char* text,FontSettings& out) {
    unsigned version,on,name,dialogue,position=0,hide=0;int cx=0,cy=0,jx=0,jy=0;char trailing;
    if(std::sscanf(text,"%u",&version)!=1)return false;
    if(version==1){
        if(std::sscanf(text,"%u %u %u %u %c",&version,&on,&name,&dialogue,&trailing)!=4)return false;
    }else if(version==2){
        if(std::sscanf(text,"%u %u %u %u %u %u %d %d %d %d %c",&version,&on,&name,&dialogue,
            &position,&hide,&cx,&cy,&jx,&jy,&trailing)!=10)return false;
    }else return false;
    if(on>1||position>1||hide>1||name<75||name>150||dialogue<75||dialogue>150||
        cx < -500||cx>500||cy < -500||cy>500||jx < -500||jx>500||jy < -500||jy>500)return false;
    out={on!=0,name,dialogue,position!=0,hide!=0,cx,cy,jx,jy};return true;
}
inline std::string font_settings_path(const std::string& directory,const std::string& id) {
    // Reversible encoding prevents traversal and collisions between game IDs.
    if(id.empty()||id.size()>100)return {};
    const char* hex="0123456789abcdef";std::string path=directory+"/";
    for(unsigned char c:id){path+=hex[c>>4];path+=hex[c&15];}
    return path+".font";
}
inline FontSettings load_font_settings(const std::string& path) {
    FontSettings value;if(path.empty())return value;
    FILE* f=std::fopen(path.c_str(),"rb");
    if(!f)f=std::fopen((path+".bak").c_str(),"rb");
    if(!f)return value;
    char text[96]{};size_t n=std::fread(text,1,sizeof(text)-1,f);int extra=std::fgetc(f);std::fclose(f);
    if(extra==EOF&&n)parse_font_settings(text,value);return value;
}
inline bool save_font_settings(const std::string& path,const FontSettings& value) {
    if(path.empty()||value.name<75||value.name>150||value.dialogue<75||value.dialogue>150||
        value.chineseX < -500||value.chineseX>500||value.chineseY < -500||value.chineseY>500||
        value.japaneseX < -500||value.japaneseX>500||value.japaneseY < -500||value.japaneseY>500)return false;
    const auto tmp=path+".tmp",bak=path+".bak";
    FILE* f=std::fopen(tmp.c_str(),"wb");if(!f)return false;
    bool ok=std::fprintf(f,"2 %u %u %u %u %u %d %d %d %d\n",unsigned(value.enabled),value.name,value.dialogue,
        unsigned(value.positionEnabled),unsigned(value.hideJapanese),value.chineseX,value.chineseY,value.japaneseX,value.japaneseY)>0;
    if(std::fclose(f)!=0)ok=false;
    if(!ok){std::remove(tmp.c_str());return false;}
    bool existed=false;if(FILE* old=std::fopen(path.c_str(),"rb")){existed=true;std::fclose(old);}
    if(existed){std::remove(bak.c_str());if(std::rename(path.c_str(),bak.c_str())!=0){std::remove(tmp.c_str());return false;}}
    if(std::rename(tmp.c_str(),path.c_str())!=0){if(existed)std::rename(bak.c_str(),path.c_str());std::remove(tmp.c_str());return false;}
    std::remove(bak.c_str());return true;
}
}
