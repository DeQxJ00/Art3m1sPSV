#pragma once
#include <cstdint>
#include <string>
#include <algorithm>

namespace direct {
// SDK limits count UTF-16 code units; script limits count Unicode scalars.
inline std::u16string dialog_utf16(const std::string& input, size_t units, size_t scalars) {
    std::u16string out;
    for(size_t i=0, count=0;i<input.size()&&count<scalars;++count){
        uint32_t cp=static_cast<unsigned char>(input[i++]);
        unsigned extra=0;uint32_t minimum=0;
        if(cp>=0xc2&&cp<=0xdf){extra=1;cp&=31;minimum=0x80;}
        else if(cp>=0xe0&&cp<=0xef){extra=2;cp&=15;minimum=0x800;}
        else if(cp>=0xf0&&cp<=0xf4){extra=3;cp&=7;minimum=0x10000;}
        else if(cp>=0x80)cp=0xfffd;
        bool valid=i+extra<=input.size();
        for(unsigned j=0;valid&&j<extra;++j){unsigned c=static_cast<unsigned char>(input[i+j]);if((c&0xc0)!=0x80)valid=false;else cp=(cp<<6)|(c&63);}
        if(extra){if(valid)i+=extra;if(!valid||cp<minimum||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))cp=0xfffd;}
        if(!cp)break;
        if(out.size()+(cp>=0x10000?2:1)>units)break;
        if(cp>=0x10000){cp-=0x10000;out.push_back(char16_t(0xd800+(cp>>10)));out.push_back(char16_t(0xdc00+(cp&1023)));}
        else out.push_back(char16_t(cp));
    }
    return out;
}
inline std::string dialog_utf8(const uint16_t* input,size_t units,size_t scalars){
    std::string out;
    for(size_t i=0,count=0;i<units&&input[i]&&count<scalars;++count){
        uint32_t cp=input[i++];
        if(cp>=0xd800&&cp<=0xdbff){if(i<units&&input[i]>=0xdc00&&input[i]<=0xdfff)cp=0x10000+((cp-0xd800)<<10)+(input[i++]-0xdc00);else cp=0xfffd;}
        else if(cp>=0xdc00&&cp<=0xdfff)cp=0xfffd;
        if(cp<0x80)out+=char(cp);
        else if(cp<0x800){out+=char(0xc0|(cp>>6));out+=char(0x80|(cp&63));}
        else if(cp<0x10000){out+=char(0xe0|(cp>>12));out+=char(0x80|((cp>>6)&63));out+=char(0x80|(cp&63));}
        else{out+=char(0xf0|(cp>>18));out+=char(0x80|((cp>>12)&63));out+=char(0x80|((cp>>6)&63));out+=char(0x80|(cp&63));}
    }
    return out;
}
}
