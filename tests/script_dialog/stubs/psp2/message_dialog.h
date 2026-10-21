#pragma once
#include "ime_dialog.h"
enum { SCE_MSG_DIALOG_MODE_USER_MSG=1,SCE_MSG_DIALOG_BUTTON_TYPE_OK=0,SCE_MSG_DIALOG_BUTTON_TYPE_OK_CANCEL=3,SCE_MSG_DIALOG_BUTTON_ID_OK=1 };
struct SceMsgDialogUserMessageParam {int buttonType=0;const char* msg=nullptr;};
struct SceMsgDialogParam {int mode=0;SceMsgDialogUserMessageParam* userMsgParam=nullptr;};
struct SceMsgDialogResult {int result=0,buttonId=0;};
inline void sceMsgDialogParamInit(SceMsgDialogParam* p){*p={};}
int sceMsgDialogInit(const SceMsgDialogParam*);
int sceMsgDialogGetStatus();
int sceMsgDialogAbort();
int sceMsgDialogTerm();
int sceMsgDialogGetResult(SceMsgDialogResult*);
