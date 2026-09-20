#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void art3m1s_register_log_callback(void (*callback)(const char*, const char*));
void art3m1s_register_media_command_callback(void (*callback)(const char*, const char*));
void art3m1s_register_file_reader(int (*callback)(const char*, uint8_t*, int, int64_t));
void art3m1s_register_file_writer(int (*callback)(const char*, const uint8_t*, int));
void art3m1s_register_file_delete(int (*callback)(const char*));
void* art3m1s_runtime_create(uint32_t width, uint32_t height, int backend);
void art3m1s_runtime_destroy(void* runtime);
int art3m1s_runtime_load_project_bytes(void* runtime, const uint8_t* bytes, size_t length, const char* platform);
int art3m1s_runtime_advance_and_present(void* runtime, uint32_t delta_ms);
int art3m1s_runtime_advance_without_render(void* runtime, uint32_t delta_ms);
int art3m1s_runtime_present_gxm(void* runtime);
void art3m1s_runtime_feed_key(void* runtime, uint32_t key, int pressed);
void art3m1s_runtime_feed_mouse(void* runtime, int x, int y);
void art3m1s_runtime_feed_mouse_button(void* runtime, uint32_t button, int pressed);
void art3m1s_runtime_feed_touch(void* runtime, uint32_t id, uint8_t phase, int x, int y);
uint32_t art3m1s_runtime_stage_width(const void* runtime);
uint32_t art3m1s_runtime_stage_height(const void* runtime);
int art3m1s_runtime_is_exit_requested(const void* runtime);

#ifdef __cplusplus
}
#endif
