#pragma once
#include <stdint.h>
#include <stddef.h>
typedef struct HostVorbis HostVorbis;
/* Returns NULL for unsupported streams; caller can reopen with FFmpeg. */
HostVorbis *host_vorbis_open(const char *path, int *rate, int *channels);
/* Compressed bytes only; oversized/budget-limited sources retain streaming.
 * Cancellation is polled during preparation, never retained after return. */
#define HOST_VORBIS_PRELOAD_FILE_LIMIT (2u * 1024u * 1024u)
#define HOST_VORBIS_PRELOAD_TOTAL_LIMIT (8u * 1024u * 1024u)
typedef int (*HostAudioCancelled)(void *);
HostVorbis *host_vorbis_open_preloaded(const char *, int *, int *, HostAudioCancelled, void *);
size_t host_vorbis_preloaded_bytes(const HostVorbis *);
size_t host_vorbis_preload_bytes_in_use(void);
int host_vorbis_read(HostVorbis *, int16_t *pcm, int frames);
int host_vorbis_rewind(HostVorbis *);
void host_vorbis_close(HostVorbis *);
