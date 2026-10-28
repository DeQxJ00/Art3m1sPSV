#pragma once
#include "psb_cache_settings.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>
namespace direct {
struct PsbCacheInventory {
    std::map<std::string,uint64_t> files;
    static void collect(const char* path,uint64_t bytes,void* context){
        auto& self=*static_cast<PsbCacheInventory*>(context);
        std::string key(path);std::replace(key.begin(),key.end(),'\\','/');
        self.files[key]=bytes;
    }
};
struct PsbCacheEstimate {unsigned mib=32;long double trimmed=0,median=0;};
inline PsbCacheEstimate estimate_psb_cache(std::vector<uint64_t> sizes){
    PsbCacheEstimate result;if(sizes.empty())return result;
    std::sort(sizes.begin(),sizes.end());const size_t n=sizes.size();
    const size_t first=n>=3?1:0,last=n>=3?n-1:n;
    long double sum=0;for(size_t i=first;i<last;++i)sum+=sizes[i];
    result.trimmed=sum/(last-first);
    result.median=n%2?sizes[n/2]:(static_cast<long double>(sizes[n/2-1])+sizes[n/2])/2;
    const long double mib=std::ceil((std::max(result.trimmed,result.median)*4)/1048576+2);
    result.mib=static_cast<unsigned>(std::clamp(mib,32.0L,64.0L));return result;
}
}
