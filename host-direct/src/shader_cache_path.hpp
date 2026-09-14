#pragma once
#include <string>
namespace direct {
// Preserve the virtual archive directory, never the host's extraction prefix.
inline std::string shader_cache_relative(const std::string& source){
    if(source.empty()||source.size()>480)return {};
    std::string normalized=source;for(char& c:normalized)if(c=='\\')c='/';
    if(normalized.front()=='/'||normalized.find(':')!=std::string::npos)return {};
    std::string result;size_t at=0;
    while(at<normalized.size()){
        auto end=normalized.find('/',at);if(end==std::string::npos)end=normalized.size();
        auto part=normalized.substr(at,end-at);at=end+1;
        if(part.empty()||part==".")continue;
        if(part==".."||part.back()=='.'||part.back()==' ')return {};
        for(unsigned char c:part)if(c<32||c=='?'||c=='*'||c=='|'||c=='"'||c=='<'||c=='>')return {};
        if(!result.empty())result+='/';result+=part;
    }
    if(result.empty())return {};
    // Include the source extension to distinguish e.g. name.hlsl/name.cg.
    return result;
}
}
