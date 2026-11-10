#ifndef BF_INPUT_H
#define BF_INPUT_H

/* Biscuit input on the trunk input surface (input.txt): the logic thread
 * polls in_held/in_pressed once per fixed step. No SDL, no raw callback. */

/* game action ids (in_bind_key targets) */
enum {
    BF_ACT_UP = 0,
    BF_ACT_DOWN,
    BF_ACT_LEFT,
    BF_ACT_RIGHT,
    BF_ACT_BOOST,
    BF_ACT_SWITCH,
    BF_ACT_RESTART,
    BF_ACT_ADVANCE,
    BF_ACT_JUMP,
    BF_ACT_QUIT,
    BF_ACT_RELOAD, /* dev: re-read the level OBJ (levels.txt hot reload) */
    BF_ACT_COUNT
};

/* call once from app_init */
void game_input_bindings(void);
/* clear held state (restart) */
void game_input_reset(void);
/* logic thread, start of app_fixed: maps actions onto the bf_hold_* and
 * bf_request_* gameplay entry points */
void game_input_poll(void);

#endif /* BF_INPUT_H */
