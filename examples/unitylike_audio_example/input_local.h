#pragma once
#include <stdatomic.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

// Project-local asyncinput wrapper for unitylike_audio_example
bool input_init(void);
void input_shutdown(void);
void input_begin_frame(void);

// Queries
bool input_should_quit(void);
int  input_move_dir(void);      // -1,0,1 for horizontal movement
int  input_vert_dir(void);      // -1,0,1 for vertical movement
bool input_jump_edge(void);     // just-pressed this frame

#ifdef __cplusplus
}
#endif