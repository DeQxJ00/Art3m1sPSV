#pragma once
#include "large_image_scope.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace direct {
using LargeImageProgress=void(*)(void*,const char*,unsigned,unsigned,int);
inline LargeImageProgress largeImageProgress=nullptr;
inline void* largeImageProgressContext=nullptr;
void large_image_configure(bool enabled,const std::string& root,unsigned thresholdMiB=30,const LargeImageScope& scope=LargeImageScope{});
std::vector<std::string> large_image_scan_files(const std::string& root,bool& failed);
std::vector<std::string> large_image_scan_folders(const std::string& root,bool& failed);
using LargeImageRead=int(*)(const char*,uint8_t*,int,int64_t);
void large_image_pregenerate(const std::string& root,LargeImageRead read);
bool large_image_allowed(unsigned w,unsigned h);
// -1 means not applicable; zero means allocation failure (retry compressed).
int large_image_upload(uint64_t id,const char* name,unsigned w,unsigned h,const uint8_t* rgba,size_t length,const uint8_t* proof=nullptr,size_t proofCount=0);
inline uint64_t large_image_hash(const uint8_t* data,size_t n){
    uint64_t h=0xcbf29ce484222325ULL;for(size_t i=0;i<n;++i)h=(h^data[i])*0x100000001b3ULL;return h;
}
inline size_t large_image_blocks(unsigned w,unsigned h,bool bc1=false){return size_t((w+3)/4)*((h+3)/4)*(bc1?8:16);}
// Explicit little-endian disk format, independent of struct packing.
inline void large_image_put(std::vector<uint8_t>& out,uint64_t value,unsigned n){for(unsigned i=0;i<n;++i)out.push_back(uint8_t(value>>(i*8)));}
inline uint64_t large_image_get(const uint8_t* p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;++i)v|=uint64_t(p[i])<<(i*8);return v;}
inline std::vector<uint8_t> large_image_header(unsigned w,unsigned h,uint64_t source,const std::vector<uint8_t>& blocks,bool bc1=false){
    std::vector<uint8_t> out;out.reserve(40);out.insert(out.end(),{'B','C','I','1'});
    large_image_put(out,bc1?2:1,4);large_image_put(out,w,4);large_image_put(out,h,4);large_image_put(out,source,8);
    large_image_put(out,blocks.size(),8);large_image_put(out,large_image_hash(blocks.data(),blocks.size()),8);
    return out;
}
inline std::vector<uint8_t> large_image_package(unsigned w,unsigned h,uint64_t source,const std::vector<uint8_t>& blocks,bool bc1=false){
    auto out=large_image_header(w,h,source,blocks,bc1);out.insert(out.end(),blocks.begin(),blocks.end());return out;
}
inline bool large_image_valid(const std::vector<uint8_t>& data,unsigned w,unsigned h,uint64_t source,bool bc1=false){
    return data.size()>=40&&!std::memcmp(data.data(),"BCI1",4)&&large_image_get(data.data()+4,4)==(bc1?2:1)&&
        large_image_get(data.data()+8,4)==w&&large_image_get(data.data()+12,4)==h&&large_image_get(data.data()+16,8)==source&&
        large_image_get(data.data()+24,8)==large_image_blocks(w,h,bc1)&&data.size()==40+large_image_blocks(w,h,bc1)&&
        large_image_get(data.data()+32,8)==large_image_hash(data.data()+40,data.size()-40);
}
}
