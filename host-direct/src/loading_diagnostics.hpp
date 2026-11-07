#pragma once
#include <array>
#include <mutex>
#include <string>
#include <cstring>
namespace direct {
struct LoadingIssue {
    bool fatal=false;
    unsigned count=0;
    std::array<std::string,4> details;
    explicit operator bool()const{return count!=0;}
};
class LoadingDiagnostics {
    std::mutex mutex;
    bool active=false,ignoreResources=false;
    LoadingIssue issue;
public:
    void begin(){std::lock_guard<std::mutex> lock(mutex);active=true;ignoreResources=false;issue={};}
    void end(){std::lock_guard<std::mutex> lock(mutex);active=false;issue={};}
    void ignore_resources(){std::lock_guard<std::mutex> lock(mutex);ignoreResources=true;if(!issue.fatal)issue={};}
    void record(const char* level,const char* text){
        if(!level||!text)return;
        const bool fatal=!std::strcmp(level,"E")&&(
            !std::strncmp(text,"解释器错误:",std::strlen("解释器错误:"))||
            !std::strncmp(text,"art3m1s_runtime_load_project_bytes",std::strlen("art3m1s_runtime_load_project_bytes")));
        const char* detail=nullptr;
        // Consume final provider failures only, never host_read probes or
        // optional include/cache misses. Callback threads only copy bounded data.
        if(!std::strcmp(level,"W"))for(const char* prefix:{"GXM texture source missing: ","GXM texture format failure: ","GXM texture decoder failure: "}){
            const size_t n=std::strlen(prefix);if(!std::strncmp(text,prefix,n)){detail=text+n;break;}
        }
        if(!fatal&&!detail)return;
        std::lock_guard<std::mutex> lock(mutex);
        if(!active||issue.fatal||(!fatal&&ignoreResources))return;
        if(fatal){issue={};issue.fatal=true;detail=text;}
        std::string value=detail;
        if(value.size()>512){size_t end=512;while(end&&((unsigned char)value[end]&0xc0)==0x80)--end;value.resize(end);value+="...";}
        for(unsigned i=0;i<issue.count;i++)if(issue.details[i]==value)return;
        if(issue.count<issue.details.size())issue.details[issue.count++]=std::move(value);
    }
    LoadingIssue snapshot(){std::lock_guard<std::mutex> lock(mutex);return issue;}
};
}
