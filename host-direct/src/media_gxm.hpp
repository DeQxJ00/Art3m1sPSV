#pragma once
void gxm_media_attach(void* runtime);
void gxm_media_detach();
void gxm_media_pump();
void gxm_media_skip();
void gxm_media_command(const char* kind, const char* json);
