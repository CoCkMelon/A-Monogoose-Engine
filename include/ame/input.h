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

/* --- device abstraction -----------------------------------------------------
 * Every input source the engine knows about is a DEVICE in one registry:
 * keyboard, mouse, touchscreen, gamepads and motion sensors. Games query
 * the registry for presence/names and read each kind through its own
 * plain-C surface below; SDL/evdev/web knowledge stays in the backends.
 * Compass/magnetometer and proximity are NOT in SDL3 - a platform shim
 * would have to feed in_on_sensor() with its own kinds later. */
enum {
    AME_DEV_KEYBOARD = 0, AME_DEV_MOUSE, AME_DEV_TOUCH, AME_DEV_GAMEPAD,
    AME_DEV_SENSOR_ACCEL, AME_DEV_SENSOR_GYRO, AME_DEV_KIND_COUNT
};
#define AME_DEV_NAME_MAX 48
#define AME_DEV_MAX      16

typedef struct {
    int  kind;                    /* AME_DEV_* */
    int  slot;                    /* per-kind index (gamepad 0.., sensor 0) */
    char name[AME_DEV_NAME_MAX];
} ame_input_device;

int  in_device_count(void);                  /* present devices */
bool in_device_info(int idx, ame_input_device *out); /* idx over present */
bool in_device_present(int kind, int slot);

/* backend registry writes (keyboard+mouse are registered by in_reset) */
void in_on_device_add(int kind, int slot, const char *name);
void in_on_device_remove(int kind, int slot);

/* --- touchscreen: first-class multi-touch points --------------------------
 * Coordinates are NORMALIZED 0..1 (window fraction), matching SDL fingers;
 * backends convert. The mouse-synthesis path still feeds clicks, so
 * pointer games keep working; taps are the touch-native consume. */
#define AME_TOUCH_MAX 8
typedef struct {
    long  id;
    float x, y;      /* normalized */
    bool  down;
} ame_input_touch;

void in_on_touch(long id, float x, float y, bool down);
int  in_touch_count(void);        /* real-time pressed points (sub-tick) */
bool in_touch_point(int i, ame_input_touch *out);
bool in_tap_consumed(float *x, float *y);     /* one release edge, consumed once */
unsigned in_tap_dropped(void);   /* taps lost to a full queue (diagnostics) */

/* --- gamepads -------------------------------------------------------------- */
#define AME_GAMEPAD_MAX 4
enum {
    AME_GP_A = 0, AME_GP_B, AME_GP_X, AME_GP_Y, AME_GP_BACK, AME_GP_START,
    AME_GP_LSHOULDER, AME_GP_RSHOULDER,
    AME_GP_DPAD_UP, AME_GP_DPAD_DOWN, AME_GP_DPAD_LEFT, AME_GP_DPAD_RIGHT,
    AME_GP_BTN_COUNT
};
enum {
    AME_GP_AXIS_LX = 0, AME_GP_AXIS_LY, AME_GP_AXIS_RX, AME_GP_AXIS_RY,
    AME_GP_AXIS_LT, AME_GP_AXIS_RT, AME_GP_AXIS_COUNT
};
void  in_on_gamepad_button(int slot, int btn, bool down);
void  in_on_gamepad_axis(int slot, int axis, float value); /* -1..1, triggers 0..1 */
bool  in_gamepad_button(int slot, int btn);   /* step snapshot */
float in_gamepad_axis(int slot, int axis);    /* step snapshot */
/* force feedback: false when the slot has no pad or no rumble motor.
 * No-op (false) on backends without rumble support. */
bool  in_gamepad_rumble(int slot, float low, float high, int duration_ms);
/* bindings fold gamepads into the action system: a button, or an axis
 * past a signed threshold (v >= thr when thr >= 0 else v <= thr). */
bool in_bind_gamepad(int action, int slot, int btn);
bool in_bind_gamepad_axis(int action, int slot, int axis, float threshold);

/* --- motion sensors (SDL units: accel m/s^2, gyro rad/s) ------------------ */
enum { AME_SENSOR_ACCEL = 0, AME_SENSOR_GYRO, AME_SENSOR_COUNT };
typedef struct {
    bool (*enable)(int sensor);   /* backend opens the hardware device */
    void (*disable)(int sensor);
    bool (*rumble)(int slot, float low, float high, int duration_ms);
} ame_input_backend;
/* NULL clears. The backend is called INTO by enabled sensors, so it may not
 * be replaced or cleared while any sensor is enabled: that call fails with
 * AME_ERR_INPUT and the old backend stays installed (disable sensors first).
 * Replacing it with nothing enabled is always fine (state-audit.txt #5). */
void in_set_backend(const ame_input_backend *ops); /* NULL clears */
bool in_sensor_enable(int sensor);  /* false: no backend or no hardware */
void in_sensor_disable(int sensor);
bool in_sensor_enabled(int sensor);
bool in_sensor_get(int sensor, float out[3], unsigned long long *ts_us);
void in_on_sensor(int sensor, const float v[3], unsigned long long ts_us);

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
