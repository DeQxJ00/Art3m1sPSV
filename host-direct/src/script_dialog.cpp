#include "script_dialog.hpp"
#include "dialog_text.hpp"
#include "runtime_api.h"
#include "cJSON.h"
#include <psp2/ime_dialog.h>
#include <psp2/message_dialog.h>
#include <psp2/sysmodule.h>
#include <deque>
#include <memory>
#include <utility>
#include <cmath>
#include <cstring>

namespace direct {
void log(const char*,...);
void wait();
void set_frame_overlay(void (*callback)(void*,SceGxmSyncObject*));
}
namespace {
void* runtime=nullptr;
enum class Mode { None, Input, Message } mode=Mode::None;
std::deque<std::pair<std::string,std::string>> commands;
uint16_t title[SCE_IME_DIALOG_MAX_TITLE_LENGTH+1]{},initial[SCE_IME_DIALOG_MAX_TEXT_LENGTH+1]{},output[SCE_IME_DIALOG_MAX_TEXT_LENGTH+1]{};
size_t limit=SCE_IME_DIALOG_MAX_TEXT_LENGTH;
std::string message;
bool ownedIme=false;
int lastRenderError=0;
const char* string(cJSON* j,const char* key){const auto* v=cJSON_GetObjectItemCaseSensitive(j,key);return cJSON_IsString(v)&&v->valuestring?v->valuestring:"";}
bool boolean(cJSON* j,const char* key){return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j,key));}
void overlay(void* pixels,SceGxmSyncObject* sync){
    SceCommonDialogUpdateParam p{};
    p.renderTarget.colorSurfaceData=pixels;p.renderTarget.surfaceType=SCE_GXM_COLOR_SURFACE_LINEAR;
    p.renderTarget.colorFormat=SCE_GXM_COLOR_FORMAT_A8B8G8R8;
    p.renderTarget.width=960;p.renderTarget.height=544;p.renderTarget.strideInPixels=1024;p.displaySyncObject=sync;
    const int result=sceCommonDialogUpdate(&p);
    if(result<0&&result!=lastRenderError)direct::log("[script-dialog] render_error=%08x",unsigned(result));
    lastRenderError=result;
}
void terminate(){
    direct::set_frame_overlay(nullptr);
    if(mode==Mode::None)return;
    direct::wait();
    if(mode==Mode::Input){if(sceImeDialogGetStatus()==SCE_COMMON_DIALOG_STATUS_RUNNING)sceImeDialogAbort();sceImeDialogTerm();}
    else{if(sceMsgDialogGetStatus()==SCE_COMMON_DIALOG_STATUS_RUNNING)sceMsgDialogAbort();sceMsgDialogTerm();}
    mode=Mode::None;message.clear();
}
void respond(bool accepted,const std::string* text){
    terminate();
    direct::log("[script-dialog] response accepted=%d text_bytes=%u",int(accepted),unsigned(text?text->size():0));
    if(runtime)art3m1s_runtime_submit_dialog(runtime,accepted,text?text->c_str():nullptr);
}
void open(cJSON* j){
    const bool input=boolean(j,"textfield"),cancel=boolean(j,"hasCancel");
    int result=0;
    if(input){
        if(sceSysmoduleIsLoaded(SCE_SYSMODULE_IME)!=0){result=sceSysmoduleLoadModule(SCE_SYSMODULE_IME);if(result>=0)ownedIme=true;}
        limit=SCE_IME_DIALOG_MAX_TEXT_LENGTH;
        const auto* size=cJSON_GetObjectItemCaseSensitive(j,"textfieldSize");
        if(cJSON_IsNumber(size)&&std::isfinite(size->valuedouble))limit=size_t(std::clamp(size->valuedouble,0.0,double(SCE_IME_DIALOG_MAX_TEXT_LENGTH)));
        const std::string label=string(j,"title");
        // Text-entry messages are the explicit initial value; the destination
        // variable may still contain the result of an earlier dialog.
        const auto* body=cJSON_GetObjectItemCaseSensitive(j,"message");
        const char* value=cJSON_IsString(body)?string(j,"message"):string(j,"initialText");
        auto t=direct::dialog_utf16(label,SCE_IME_DIALOG_MAX_TITLE_LENGTH,SCE_IME_DIALOG_MAX_TITLE_LENGTH);
        auto v=direct::dialog_utf16(value,SCE_IME_DIALOG_MAX_TEXT_LENGTH,limit);
        std::memset(title,0,sizeof title);std::memset(initial,0,sizeof initial);std::memset(output,0,sizeof output);
        std::copy(t.begin(),t.end(),title);std::copy(v.begin(),v.end(),initial);
        SceImeDialogParam p;sceImeDialogParamInit(&p);
        p.type=SCE_IME_TYPE_DEFAULT;p.dialogMode=cancel?SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL:SCE_IME_DIALOG_DIALOG_MODE_DEFAULT;
        p.maxTextLength=unsigned(std::max(size_t(1),limit));p.title=title;p.initialText=initial;p.inputTextBuffer=output;
        if(result>=0)result=sceImeDialogInit(&p);
        if(result>=0)mode=Mode::Input;
    }else{
        message=string(j,"title");const std::string body=string(j,"message");
        if(!body.empty()){if(!message.empty())message+="\n";message+=body;}
        SceMsgDialogUserMessageParam user{};user.buttonType=cancel?SCE_MSG_DIALOG_BUTTON_TYPE_OK_CANCEL:SCE_MSG_DIALOG_BUTTON_TYPE_OK;user.msg=reinterpret_cast<decltype(user.msg)>(message.c_str());
        SceMsgDialogParam p;sceMsgDialogParamInit(&p);p.mode=SCE_MSG_DIALOG_MODE_USER_MSG;p.userMsgParam=&user;
        result=sceMsgDialogInit(&p);if(result>=0)mode=Mode::Message;
    }
    direct::log("[script-dialog] open input=%d cancel=%d limit=%u result=%08x",int(input),int(cancel),unsigned(limit),unsigned(result));
    if(result<0){respond(false,nullptr);return;}
    lastRenderError=0;direct::set_frame_overlay(overlay);
}
}
void script_dialog_attach(void* value){script_dialog_detach();runtime=value;art3m1s_register_ui_command_callback(script_dialog_command);}
void script_dialog_detach(){terminate();runtime=nullptr;commands.clear();if(ownedIme){sceSysmoduleUnloadModule(SCE_SYSMODULE_IME);ownedIme=false;}}
void script_dialog_command(const char* kind,const char* json){if(runtime&&kind&&json)commands.emplace_back(kind,json);}
bool script_dialog_busy(){
    if(mode!=Mode::None)return true;
    for(const auto& command:commands)if(command.first=="dialog_show")return true;
    return false;
}
void script_dialog_pump(){
    if(!runtime)return;
    if(mode==Mode::Input){
        if(sceImeDialogGetStatus()!=SCE_COMMON_DIALOG_STATUS_FINISHED)return;
        SceImeDialogResult result{};const int status=sceImeDialogGetResult(&result);
        const bool accepted=status>=0&&result.result==SCE_COMMON_DIALOG_RESULT_OK&&result.button==SCE_IME_DIALOG_BUTTON_ENTER;
        const auto text=direct::dialog_utf8(output,SCE_IME_DIALOG_MAX_TEXT_LENGTH,limit);
        respond(accepted,accepted?&text:nullptr);return;
    }
    if(mode==Mode::Message){
        if(sceMsgDialogGetStatus()!=SCE_COMMON_DIALOG_STATUS_FINISHED)return;
        SceMsgDialogResult result{};const int status=sceMsgDialogGetResult(&result);
        respond(status>=0&&result.result==SCE_COMMON_DIALOG_RESULT_OK&&result.buttonId==SCE_MSG_DIALOG_BUTTON_ID_OK,nullptr);return;
    }
    // Limit the drain to its initial size: a response can enqueue a new dialog.
    for(size_t left=commands.size();left&&!commands.empty()&&mode==Mode::None;--left){
        auto command=std::move(commands.front());commands.pop_front();
        std::unique_ptr<cJSON,decltype(&cJSON_Delete)> j(cJSON_Parse(command.second.c_str()),cJSON_Delete);
        if(command.first=="dialog_show"){if(j)open(j.get());else respond(false,nullptr);return;}
        // Registering a UI callback must not strand other script waits.
        if(command.first=="http_request")art3m1s_runtime_submit_http_result(runtime,0,nullptr,0);
        else if(command.first=="purchase"&&j){const char* name=string(j.get(),"varname");if(*name)art3m1s_runtime_set_string_variable(runtime,name,"-1");}
        // callnative already has its empty failure default in the core.
        else direct::log("[script-ui] unsupported command=%s",command.first.c_str());
    }
}
