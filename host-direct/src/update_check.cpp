#include "update_check.hpp"
#include "cJSON.h"
#include <array>
#include <pthread.h>

namespace direct {
namespace {
bool version(const std::string& text,std::array<unsigned,3>& out,bool local){
    if(text.empty()||text.size()>100)return false;
    size_t i=text[0]=='v'?1:0;
    for(unsigned part=0;part<3;++part){
        const size_t begin=i;unsigned value=0;
        while(i<text.size()&&text[i]>='0'&&text[i]<='9'){
            if(i-begin>=6)return false;
            value=value*10+unsigned(text[i++]-'0');
        }
        if(i==begin||(i-begin>1&&text[begin]=='0'))return false;
        out[part]=value;
        if(part<2){if(i==text.size()||text[i++]!='.')return false;}
    }
    return i==text.size()||(local&&(text[i]=='-'||text[i]=='+'));
}
}
std::string newer_release(const std::string& json,const std::string& installed){
    if(json.empty()||json.size()>updateMaxBody)return {};
    // Called on the main thread after the HTTP worker publishes its response.
    // cJSON has a process-global error slot; keep it off the network worker.
    const char* end=nullptr;
    std::unique_ptr<cJSON,decltype(&cJSON_Delete)> root(cJSON_ParseWithLengthOpts(json.c_str(),json.size()+1,&end,1),cJSON_Delete);
    if(!root||end!=json.c_str()+json.size()||!cJSON_IsObject(root.get()))return {};
    auto get=[&](const char* name){return cJSON_GetObjectItemCaseSensitive(root.get(),name);};
    if(!cJSON_IsFalse(get("draft"))||!cJSON_IsFalse(get("prerelease")))return {};
    const auto* tag=get("tag_name");
    if(!cJSON_IsString(tag)||!tag->valuestring)return {};
    std::array<unsigned,3> remote{},current{};
    if(!version(tag->valuestring,remote,false)||!version(installed,current,true)||remote<=current)return {};
    // Do not announce the short CI interval between publishing a release and
    // uploading its usable VPK. No download or remote URL execution occurs.
    const cJSON* asset=nullptr;
    cJSON_ArrayForEach(asset,get("assets")){
        const auto* name=cJSON_GetObjectItemCaseSensitive(asset,"name");
        const auto* state=cJSON_GetObjectItemCaseSensitive(asset,"state");
        if(!cJSON_IsString(name)||!name->valuestring||!cJSON_IsString(state)||!state->valuestring)continue;
        const std::string n=name->valuestring;
        if(n.size()>4&&n.compare(n.size()-4,4,".vpk")==0&&std::string(state->valuestring)=="uploaded")return tag->valuestring;
    }
    return {};
}
struct UpdateCheck::State {
    std::atomic<bool> cancelled{false},ready{false};
    UpdateFetch fetch;
    UpdateResponse response;
    explicit State(UpdateFetch f):fetch(f){}
};
UpdateCheck::UpdateCheck(std::string installed,UpdateFetch fetch):state_(std::make_shared<State>(fetch)),installed_(std::move(installed)){}
UpdateCheck::~UpdateCheck(){state_->cancelled.store(true);}
void* UpdateCheck::worker(void* opaque){
    std::unique_ptr<std::shared_ptr<State>> owner(static_cast<std::shared_ptr<State>*>(opaque));
    auto state=*owner;
    try {state->response=state->fetch(state->cancelled);}catch(...){state->response.error="request failed";}
    state->ready.store(true,std::memory_order_release);
    return nullptr;
}
void UpdateCheck::start(){
    if(started_)return;
    started_=true;
    auto* owner=new std::shared_ptr<State>(state_);
    pthread_attr_t attr;pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr,256*1024);
    pthread_t thread;
    const int result=pthread_create(&thread,&attr,worker,owner);
    pthread_attr_destroy(&attr);
    if(result){delete owner;consumed_=true;diagnostic_="thread unavailable";}
}
void UpdateCheck::tick(bool launcherVisible){
    if(!consumed_&&state_->ready.load(std::memory_order_acquire)){
        consumed_=true;
        auto response=std::move(state_->response);
        diagnostic_=response.error.empty()?"HTTP "+std::to_string(response.status):response.error;
        if(response.error.empty()&&response.status==200)tag_=newer_release(response.body,installed_);
    }
    if(launcherVisible&&!shown_&&!tag_.empty())shown_=true;
}
bool UpdateCheck::visible(bool launcherVisible)const{return launcherVisible&&shown_;}
}
