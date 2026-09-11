#ifndef ART3M1S_AUDIO_PATH_H
#define ART3M1S_AUDIO_PATH_H
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int host_audio_resolve_path(char *out, size_t capacity, const char *path,
                                   int (*exists)(const char *)) {
    if (!out || !capacity || !path || !*path || !exists) return -1;
    const char *extensions[] = {"", ".ogg", ".oga", ".wav", ".mp3", ".m4a", ".at9"};
    for (size_t i = 0; i < sizeof(extensions) / sizeof(*extensions); ++i) {
        int n = snprintf(out, capacity, "%s%s", path, extensions[i]);
        if (n >= 0 && (size_t)n < capacity && exists(out)) return 0;
    }
    // Vita releases can retain desktop audio suffixes in their scripts while
    // shipping the same basename as ATRAC9. Never override an existing file.
    const char *dot = strrchr(path, '.');
    if (dot) for (size_t i = 1; i < 6; ++i) {
        if (strcmp(dot, extensions[i])) continue;
        size_t stem = (size_t)(dot - path);
        if (stem > capacity || capacity - stem < sizeof(".at9")) return -1;
        memcpy(out, path, stem);
        memcpy(out + stem, ".at9", sizeof(".at9"));
        return exists(out) ? 0 : -1;
    }
    return -1;
}
#endif
