#include "script_dialog.hpp"
#include "dialog_text.hpp"
#include "runtime_api.h"
#include <psp2/ime_dialog.h>
#include <psp2/message_dialog.h>
#include <cassert>
#include <cstdio>
#include <algorithm>
namespace {
int ime=0,msg=0,initialized=0,submitted=0,lastAccepted=-1,terms=0,aborts=0,loads=0,unloads=0,initError=0;
bool loaded=false,cascade=false;std::string response,purchase;
SceImeDialogParam imeParam{};SceImeDialogResult imeResult{};SceMsgDialogResult msgResult{};
void (*callback)(const char*,const char*)=nullptr;
void (*overlay)(void*,SceGxmSyncObject*)=nullptr;
const char* request=R"({"title":"Name","message":"あいうえ","textfield":true,"hasCancel":true,"textfieldSize":3,"initialText":"old"})";
void finish(bool accepted){imeResult={accepted?SCE_COMMON_DIALOG_RESULT_OK:SCE_COMMON_DIALOG_RESULT_USER_CANCELED,accepted?SCE_IME_DIALOG_BUTTON_ENTER:SCE_IME_DIALOG_BUTTON_CLOSE};ime=SCE_COMMON_DIALOG_STATUS_FINISHED;script_dialog_pump();}
}
namespace direct {void log(const char*,...){} void wait(){} void set_frame_overlay(void(*fn)(void*,SceGxmSyncObject*)){overlay=fn;}}
extern "C" {
void art3m1s_register_ui_command_callback(void(*fn)(const char*,const char*)){callback=fn;}
int art3m1s_runtime_submit_dialog(void*,int accepted,const char* text){assert(!overlay&&ime==0&&msg==0);++submitted;lastAccepted=accepted;response=text?text:"<null>";if(cascade){cascade=false;callback("dialog_show",request);}return 1;}
int art3m1s_runtime_submit_http_result(void*,int status,const uint8_t* p,int n){assert(!status&&!p&&!n);return 1;}
void art3m1s_runtime_set_string_variable(void*,const char*,const char* v){purchase=v;}
}
int sceSysmoduleIsLoaded(int){return loaded?0:-1;}
int sceSysmoduleLoadModule(int){loaded=true;++loads;return 0;}
int sceSysmoduleUnloadModule(int){loaded=false;++unloads;return 0;}
int sceImeDialogInit(const SceImeDialogParam* p){++initialized;if(initError)return initError;imeParam=*p;ime=1;return 0;}
int sceImeDialogGetStatus(){return ime;}
int sceImeDialogAbort(){++aborts;ime=2;return 0;}
int sceImeDialogTerm(){++terms;ime=0;return 0;}
int sceImeDialogGetResult(SceImeDialogResult* r){*r=imeResult;return 0;}
int sceMsgDialogInit(const SceMsgDialogParam* p){assert(p->userMsgParam->buttonType==SCE_MSG_DIALOG_BUTTON_TYPE_OK_CANCEL);msg=1;return 0;}
int sceMsgDialogGetStatus(){return msg;}
int sceMsgDialogAbort(){msg=2;return 0;}
int sceMsgDialogTerm(){msg=0;return 0;}
int sceMsgDialogGetResult(SceMsgDialogResult* r){*r=msgResult;return 0;}
int sceCommonDialogUpdate(const SceCommonDialogUpdateParam* p){assert(p->renderTarget.width==960&&p->renderTarget.height==544&&p->renderTarget.strideInPixels==1024);return 0;}
int main(){
    const std::string unicode=u8"名前😀";auto encoded=direct::dialog_utf16(unicode,4,3);
    assert(encoded.size()==4);uint16_t b[6]{};std::copy(encoded.begin(),encoded.end(),b);
    assert(direct::dialog_utf8(b,6,3)==unicode);
    assert(direct::dialog_utf16(unicode,3,3)==u"名前");
    assert(direct::dialog_utf8(b,6,2)==u8"名前");
    assert(direct::dialog_utf16("\xf0\x80\x80\x80",8,8)==u"\ufffd");
    assert(direct::dialog_utf16("abc",8,0).empty());
    script_dialog_attach(reinterpret_cast<void*>(1));callback("dialog_show",request);
    assert(script_dialog_busy()&&!initialized&&!submitted);script_dialog_pump();
    assert(imeParam.maxTextLength==3&&imeParam.dialogMode==1&&overlay);
    assert(direct::dialog_utf8(imeParam.initialText,2048,2048)==u8"あいう");overlay(nullptr,nullptr);
    std::copy(encoded.begin(),encoded.end(),imeParam.inputTextBuffer);finish(true);
    assert(lastAccepted==1&&response==unicode&&!script_dialog_busy());
    assert(direct::dialog_utf8(imeParam.title,128,128)=="Name");
    callback("dialog_show",R"({"title":"Name","message":"","textfield":true,"initialText":"old"})");script_dialog_pump();
    assert(direct::dialog_utf8(imeParam.initialText,2048,2048).empty());finish(false);
    callback("dialog_show",R"({"title":"Name","message":"テスト","textfield":true,"initialText":""})");script_dialog_pump();
    assert(direct::dialog_utf8(imeParam.initialText,2048,2048)==u8"テスト");finish(false);
    callback("dialog_show",R"({"title":"Name","textfield":true,"initialText":"fallback"})");script_dialog_pump();
    assert(direct::dialog_utf8(imeParam.initialText,2048,2048)=="fallback");finish(false);
    callback("dialog_show",request);script_dialog_pump();finish(false);
    assert(lastAccepted==0&&response=="<null>"&&!script_dialog_busy());
    callback("dialog_show",request);script_dialog_pump();cascade=true;finish(true);
    assert(script_dialog_busy()&&ime==0);script_dialog_pump();assert(ime==1);
    const int before=submitted;script_dialog_detach();assert(submitted==before&&!overlay&&!script_dialog_busy()&&aborts==1&&loads==unloads);
    script_dialog_attach(reinterpret_cast<void*>(2));callback("dialog_show",request);initError=-1;script_dialog_pump();initError=0;assert(lastAccepted==0&&!script_dialog_busy());
    callback("dialog_show",R"({"title":"Confirm","message":"Continue?","textfield":false,"hasCancel":true})");script_dialog_pump();
    msgResult={SCE_COMMON_DIALOG_RESULT_OK,SCE_MSG_DIALOG_BUTTON_ID_OK};msg=2;script_dialog_pump();assert(lastAccepted==1);
    callback("dialog_show",R"({"textfield":false,"hasCancel":true})");script_dialog_pump();
    msgResult={SCE_COMMON_DIALOG_RESULT_USER_CANCELED,0};msg=2;script_dialog_pump();assert(lastAccepted==0);
    callback("http_request","{}");callback("purchase",R"({"varname":"result"})");script_dialog_pump();assert(purchase=="-1");
    script_dialog_detach();assert(loads==unloads);
    puts("PASS: deferred dialog lifecycle, confirm/cancel, Unicode limits, chained requests, init failure, detach and unsupported wait fallback");
}
