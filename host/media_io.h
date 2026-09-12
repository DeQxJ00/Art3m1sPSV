#pragma once
#include <libavformat/avformat.h>
#include "files.h"
typedef struct HostMediaInput {
    char path[512];
    int64_t position, size;
    AVIOContext *io;
    AVFormatContext *format;
    HostReadStream *reader;
    uint64_t read_calls,read_bytes,read_us,max_read_us;
    uint8_t *cached;
    size_t cache_charge;
    uint64_t cache_reads,cache_bytes;
} HostMediaInput;
int host_media_input_open(HostMediaInput *input,const char *path);
/* Optional bounded preload of one active stream; failure keeps disk streaming. */
size_t host_media_input_preload(HostMediaInput *input,size_t budget);
void host_media_input_close(HostMediaInput *input);
