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
} HostMediaInput;
int host_media_input_open(HostMediaInput *input,const char *path);
void host_media_input_close(HostMediaInput *input);
