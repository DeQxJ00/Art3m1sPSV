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
int art3m1s_launcher_decode_icon(const uint8_t* png, size_t png_len, uint8_t* rgba, size_t rgba_len);
int art3m1s_launcher_extract_exe_icon(const uint8_t* exe, size_t exe_len, uint8_t* rgba, size_t rgba_len);
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
void art3m1s_runtime_set_toolbar_hidden(void* runtime, int hidden);
void art3m1s_runtime_set_dialogue_volume_hidden(void* runtime, int hidden);
int art3m1s_runtime_set_message_position(void* runtime, int enabled, int hide_subtitle, int dx, int dy, int sx, int sy);
int art3m1s_runtime_set_message_font_sizes_separate(void* runtime, int enabled, uint32_t name, uint32_t dialogue, uint32_t subtitle);
int art3m1s_runtime_set_emote_mesh_ratio(void* runtime, float ratio);
int art3m1s_runtime_set_ignore_background_alpha(void* runtime, int enabled);
uint32_t art3m1s_runtime_stage_width(const void* runtime);
uint32_t art3m1s_runtime_stage_height(const void* runtime);
int art3m1s_runtime_is_exit_requested(const void* runtime);

#ifdef __cplusplus
}
#endif
