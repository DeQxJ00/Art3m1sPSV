#pragma once
#include <stdint.h>
typedef struct HostVorbis HostVorbis;
/* Returns NULL for unsupported streams; caller can reopen with FFmpeg. */
HostVorbis *host_vorbis_open(const char *path, int *rate, int *channels);
int host_vorbis_read(HostVorbis *, int16_t *pcm, int frames);
int host_vorbis_rewind(HostVorbis *);
void host_vorbis_close(HostVorbis *);
