#include "update_check.hpp"
#include "files.h"
#include <curl/curl.h>
#include <psp2/sysmodule.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/kernel/threadmgr.h>
#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string_view>

namespace direct {
namespace {
std::mutex networkMutex;

struct Network {
    bool module=false,net=false,ctl=false;
    void* memory=nullptr;
    bool open(bool requireLink=true){
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
        if(!requireLink)return true;
        ctl=sceNetCtlInit()>=0;
        int state=0;
        return sceNetCtlInetGetState(&state)>=0&&state==SCE_NETCTL_STATE_CONNECTED;
    }
    ~Network(){if(ctl)sceNetCtlTerm();if(net)sceNetTerm();std::free(memory);if(module)sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);}
};
struct CurlGlobal {
    const bool ready=curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK;
    ~CurlGlobal(){if(ready)curl_global_cleanup();}
};
using Curl=std::unique_ptr<CURL,decltype(&curl_easy_cleanup)>;
struct Transfer {
    std::string body;
    const std::atomic<bool>* cancelled=nullptr;
    size_t limit=updateMaxBody;
};
size_t receive(char* data,size_t size,size_t count,void* opaque){
    auto& t=*static_cast<Transfer*>(opaque);
    if((t.cancelled&&t.cancelled->load())||size==0||count>t.limit/size)return 0;
    const auto bytes=size*count;
    if(bytes>t.limit-t.body.size())return 0;
    try {t.body.append(data,bytes);}catch(...){return 0;}
    return bytes;
}
int progress(void* opaque,curl_off_t,curl_off_t,curl_off_t,curl_off_t){return static_cast<Transfer*>(opaque)->cancelled->load()?1:0;}

void configure_http(CURL* curl,const char* protocols){
    curl_easy_setopt(curl,CURLOPT_CAINFO,"app0:assets/update-ca.pem");
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L);
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L);
    curl_easy_setopt(curl,CURLOPT_SSLVERSION,long(CURL_SSLVERSION_TLSv1_2));
    curl_easy_setopt(curl,CURLOPT_PROTOCOLS_STR,protocols);
    curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,0L);
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT_MS,3000L);
    curl_easy_setopt(curl,CURLOPT_TIMEOUT_MS,8000L);
    curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,receive);
}

std::string unison_request(CURL* curl,const std::string& url,Transfer& transfer){
    curl_easy_setopt(curl,CURLOPT_URL,url.c_str());
    curl_easy_setopt(curl,CURLOPT_MAXFILESIZE_LARGE,curl_off_t(transfer.limit));
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,&transfer);
    const auto code=curl_easy_perform(curl);
    if(code!=CURLE_OK)return curl_easy_strerror(code);
    long status=0;
    curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
    if(status!=200)return "Save synchronization HTTP " + std::to_string(status);
    return {};
}

std::string_view trim_status(std::string_view value){
    const auto first=value.find_first_not_of(" \t\r\n");
    if(first==value.npos)return {};
    return value.substr(first,value.find_last_not_of(" \t\r\n")-first+1);
}

std::string status_error(std::string_view status){
    if(status.substr(0,3)=="NG,"){
        const auto message=trim_status(status.substr(3));
        if(!message.empty())return std::string(message);
    }
    return "Invalid save synchronization response";
}

