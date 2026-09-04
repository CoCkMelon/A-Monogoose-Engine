/* ame-next — input: mapped actions over raw backends (input.txt).
 *
 * Gameplay reads ONLY actions (in_held/in_pressed/in_released/in_axis),
 * never raw keycodes. Raw events arrive from the compile-time backend:
 *   AME_INPUT_SDL        — SDL keyboard/mouse/gamepad via in_on_sdl_events()
 *   AME_INPUT_ASYNCINPUT — libasyncinput reader thread calls in_on_key()
 *                          with NI_KEY_* codes (SDL still does window/gamepad)
 *   web                  — browser events mapped into in_on_key/in_on_mouse_*
 * Backends only WRITE the shared atomic down-state (reader threads never run
 * gameplay). The logic thread polls once per fixed step: in_begin_step()
 * translates atomics into actions and computes frame-rate-independent edges.
 *
 * A real-time layer may read the latest atomics directly (in_key_down_raw,
 * in_mouse_pos) for sub-tick response (loop.txt) without touching sim state.
 */
#ifndef AME_INPUT_H
#define AME_INPUT_H

#include <ame/ame.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AME_ACTION_MAX 32
#define AME_KEYS_MAX   512  /* backend keycodes index atomics directly */
#define AME_BIND_PER_ACTION 4

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

/* bind a backend key code (SDL scancode / NI_KEY_* / browser code) to an
 * action; up to AME_BIND_PER_ACTION keys per action (keyboard+gamepad mix).
 * Returns true on success. */
bool in_bind_key(int action, int key);

/* --- backend write surface (called by SDL loop / asyncinput thread / web) ---
 * Only writes atomics. Never any gameplay. */
void in_on_key(int key, bool down);            /* key index must be < AME_KEYS_MAX */
void in_on_mouse_move(float x, float y);       /* window pixels */
void in_on_mouse_button(int button, bool down);/* AME_BTN_* */
void in_on_wheel(float dy);                    /* clicky notches, accumulates */

/* --- logic-thread fixed-step polling --------------------------------------- */
void in_begin_step(void);   /* read atomics, compute held/pressed/released */

bool  in_held(int action);     /* down now */
bool  in_pressed(int action);  /* went down this fixed step (exactly one) */
bool  in_released(int action); /* went up this fixed step */
float in_axis(int axis);       /* mouse x/y px, wheel delta this step */

/* real-time (sub-tick) reads of the latest atomics — safe from any thread */
bool  in_key_down_raw(int key);
void  in_mouse_pos(float *x, float *y);
bool  in_mouse_button_raw(int button);

/* helper used by backends and tests: clear all down state */
void in_clear_down(void);

/* --- asyncinput backend (only in AME_INPUT_ASYNCINPUT builds) -------------- */
#if defined(AME_INPUT_ASYNCINPUT)
/* init raw keyboard/mouse. Returns false when device nodes are unreadable
 * (permissions): the game keeps running WITHOUT raw input — surface a clear
 * message (input.txt). SDL still provides windowing + gamepad. */
bool in_asyncinput_init(void);
void in_asyncinput_shutdown(void);
bool in_asyncinput_available(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* AME_INPUT_H */
