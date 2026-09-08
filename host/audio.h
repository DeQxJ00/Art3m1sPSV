#pragma once
int host_audio_start(void);
void host_audio_stop(void);
void host_media_command(const char *kind,const char *json);
void host_audio_poll(void *runtime);