std::string sync_save(const char* path,std::string server,const char* appname,const char* id,const char* pass,
    bool& downloaded){
    const char* filename=path;
    for(const char* i=path;*i;++i)if(*i=='/'||*i=='\\')filename=i+1;
    if(!*filename||server.empty()||server.find('#')!=server.npos)return "Invalid unison parameter";
    if(server.find("://")==server.npos)server="http://"+server;

    const auto local=host_save_info(path);
    const auto localTime=local.modified;
    const auto localLength=local.length;
    if(localLength < -1)return "Local save is unavailable";
    if(localLength>HOST_UNISON_MAX_SAVE)return "Local save is too large to synchronize";

    const std::lock_guard<std::mutex> lock(networkMutex);
    Network network;
    if(!network.open(false))return "Save synchronization network is unavailable";
    const CurlGlobal global;
    if(!global.ready)return "Save synchronization initialization failed";
    Curl curl(curl_easy_init(),curl_easy_cleanup);
    if(!curl)return "Save synchronization allocation failed";
    configure_http(curl.get(),"http,https");
    curl_easy_setopt(curl.get(),CURLOPT_DISALLOW_USERNAME_IN_URL,1L);

    const std::pair<const char*,const char*> fields[]={{"appname",appname},{"id",id},{"pass",pass},{"filename",filename}};
    std::string query;
    for(const auto& field:fields){
        std::unique_ptr<char,decltype(&curl_free)> escaped(curl_easy_escape(curl.get(),field.second,0),curl_free);
        if(!escaped)return "Save synchronization allocation failed";
        query+='&';
        query+=field.first;
        query+='=';
        query+=escaped.get();
    }
    std::string endpoint=server;
    if(endpoint.find('?')==endpoint.npos)endpoint+='?';
    else if(endpoint.back()!='?'&&endpoint.back()!='&')endpoint+='&';
    endpoint+="mode=";

    Transfer info;
    auto error=unison_request(curl.get(),endpoint+"info"+query,info);
    if(!error.empty())return error;
    const auto status=trim_status(info.body);
    if(status.substr(0,3)!="OK,")return status_error(status);
    auto timeField=trim_status(status.substr(3));
    timeField=trim_status(timeField.substr(0,timeField.find(',')));
    if(timeField.empty())return "Invalid remote save timestamp";
    int64_t remoteSeconds=0;
    const auto parsed=std::from_chars(timeField.data(),timeField.data()+timeField.size(),remoteSeconds);
    if(parsed.ec!=std::errc()||parsed.ptr!=timeField.data()+timeField.size())return "Invalid remote save timestamp";
    const uint64_t remoteTime=remoteSeconds>0?static_cast<uint64_t>(remoteSeconds):0;
    if(remoteTime==localTime)return {};

    if(remoteTime>localTime){
        Transfer download;
        download.limit=HOST_UNISON_MAX_SAVE;
        error=unison_request(curl.get(),endpoint+"down"+query,download);
        if(!error.empty())return error;
        char* contentType=nullptr;
        curl_easy_getinfo(curl.get(),CURLINFO_CONTENT_TYPE,&contentType);
        const std::string_view mimeType=contentType?contentType:"";
        const auto downloadStatus=trim_status(download.body);
        if(mimeType.substr(0,9)=="text/html"||mimeType.substr(0,21)=="application/xhtml+xml")
            return "Invalid save download response";
        if((mimeType.empty()||mimeType.substr(0,5)=="text/")&&downloadStatus.substr(0,3)=="NG,")
            return status_error(downloadStatus);
        const int result=host_save_write_remote(path,reinterpret_cast<const uint8_t*>(download.body.data()),
            static_cast<int>(download.body.size()),remoteTime,local);
        if(result==-2)return "Local save changed during synchronization";
        if(result<0)return "Unable to write synchronized save";
        downloaded=true;
        return {};
    }

    std::string bytes(static_cast<size_t>(localLength),'\0');
    if(host_save_read_unchanged(path,reinterpret_cast<uint8_t*>(bytes.data()),local)!=localLength)
        return "Local save changed during synchronization";
    std::unique_ptr<curl_mime,decltype(&curl_mime_free)> mime(curl_mime_init(curl.get()),curl_mime_free);
    if(!mime)return "Save synchronization allocation failed";
    const auto addField=[&mime](const char* key,const char* value){
        auto* part=curl_mime_addpart(mime.get());
        return part&&curl_mime_name(part,key)==CURLE_OK&&curl_mime_data(part,value,CURL_ZERO_TERMINATED)==CURLE_OK;
    };
    if(!addField("mode","up"))return "Unable to prepare save upload";
    for(const auto& field:fields)if(!addField(field.first,field.second))return "Unable to prepare save upload";
    if(!addField("filetime",std::to_string(localTime).c_str()))return "Unable to prepare save upload";
    auto* filePart=curl_mime_addpart(mime.get());
    if(!filePart||curl_mime_name(filePart,"file")!=CURLE_OK||curl_mime_filename(filePart,filename)!=CURLE_OK||
        curl_mime_type(filePart,"application/octet-stream")!=CURLE_OK||
        curl_mime_data(filePart,bytes.data(),bytes.size())!=CURLE_OK||
        curl_easy_setopt(curl.get(),CURLOPT_MIMEPOST,mime.get())!=CURLE_OK)return "Unable to prepare save upload";

    Transfer upload;
    error=unison_request(curl.get(),server,upload);
    curl_easy_setopt(curl.get(),CURLOPT_MIMEPOST,static_cast<curl_mime*>(nullptr));
    if(!error.empty())return error;
    const auto uploadStatus=trim_status(upload.body);
    if(uploadStatus=="OK"||uploadStatus.substr(0,3)=="OK,")return {};
    return status_error(uploadStatus);
}
}
UpdateResponse fetch_github_release(const std::atomic<bool>& cancelled){
    sceKernelChangeThreadPriority(0,191);
    UpdateResponse result;
    const std::lock_guard<std::mutex> lock(networkMutex);
    if(cancelled.load()){result.error="cancelled";return result;}
    Network network;
    if(!network.open()){result.error="offline/network unavailable";return result;}
    const CurlGlobal global;
    if(!global.ready){result.error="HTTPS initialization failed";return result;}
    {
        Curl curl(curl_easy_init(),curl_easy_cleanup);
        if(!curl){result.error="HTTPS allocation failed";}else{
            Transfer transfer{{},&cancelled};
            configure_http(curl.get(),"https");
            curl_easy_setopt(curl.get(),CURLOPT_URL,updateApi);
            curl_easy_setopt(curl.get(),CURLOPT_USERAGENT,"Art3m1sPSV-update-check/1");
            curl_easy_setopt(curl.get(),CURLOPT_MAXFILESIZE_LARGE,curl_off_t(updateMaxBody));
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
    return result;
}
}

extern "C" int art3m1s_host_unison(const char* path,const char* server,const char* appname,const char* id,
    const char* pass,char* output,size_t capacity){
    const auto fail=[output,capacity](std::string_view message){
        if(output&&capacity){
            const size_t bytes=std::min(capacity-1,message.size());
            std::memcpy(output,message.data(),bytes);
            output[bytes]='\0';
        }
        return -1;
    };
    try {
        bool downloaded=false;
        auto error=direct::sync_save(path,server,appname,id,pass,downloaded);
        return error.empty()?static_cast<int>(downloaded):fail(error);
    } catch (...) {
        return fail("Save synchronization failed");
    }
}
