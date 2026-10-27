#pragma once
void host_video_command(const char *kind,const char *json);
void host_video_tick(void *runtime);
void host_video_skip(void *runtime);
void host_video_close(void);
void host_video_present_idle(void);
// True only once a full-screen movie has a frame ready to cover the scene.
int host_video_fullscreen_active(void);
