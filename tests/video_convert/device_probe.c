/* Independent Vita/MCP probe. Deliberately keeps assertions in Release builds. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <stdio.h>
#include <stdlib.h>
static FILE *probe_log;
static void probe_failed(const char *expression,int line) {
    fprintf(probe_log,"FAIL line=%d expression=%s\n",line,expression);
    fclose(probe_log);exit(3);
}
#define HOST_CONVERT_ASSERT(e) ((e)?(void)0:probe_failed(#e,__LINE__))
#define printf(...) fprintf(probe_log,__VA_ARGS__)
#define main host_video_convert_test_main
#include "test.c"
#undef main

int host_video_convert_device_probe(const char *report) {
    probe_log=fopen(report,"w");if(!probe_log)return 2;
    fprintf(probe_log,"Starting ARM conversion tests\n");fflush(probe_log);
    int result=host_video_convert_test_main(0,NULL);
    printf("Device conversion probe %s; NEON=%d\n",result?"FAIL":"PASS",
#ifdef __ARM_NEON
        1
#else
        0
#endif
    );
    fclose(probe_log);return result;
}
