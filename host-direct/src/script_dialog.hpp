#pragma once

// Owner-thread only. Commands are queued: never re-enter the runtime from its
// synchronous UI callback. Pump before advancing the game or beginning GXM.
void script_dialog_attach(void* runtime);
void script_dialog_detach();
void script_dialog_command(const char* kind, const char* json);
void script_dialog_pump();
bool script_dialog_busy();
