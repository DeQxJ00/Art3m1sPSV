#pragma once
#include <cstdint>
struct SceGxmSyncObject {};
enum { SCE_GXM_COLOR_SURFACE_LINEAR=0, SCE_GXM_COLOR_FORMAT_A8B8G8R8=1 };
enum { SCE_COMMON_DIALOG_STATUS_NONE, SCE_COMMON_DIALOG_STATUS_RUNNING, SCE_COMMON_DIALOG_STATUS_FINISHED };
enum { SCE_COMMON_DIALOG_RESULT_OK, SCE_COMMON_DIALOG_RESULT_USER_CANCELED };
enum { SCE_IME_DIALOG_MAX_TITLE_LENGTH=128,SCE_IME_DIALOG_MAX_TEXT_LENGTH=2048,SCE_IME_TYPE_DEFAULT=0,
       SCE_IME_DIALOG_DIALOG_MODE_DEFAULT=0,SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL=1,
       SCE_IME_DIALOG_BUTTON_NONE=0,SCE_IME_DIALOG_BUTTON_CLOSE=1,SCE_IME_DIALOG_BUTTON_ENTER=2 };
struct SceCommonDialogUpdateParam {
    struct {void* colorSurfaceData=nullptr;int surfaceType=0,colorFormat=0;unsigned width=0,height=0,strideInPixels=0;} renderTarget;
    SceGxmSyncObject* displaySyncObject=nullptr;
};
struct SceImeDialogParam { unsigned type=0,dialogMode=0,maxTextLength=0;const uint16_t* title=nullptr;uint16_t* initialText=nullptr;uint16_t* inputTextBuffer=nullptr; };
struct SceImeDialogResult {int result=0,button=0;};
inline void sceImeDialogParamInit(SceImeDialogParam* p){*p={};}
int sceImeDialogInit(const SceImeDialogParam*);
int sceImeDialogGetStatus();
int sceImeDialogAbort();
int sceImeDialogTerm();
int sceImeDialogGetResult(SceImeDialogResult*);
int sceCommonDialogUpdate(const SceCommonDialogUpdateParam*);
