#include "input_local.h"
extern "C" {
#include "ame/input_bridge.h"
}

static uint8_t prev_jump = 0;

int input_init() {
    // Initialize asyncinput bridge via AME C helper
    ame_input_bridge_init();
    prev_jump = 0;
    return 1;
}

void input_begin_frame() {
    ame_input_bridge_begin_frame();
}

int input_should_quit() {
    return ame_input_bridge_key_down(AMEK_ESCAPE);
}

int input_move_left() {
    return ame_input_bridge_key(AMEK_A) || ame_input_bridge_key(AMEK_LEFT);
}

int input_move_right() {
    return ame_input_bridge_key(AMEK_D) || ame_input_bridge_key(AMEK_RIGHT);
}

int input_jump_edge() {
    uint8_t now = (uint8_t)(ame_input_bridge_key(AMEK_SPACE) || ame_input_bridge_key(AMEK_W) || ame_input_bridge_key(AMEK_UP));
    int edge = (now && !prev_jump);
    prev_jump = now;
    return edge;
}

void input_shutdown() {
    ame_input_bridge_shutdown();
}

