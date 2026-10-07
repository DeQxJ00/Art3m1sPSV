#include "update_check.hpp"
#include <curl/curl.h>
#include <psp2/sysmodule.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/kernel/threadmgr.h>
#include <cstdlib>
#include <memory>

namespace direct {
namespace {
struct Network {
    bool module=false,net=false,ctl=false;
    void* memory=nullptr;
    bool open(){
        if(sceSysmoduleIsLoaded(SCE_SYSMODULE_NET)<0){
            if(sceSysmoduleLoadModule(SCE_SYSMODULE_NET)<0)return false;
            module=true;
        }
        if(sceNetShowNetstat()<0){
            memory=std::malloc(256*1024);if(!memory)return false;
            SceNetInitParam p{};p.memory=memory;p.size=256*1024;
            if(sceNetInit(&p)<0)return false;
            net=true;
        }
        ctl=sceNetCtlInit()>=0;
        int state=0;
        return sceNetCtlInetGetState(&state)>=0&&state==SCE_NETCTL_STATE_CONNECTED;
    }
    ~Network(){if(ctl)sceNetCtlTerm();if(net)sceNetTerm();std::free(memory);if(module)sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);}
};
struct Transfer {std::string body;const std::atomic<bool>& cancelled;};
size_t receive(char* data,size_t size,size_t count,void* opaque){
    auto& t=*static_cast<Transfer*>(opaque);
    if(t.cancelled.load()||size==0||count>updateMaxBody/size)return 0;
    const auto bytes=size*count;
    if(bytes>updateMaxBody-t.body.size())return 0;
    try {t.body.append(data,bytes);}catch(...){return 0;}
    return bytes;
}
int progress(void* opaque,curl_off_t,curl_off_t,curl_off_t,curl_off_t){return static_cast<Transfer*>(opaque)->cancelled.load()?1:0;}
}
UpdateResponse fetch_github_release(const std::atomic<bool>& cancelled){
    sceKernelChangeThreadPriority(0,191);
    UpdateResponse result;
    if(cancelled.load()){result.error="cancelled";return result;}
    Network network;
    if(!network.open()){result.error="offline/network unavailable";return result;}
    if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK){result.error="HTTPS initialization failed";return result;}
    {
        std::unique_ptr<CURL,decltype(&curl_easy_cleanup)> curl(curl_easy_init(),curl_easy_cleanup);
        if(!curl){result.error="HTTPS allocation failed";}else{
            Transfer transfer{{},cancelled};
            curl_easy_setopt(curl.get(),CURLOPT_URL,updateApi);
            curl_easy_setopt(curl.get(),CURLOPT_USERAGENT,"Art3m1sPSV-update-check/1");
            curl_easy_setopt(curl.get(),CURLOPT_CAINFO,"app0:assets/update-ca.pem");
            curl_easy_setopt(curl.get(),CURLOPT_SSL_VERIFYPEER,1L);
            curl_easy_setopt(curl.get(),CURLOPT_SSL_VERIFYHOST,2L);
            curl_easy_setopt(curl.get(),CURLOPT_SSLVERSION,long(CURL_SSLVERSION_TLSv1_2));
            curl_easy_setopt(curl.get(),CURLOPT_PROTOCOLS_STR,"https");
            curl_easy_setopt(curl.get(),CURLOPT_FOLLOWLOCATION,0L);
            curl_easy_setopt(curl.get(),CURLOPT_CONNECTTIMEOUT_MS,3000L);
            curl_easy_setopt(curl.get(),CURLOPT_TIMEOUT_MS,8000L);
            curl_easy_setopt(curl.get(),CURLOPT_NOSIGNAL,1L);
            curl_easy_setopt(curl.get(),CURLOPT_MAXFILESIZE_LARGE,curl_off_t(updateMaxBody));
            curl_easy_setopt(curl.get(),CURLOPT_WRITEFUNCTION,receive);
            curl_easy_setopt(curl.get(),CURLOPT_WRITEDATA,&transfer);
            curl_easy_setopt(curl.get(),CURLOPT_NOPROGRESS,0L);
            curl_easy_setopt(curl.get(),CURLOPT_XFERINFOFUNCTION,progress);
            curl_easy_setopt(curl.get(),CURLOPT_XFERINFODATA,&transfer);
            auto* headers=curl_slist_append(nullptr,"Accept: application/vnd.github+json");
            headers=curl_slist_append(headers,"X-GitHub-Api-Version: 2022-11-28");
            curl_easy_setopt(curl.get(),CURLOPT_HTTPHEADER,headers);
            const auto code=curl_easy_perform(curl.get());
            if(code==CURLE_OK){curl_easy_getinfo(curl.get(),CURLINFO_RESPONSE_CODE,&result.status);result.body=std::move(transfer.body);}
            else result.error=curl_easy_strerror(code);
            curl_slist_free_all(headers);
        }
    }
    curl_global_cleanup();return result;
}
}
