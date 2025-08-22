#ifndef AME_INPUT_BRIDGE_H
#define AME_INPUT_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

// Initialize/shutdown asyncinput-based input bridge
void ame_input_init(void);
void ame_input_shutdown(void);

// Call at the start of each frame on logic thread to compute edges
void ame_input_begin_frame(void);

// Query current per-frame values
// move_dir: -1 (left), 0 (none), 1 (right)
int ame_input_move_dir(void);
// jump_edge: 1 if jump pressed this frame, else 0
int ame_input_jump_edge(void);

#ifdef __cplusplus
}
#endif

#endif // AME_INPUT_BRIDGE_H
