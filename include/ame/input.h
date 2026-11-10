/* ame-next — input: mapped actions over raw backends (input.txt).
 *
 * Gameplay reads ONLY actions (in_held/in_pressed/in_released/in_axis),
 * never operating-system event structs or key names. Raw events arrive from
 * the compile-time backend and only write the shared atomic state. The logic
 * thread polls once per fixed step and computes deterministic edges.
 *
 * `AME_KEY_*` values are the engine's canonical keyboard ids (the USB/SDL
 * scancode values). The asyncinput backend maps evdev ids to the same values,
 * so bindings are portable between native backends and web.
 */
#ifndef AME_INPUT_H
#define AME_INPUT_H

#include <ame/ame.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AME_ACTION_MAX 32
#define AME_KEYS_MAX   512
#define AME_BIND_PER_ACTION 4
#define AME_BUTTON_MAX 8

/* Canonical keyboard ids.  Keeping these in the engine header means a game
 * can express its mapping without including SDL_scancode.h. */
enum {
    AME_KEY_UNKNOWN = 0,
    AME_KEY_A = 4, AME_KEY_B = 5, AME_KEY_C = 6, AME_KEY_D = 7,
    AME_KEY_E = 8, AME_KEY_F = 9, AME_KEY_G = 10, AME_KEY_H = 11,
    AME_KEY_I = 12, AME_KEY_J = 13, AME_KEY_K = 14, AME_KEY_L = 15,
    AME_KEY_M = 16, AME_KEY_N = 17, AME_KEY_O = 18, AME_KEY_P = 19,
    AME_KEY_Q = 20, AME_KEY_R = 21, AME_KEY_S = 22, AME_KEY_T = 23,
    AME_KEY_U = 24, AME_KEY_V = 25, AME_KEY_W = 26, AME_KEY_X = 27,
    AME_KEY_Y = 28, AME_KEY_Z = 29,
    AME_KEY_1 = 30, AME_KEY_2 = 31, AME_KEY_3 = 32, AME_KEY_4 = 33,
    AME_KEY_5 = 34, AME_KEY_6 = 35, AME_KEY_7 = 36, AME_KEY_8 = 37,
    AME_KEY_9 = 38, AME_KEY_0 = 39,
    AME_KEY_RETURN = 40, AME_KEY_ESCAPE = 41, AME_KEY_BACKSPACE = 42,
    AME_KEY_TAB = 43, AME_KEY_SPACE = 44, AME_KEY_MINUS = 45,
    AME_KEY_EQUALS = 46, AME_KEY_LEFTBRACKET = 47,
    AME_KEY_RIGHTBRACKET = 48, AME_KEY_BACKSLASH = 49,
    AME_KEY_SEMICOLON = 51, AME_KEY_APOSTROPHE = 52, AME_KEY_GRAVE = 53,
    AME_KEY_COMMA = 54, AME_KEY_PERIOD = 55, AME_KEY_SLASH = 56,
    /* function keys (SDL scancodes 58..69): dev-only conveniences such
     * as hot reload bind here; games never need them to play */
    AME_KEY_F1 = 58, AME_KEY_F2 = 59, AME_KEY_F3 = 60, AME_KEY_F4 = 61,
    AME_KEY_F5 = 62, AME_KEY_F6 = 63, AME_KEY_F7 = 64, AME_KEY_F8 = 65,
    AME_KEY_F9 = 66, AME_KEY_F10 = 67, AME_KEY_F11 = 68, AME_KEY_F12 = 69,
    AME_KEY_HOME = 74, AME_KEY_END = 77,
    AME_KEY_RIGHT = 79, AME_KEY_LEFT = 80, AME_KEY_DOWN = 81,
    AME_KEY_UP = 82, AME_KEY_KP_ENTER = 88,
    AME_KEY_LCTRL = 224, AME_KEY_LSHIFT = 225, AME_KEY_LALT = 226,
    AME_KEY_RCTRL = 228, AME_KEY_RSHIFT = 229, AME_KEY_RALT = 230
};

/* base action ids; games may use AME_ACT_USER..AME_ACTION_MAX-1 */
enum {
    AME_ACT_LEFT = 0, AME_ACT_RIGHT, AME_ACT_UP, AME_ACT_DOWN,
    AME_ACT_JUMP, AME_ACT_BOOST, AME_ACT_SWITCH, AME_ACT_FIRE,
    AME_ACT_CONFIRM, AME_ACT_CANCEL, AME_ACT_PAUSE, AME_ACT_RESTART,
    AME_ACT_USER
};

enum { AME_BTN_LEFT = 0, AME_BTN_MIDDLE = 1, AME_BTN_RIGHT = 2 };
enum { AME_AXIS_MOUSE_X = 0, AME_AXIS_MOUSE_Y, AME_AXIS_WHEEL, AME_AXIS_COUNT };

void in_reset(void);

/* Bind keyboard ids and mouse buttons to one action. Each action has one
 * shared capacity across both kinds of binding. Returns true on success. */
bool in_bind_key(int action, int key);
bool in_bind_button(int action, int button);

/* --- backend write surface (called by SDL loop / asyncinput thread / web) ---
 * Only writes atomics. Never any gameplay. */
void in_on_key(int key, bool down);
void in_on_mouse_move(float x, float y);       /* window pixels */
void in_on_mouse_button(int button, bool down);/* uses latest mouse position */
void in_on_mouse_button_at(int button, bool down, float x, float y);
void in_on_wheel(float dy);                    /* clicky notches, accumulates */

/* --- logic-thread fixed-step polling --------------------------------------- */
void in_begin_step(void);   /* read atomics, compute held/pressed/released */

bool  in_held(int action);     /* down now */
bool  in_btn_held(int button); /* polled button state after in_begin_step */
bool  in_pressed(int action);  /* went down this fixed step (exactly one) */
bool  in_released(int action); /* went up this fixed step */
float in_axis(int axis);       /* mouse x/y px, wheel delta this step */

/* A left-button click is captured with its event-time position and consumed
 * exactly once by the logic thread. This replaces per-game atomic flag and
 * fixed-point-coordinate protocols. It is independent of button held edges. */
bool in_click_consumed(float *x, float *y);

/* real-time (sub-tick) reads of the latest atomics — safe from any thread */
bool  in_key_down_raw(int key);
void  in_mouse_pos(float *x, float *y);
bool  in_mouse_button_raw(int button);

/* helper used by backends and tests: clear all down state */
void in_clear_down(void);

/* --- asyncinput backend (only in AME_INPUT_ASYNCINPUT builds) -------------- */
#if defined(AME_INPUT_ASYNCINPUT)
bool in_asyncinput_init(void);
void in_asyncinput_shutdown(void);
bool in_asyncinput_available(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* AME_INPUT_H */
