/* ame-next — input implementation (input.txt). One .c owns the state.
 * Backends write ATOMICS; the logic thread computes action edges per step. */
#include <ame/input.h>

#include <string.h>

typedef struct {
    /* shared: written by the input backend thread(s), read by logic.
     * exactly ONE backend feeds any given key/button atomic. */
    _Atomic uint8_t  key_down[AME_KEYS_MAX];
    _Atomic uint8_t  btn_down[8];
    _Atomic int32_t  mouse_x, mouse_y;   /* px * 16 fixed point */
    _Atomic int32_t  wheel_accum;        /* backend fetch-adds; logic reads deltas */

    /* logic-thread private */
    uint8_t prev_keys[AME_KEYS_MAX];
    uint8_t keys[AME_KEYS_MAX];
    uint8_t prev_btn[8];
    uint8_t btns[8];
    int32_t last_wheel_read;
    float   wheel_step;
    float   mx, my;

    /* bindings: action -> keys (static table, set at init) */
    int binds[AME_ACTION_MAX][AME_BIND_PER_ACTION];
    int bind_count[AME_ACTION_MAX];
} input_state;

static input_state S;

void in_reset(void) {
    memset(&S, 0, sizeof S);
    for (int i = 0; i < AME_KEYS_MAX; i++) {
        atomic_store_explicit(&S.key_down[i], 0, memory_order_relaxed);
    }
    for (int i = 0; i < 8; i++)
        atomic_store_explicit(&S.btn_down[i], 0, memory_order_relaxed);
    atomic_store_explicit(&S.mouse_x, 0, memory_order_relaxed);
    atomic_store_explicit(&S.mouse_y, 0, memory_order_relaxed);
    atomic_store_explicit(&S.wheel_accum, 0, memory_order_relaxed);
}

void in_clear_down(void) {
    for (int i = 0; i < AME_KEYS_MAX; i++)
        atomic_store_explicit(&S.key_down[i], 0, memory_order_relaxed);
    for (int i = 0; i < 8; i++)
        atomic_store_explicit(&S.btn_down[i], 0, memory_order_relaxed);
}

bool in_bind_key(int action, int key) {
    if (action < 0 || action >= AME_ACTION_MAX || key < 0 || key >= AME_KEYS_MAX)
        return false;
    if (S.bind_count[action] >= AME_BIND_PER_ACTION)
        return false;
    S.binds[action][S.bind_count[action]++] = key;
    return true;
}

void in_on_key(int key, bool down) {
    if (key < 0 || key >= AME_KEYS_MAX)
        return;
    atomic_store_explicit(&S.key_down[key], down ? 1 : 0, memory_order_release);
}

void in_on_mouse_move(float x, float y) {
    atomic_store_explicit(&S.mouse_x, (int32_t)(x * 16.0f), memory_order_release);
    atomic_store_explicit(&S.mouse_y, (int32_t)(y * 16.0f), memory_order_release);
}

void in_on_mouse_button(int button, bool down) {
    if (button < 0 || button >= 8)
        return;
    atomic_store_explicit(&S.btn_down[button], down ? 1 : 0, memory_order_release);
}

void in_on_wheel(float dy) {
    atomic_fetch_add_explicit(&S.wheel_accum, (int32_t)(dy * 16.0f),
                              memory_order_release);
}

bool in_key_down_raw(int key) {
    if (key < 0 || key >= AME_KEYS_MAX)
        return false;
    return atomic_load_explicit(&S.key_down[key], memory_order_acquire) != 0;
}

void in_mouse_pos(float *x, float *y) {
    if (x) *x = (float)atomic_load_explicit(&S.mouse_x, memory_order_acquire) / 16.0f;
    if (y) *y = (float)atomic_load_explicit(&S.mouse_y, memory_order_acquire) / 16.0f;
}

bool in_mouse_button_raw(int button) {
    if (button < 0 || button >= 8)
        return false;
    return atomic_load_explicit(&S.btn_down[button], memory_order_acquire) != 0;
}

void in_begin_step(void) {
    memcpy(S.prev_keys, S.keys, sizeof S.keys);
    memcpy(S.prev_btn, S.btns, sizeof S.btns);
    for (int i = 0; i < AME_KEYS_MAX; i++)
        S.keys[i] = atomic_load_explicit(&S.key_down[i], memory_order_acquire);
    for (int i = 0; i < 8; i++)
        S.btns[i] = atomic_load_explicit(&S.btn_down[i], memory_order_acquire);
    int32_t w = atomic_load_explicit(&S.wheel_accum, memory_order_acquire);
    S.wheel_step = (float)(w - S.last_wheel_read) / 16.0f;
    S.last_wheel_read = w;
    in_mouse_pos(&S.mx, &S.my);
}

static bool any_bound_down(int action, const uint8_t *table) {
    if (action < 0 || action >= AME_ACTION_MAX)
        return false;
    for (int i = 0; i < S.bind_count[action]; i++) {
        int k = S.binds[action][i];
        if (k >= 0 && k < AME_KEYS_MAX && table[k])
            return true;
    }
    return false;
}

static bool any_bound_transition(int action, const uint8_t *now,
                                 const uint8_t *prev, bool wanted) {
    if (action < 0 || action >= AME_ACTION_MAX)
        return false;
    for (int i = 0; i < S.bind_count[action]; i++) {
        int k = S.binds[action][i];
        if (k < 0 || k >= AME_KEYS_MAX)
            continue;
        if (now[k] == wanted && prev[k] != wanted)
            return true;
    }
    return false;
}

bool in_held(int action)     { return any_bound_down(action, S.keys); }
bool in_pressed(int action)  { return any_bound_transition(action, S.keys, S.prev_keys, 1); }
bool in_released(int action) { return any_bound_transition(action, S.keys, S.prev_keys, 0); }

float in_axis(int axis) {
    switch (axis) {
    case AME_AXIS_MOUSE_X:  return S.mx;
    case AME_AXIS_MOUSE_Y:  return S.my;
    case AME_AXIS_WHEEL:    return S.wheel_step;
    default:                return 0.0f;
    }
}
