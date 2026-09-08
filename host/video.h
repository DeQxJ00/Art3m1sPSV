#pragma once
void host_video_command(const char *kind,const char *json);
void host_video_tick(void *runtime);
void host_video_skip(void *runtime);
void host_video_close(void);
void host_video_present_idle(void);
