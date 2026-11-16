#pragma once
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <utility>
#include <vector>

namespace direct {
struct MenuGlyphBitmap {
    int16_t width=0,height=0,left=0,top=0,advance=0;
    std::vector<uint8_t> alpha;
};

class MenuGlyphCache {
public:
    const MenuGlyphBitmap* find(uint64_t key)const{
        const auto it=glyphs_.find(key);
        return it==glyphs_.end()?nullptr:&it->second;
    }
    bool insert(uint64_t key,MenuGlyphBitmap bitmap){
        if(!valid_key(key)||!valid_metrics(bitmap)||bitmap.alpha.size()!=pixels(bitmap)
            ||bytes_+recordBytes+bitmap.alpha.size()>limit||glyphs_.size()>=maxGlyphs)return false;
        const auto size=recordBytes+bitmap.alpha.size();
        if(!glyphs_.emplace(key,std::move(bitmap)).second)return false;
        bytes_+=size;
        return true;
    }
    static MenuGlyphCache load(const char* path,uint64_t stamp){
        FILE* file=std::fopen(path,"rb");
        if(!file)return {};
        MenuGlyphCache result;
        const bool ok=result.read(file,stamp);
        std::fclose(file);
        return ok?std::move(result):MenuGlyphCache{};
    }
    bool save(const char* path,const char* temporary,uint64_t stamp)const{
        FILE* file=std::fopen(temporary,"wb");
        if(!file)return false;
        const uint32_t count=glyphs_.size();
        bool ok=write_value(file,magic)&&write_value(file,stamp)&&write_value(file,count);
        for(const auto& [key,bitmap]:glyphs_){
            if(!ok)break;
            const int16_t metrics[]={bitmap.width,bitmap.height,bitmap.left,bitmap.top,bitmap.advance};
            ok=write_value(file,key)&&std::fwrite(metrics,sizeof(metrics),1,file)==1
                &&(bitmap.alpha.empty()||std::fwrite(bitmap.alpha.data(),1,bitmap.alpha.size(),file)==bitmap.alpha.size());
        }
        if(std::fclose(file)!=0)ok=false;
        if(ok&&std::rename(temporary,path)!=0){
            // Vita rename needs the destination to be absent.
            ok=std::remove(path)==0&&std::rename(temporary,path)==0;
        }
        std::remove(temporary);
        return ok;
    }

private:
    static constexpr uint64_t magic=0x3130434647334141ULL;
    static constexpr size_t recordBytes=sizeof(uint64_t)+5*sizeof(int16_t);
    static constexpr size_t limit=512*1024,maxGlyphs=2048;
    std::unordered_map<uint64_t,MenuGlyphBitmap> glyphs_;
    size_t bytes_=0;

    static bool valid_key(uint64_t key){
        const auto size=key>>32;
        return size>=8&&size<=64&&uint32_t(key)<=0x10ffff;
    }
    static bool valid_metrics(const MenuGlyphBitmap& b){
        return b.width>=0&&b.width<=128&&b.height>=0&&b.height<=128
            &&b.left>=-128&&b.left<=128&&b.top>=-128&&b.top<=128&&b.advance>=0&&b.advance<=128;
    }
    static size_t pixels(const MenuGlyphBitmap& b){return size_t(b.width)*b.height;}
    template<class T> static bool read_value(FILE* f,T& value){return std::fread(&value,sizeof(value),1,f)==1;}
    template<class T> static bool write_value(FILE* f,T value){return std::fwrite(&value,sizeof(value),1,f)==1;}
    bool read(FILE* file,uint64_t expectedStamp){
        uint64_t version=0,stamp=0;
        uint32_t count=0;
        if(!read_value(file,version)||version!=magic||!read_value(file,stamp)||stamp!=expectedStamp
            ||!read_value(file,count)||count>maxGlyphs)return false;
        for(uint32_t i=0;i<count;++i){
            uint64_t key=0;
            int16_t metrics[5]{};
            if(!read_value(file,key)||std::fread(metrics,sizeof(metrics),1,file)!=1)return false;
            MenuGlyphBitmap bitmap{metrics[0],metrics[1],metrics[2],metrics[3],metrics[4],{}};
            if(!valid_metrics(bitmap)||bytes_+recordBytes+pixels(bitmap)>limit)return false;
            bitmap.alpha.resize(pixels(bitmap));
            if(!bitmap.alpha.empty()&&std::fread(bitmap.alpha.data(),1,bitmap.alpha.size(),file)!=bitmap.alpha.size())
                return false;
            if(!insert(key,std::move(bitmap)))return false;
        }
        return std::fgetc(file)==EOF&&!std::ferror(file);
    }
};
}
