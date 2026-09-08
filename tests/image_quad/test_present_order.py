"""Compile production display submission with mocked GPU calls; no hardware proof."""
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[2]
source = (root / 'vendor/borealis/library/include/borealis/extern/nanovg/nanovg_gxm_utils.h').read_text()
def function(signature):
    start = source.index(signature + ' {')
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]
production = '\n'.join(function(s) for s in (
    'static void display_queue_callback(const void *callbackData)',
    'void gxmEndFrame(void)', 'void gxmSetPresentCompletionWait(int enabled)',
    'void gxmSetNativePresent(int enabled)',
    'int gxmSetAsyncPresent(int enabled)', 'void gxmSwapBuffer(void)'))
fixture = r'''
#include <cassert>
#include <vector>
#include <cstring>
#define DISPLAY_BUFFER_COUNT 3
#define DISPLAY_STRIDE 960
#define DISPLAY_WIDTH 960
#define DISPLAY_HEIGHT 544
#define DISPLAY_PIXEL_FORMAT 0
#define SCE_DISPLAY_SETBUF_NEXTFRAME 1
struct SceGxmNotification { volatile unsigned int* address; unsigned int value; };
struct SceDisplayFrameBuf { int size; void* base; int pitch, pixelformat, width, height; };
struct Surface { void* surface_addr; void* sync_object; };
struct NVGXMframebuffer { Surface gxm_color_surfaces[3]; int gxm_front_buffer_index=0, gxm_back_buffer_index=1; struct { int display_buffer_count=3; } initOptions; };
struct NVGXMwindow { NVGXMframebuffer* fb; };
struct { NVGXMwindow* window; void* context; struct { int swapInterval=1; } initOptions; } gxm_internal;
struct display_queue_callback_data { void* addr; SceGxmNotification completion; };
static int gxm_present_completion_wait=0;
static volatile unsigned int* gxm_present_notifications=nullptr;
static unsigned int gxm_present_serial[3]={};
static SceGxmNotification gxm_present_completed_scene={};
static NVGXMframebuffer* gxm_active_framebuffer=nullptr;
volatile unsigned int region[3]={};
bool region_available=true;
SceGxmNotification queued_notification{}, pending_notification{};
std::vector<int> calls;
int queue_error=0;
void sceGxmFinish(void*) { calls.push_back(1); }
void sceGxmDisplayQueueFinish() { calls.push_back(3); }
volatile unsigned int* sceGxmGetNotificationRegion() { return region_available ? region : nullptr; }
int sceGxmEndScene(void*, void*, const SceGxmNotification* n) { calls.push_back(4); pending_notification=n ? *n : SceGxmNotification{}; return 0; }
int sceGxmNotificationWait(const SceGxmNotification* n) {
    calls.push_back(5); assert(n->address==pending_notification.address && n->value==pending_notification.value);
    // Model a GPU completing only once its exact-frame wait is reached.
    *n->address=n->value; return 0;
}
void sceDisplaySetFrameBuf(SceDisplayFrameBuf*, int) { calls.push_back(6); }
int sceDisplayWaitVblankStartMulti(int) { calls.push_back(7); return 0; }
int sceGxmDisplayQueueAddEntry(void* old_sync, void* new_sync, void* data) {
    assert(old_sync==(void*)1 && new_sync==(void*)2);
    assert(((display_queue_callback_data*)data)->addr==(void*)20);
    queued_notification=((display_queue_callback_data*)data)->completion;
    calls.push_back(2); return queue_error;
}
#define GXM_CHECK_VOID(expr) do { if ((expr)!=0) return; } while(0)
'''
tests = r'''
int main() {
    NVGXMframebuffer fb; fb.gxm_color_surfaces[0]={(void*)10,(void*)1}; fb.gxm_color_surfaces[1]={(void*)20,(void*)2};
    NVGXMwindow window{&fb}; gxm_internal.window=&window;
    gxmSwapBuffer(); assert((calls==std::vector<int>{2})); assert(fb.gxm_front_buffer_index==1 && fb.gxm_back_buffer_index==2);
    fb.gxm_front_buffer_index=0; fb.gxm_back_buffer_index=1; calls.clear();
    gxmSetPresentCompletionWait(1); gxmSwapBuffer(); assert((calls==std::vector<int>{1,2}));
    fb.gxm_front_buffer_index=0; fb.gxm_back_buffer_index=1; calls.clear(); queue_error=-1;
    gxmSwapBuffer(); assert((calls==std::vector<int>{1,2})); assert(fb.gxm_back_buffer_index==1);
    calls.clear(); window.fb=nullptr; gxmSwapBuffer(); assert(calls.empty());
    gxm_internal.window=nullptr; gxmSwapBuffer(); assert(calls.empty());
    gxm_internal.window=&window; window.fb=&fb; queue_error=0;
    assert(gxmSetAsyncPresent(1)==1); assert(gxm_present_completion_wait==2);
    calls.clear(); gxm_active_framebuffer=&fb; gxmEndFrame(); gxmSwapBuffer();
    assert((calls==std::vector<int>{4,2})); // no producer-side finish
    assert(queued_notification.address==&region[1] && queued_notification.value==1);
    display_queue_callback_data saved{(void*)20,queued_notification};
    gxm_present_completed_scene={}; // callback must use its copied notification
    calls.clear(); display_queue_callback(&saved); assert((calls==std::vector<int>{5,6,7}));
    fb.gxm_front_buffer_index=0; fb.gxm_back_buffer_index=1;
    calls.clear(); gxmEndFrame(); gxmSwapBuffer(); assert(queued_notification.value==2);
    saved.completion=queued_notification; display_queue_callback(&saved);
    fb.gxm_front_buffer_index=0; fb.gxm_back_buffer_index=1;
    gxm_present_completed_scene={}; calls.clear(); gxmSwapBuffer();
    assert((calls==std::vector<int>{1,2})); assert(!queued_notification.address);
    region_available=false; assert(gxmSetAsyncPresent(1)==0); assert(gxm_present_completion_wait==1);
    fb.gxm_front_buffer_index=0; fb.gxm_back_buffer_index=1;
    calls.clear(); gxmSwapBuffer(); assert((calls==std::vector<int>{1,2}));
    fb.gxm_front_buffer_index=0; fb.gxm_back_buffer_index=1;
    gxmSetNativePresent(1); calls.clear(); gxmSwapBuffer();
    assert((calls==std::vector<int>{2,1}));
}
'''
out = root / 'build/present-order-test'
out.mkdir(parents=True, exist_ok=True)
(out / 'test.cpp').write_text(fixture + production + tests)
subprocess.run(['g++', '-std=c++17', '-fsanitize=address,undefined', str(out/'test.cpp'), '-o', str(out/'test')], check=True)
subprocess.run([str(out/'test')], check=True)
print('Production present ordering and queue-failure checks passed; hardware flicker untested.')
