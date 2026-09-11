#include "../../host/audio_path.h"
#include <assert.h>
static const char *files[4];
static int exists(const char *path) {
    for (int i=0; i<4; ++i) if (files[i] && !strcmp(path,files[i])) return 1;
    return 0;
}
int main(void) {
    char out[512];
    files[0]="se/system/voice.at9";
    assert(!host_audio_resolve_path(out,sizeof(out),"se/system/voice.m4a",exists));
    assert(!strcmp(out,files[0]));
    assert(!host_audio_resolve_path(out,sizeof(out),"se/system/voice",exists));
    assert(!strcmp(out,files[0]));
    files[1]="se/system/voice.m4a";
    assert(!host_audio_resolve_path(out,sizeof(out),files[1],exists));
    assert(!strcmp(out,files[1]));
    files[2]="se/system/voice.ogg";
    assert(!host_audio_resolve_path(out,sizeof(out),"se/system/voice",exists));
    assert(!strcmp(out,files[2]));
    assert(host_audio_resolve_path(out,sizeof(out),"missing.m4a",exists)<0);
    assert(host_audio_resolve_path(out,sizeof(out),"se/system/voice.png",exists)<0);
    files[3]="abc";
    assert(host_audio_resolve_path(out,4,"abcdef",exists)<0); // no truncated-path match
    assert(host_audio_resolve_path(out,4,"abc.m4a",exists)<0);
    return 0;
}
