#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int host_files_open(const char *root, const char *save_root);
void host_files_close(void);
int host_read(const char *path, uint8_t *out, int capacity, int64_t offset);
int host_write(const char *path, const uint8_t *bytes, int length);
int host_delete(const char *path);
typedef struct HostReadStream HostReadStream;
HostReadStream *host_stream_open(const char *path, int64_t *size);
int host_stream_read(HostReadStream *, uint8_t *, int capacity, int64_t offset);
void host_stream_close(HostReadStream *);
#ifdef __cplusplus
}
#endif
