#pragma once
#include <stdint.h>

// Minimal local input shim for examples, mirroring examples/unitylike_minimal
// Uses asyncinput behind the scenes via AME input bridge.

#ifdef __cplusplus
extern "C" {
#endif

int input_init();
void input_begin_frame();
int input_should_quit();
int input_move_left();
int input_move_right();
int input_jump_edge();
void input_shutdown();

#ifdef __cplusplus
}
#endif

