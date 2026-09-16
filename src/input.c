/* ame-next — input implementation (input.txt). One .c owns the state.
 * Backends write ATOMICS; the logic thread computes action edges per step. */
#include <ame/input.h>

#include <string.h>

#define IN_CLICK_CAP 32u
#define IN_CLICK_MASK (IN_CLICK_CAP - 1u)

typedef struct {
    /* shared: written by the input backend thread(s), read by logic.
     * exactly ONE backend feeds any given key/button atomic. */
    _Atomic uint8_t  key_down[AME_KEYS_MAX];
    _Atomic uint8_t  btn_down[AME_BUTTON_MAX];
    _Atomic int32_t  mouse_x, mouse_y;   /* px * 16 fixed point */
    _Atomic int32_t  wheel_accum;        /* backend fetch-adds */

    /* SPSC click queue: platform/main thread produces, logic consumes. */
    _Atomic uint32_t click_head, click_tail;
    _Atomic int32_t click_x[IN_CLICK_CAP], click_y[IN_CLICK_CAP];
    _Atomic uint32_t click_dropped;

    /* logic-thread private */
    uint8_t prev_keys[AME_KEYS_MAX];
    uint8_t keys[AME_KEYS_MAX];
    uint8_t prev_btn[AME_BUTTON_MAX];
    uint8_t btns[AME_BUTTON_MAX];
    int32_t last_wheel_read;
    float   wheel_step;
    float   mx, my;

    /* bindings: action -> keys and buttons */
    int key_binds[AME_ACTION_MAX][AME_BIND_PER_ACTION];
    int key_bind_count[AME_ACTION_MAX];
    int button_binds[AME_ACTION_MAX][AME_BIND_PER_ACTION];
    int button_bind_count[AME_ACTION_MAX];
    int bind_total[AME_ACTION_MAX];

    /* deduped list of every bound key: in_begin_step polls ONLY these */
    int bound_keys[AME_ACTION_MAX * AME_BIND_PER_ACTION];
    int bound_count;
} input_state;

static input_state S;

void in_reset(void) {
    memset(&S, 0, sizeof S);
    for (int i = 0; i < AME_KEYS_MAX; i++)
        atomic_store_explicit(&S.key_down[i], 0, memory_order_relaxed);
    for (int i = 0; i < AME_BUTTON_MAX; i++)
        atomic_store_explicit(&S.btn_down[i], 0, memory_order_relaxed);
    atomic_store_explicit(&S.mouse_x, 0, memory_order_relaxed);
    atomic_store_explicit(&S.mouse_y, 0, memory_order_relaxed);
    atomic_store_explicit(&S.wheel_accum, 0, memory_order_relaxed);
    atomic_store_explicit(&S.click_head, 0, memory_order_relaxed);
    atomic_store_explicit(&S.click_tail, 0, memory_order_relaxed);
    atomic_store_explicit(&S.click_dropped, 0, memory_order_relaxed);
}

void in_clear_down(void) {
    for (int i = 0; i < AME_KEYS_MAX; i++)
        atomic_store_explicit(&S.key_down[i], 0, memory_order_relaxed);
    for (int i = 0; i < AME_BUTTON_MAX; i++)
        atomic_store_explicit(&S.btn_down[i], 0, memory_order_relaxed);
    atomic_store_explicit(&S.click_tail,
                          atomic_load_explicit(&S.click_head,
                                               memory_order_acquire),
                          memory_order_release);
}

bool in_bind_key(int action, int key) {
    if (action < 0 || action >= AME_ACTION_MAX) {
        ame_set_errorf(AME_ERR_INPUT, "action id %d is out of range", action);
        return false;
    }
    if (key < 0 || key >= AME_KEYS_MAX) {
        ame_set_errorf(AME_ERR_INPUT, "key id %d is out of range", key);
        return false;
    }
    if (S.bind_total[action] >= AME_BIND_PER_ACTION
        || S.key_bind_count[action] >= AME_BIND_PER_ACTION) {
        ame_set_errorf(AME_ERR_INPUT, "action %d has no keyboard binding slots", action);
        return false;
    }
    for (int i = 0; i < S.key_bind_count[action]; i++)
        if (S.key_binds[action][i] == key)
            return true;
    S.key_binds[action][S.key_bind_count[action]++] = key;
    S.bind_total[action]++;

    /* keep the poll list deduped (two actions may share one key) */
    for (int i = 0; i < S.bound_count; i++)
        if (S.bound_keys[i] == key)
            return true;
    uint8_t live = atomic_load_explicit(&S.key_down[key], memory_order_acquire);
    S.keys[key] = live;
    S.prev_keys[key] = live;
    S.bound_keys[S.bound_count++] = key;
    return true;
}

bool in_bind_button(int action, int button) {
    if (action < 0 || action >= AME_ACTION_MAX) {
        ame_set_errorf(AME_ERR_INPUT, "action id %d is out of range", action);
        return false;
    }
    if (button < 0 || button >= AME_BUTTON_MAX) {
        ame_set_errorf(AME_ERR_INPUT, "button id %d is out of range", button);
        return false;
    }
    if (S.bind_total[action] >= AME_BIND_PER_ACTION
        || S.button_bind_count[action] >= AME_BIND_PER_ACTION) {
        ame_set_errorf(AME_ERR_INPUT, "action %d has no button binding slots", action);
        return false;
    }
    for (int i = 0; i < S.button_bind_count[action]; i++)
        if (S.button_binds[action][i] == button)
            return true;
    S.button_binds[action][S.button_bind_count[action]++] = button;
    S.bind_total[action]++;
    /* Match in_bind_key: rebinding a held button must not invent a
     * pressed edge on the next fixed step. */
    uint8_t live = atomic_load_explicit(&S.btn_down[button], memory_order_acquire);
    S.btns[button] = live;
    S.prev_btn[button] = live;
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

static void queue_click(float x, float y) {
    uint32_t head = atomic_load_explicit(&S.click_head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&S.click_tail, memory_order_acquire);
    if (head - tail >= IN_CLICK_CAP) {
        /* Keep the queue bounded and deterministic. Dropping the newest
         * click is preferable to overwriting a click the logic thread is
         * about to consume. The counter is available for diagnostics. */
        atomic_fetch_add_explicit(&S.click_dropped, 1, memory_order_relaxed);
        return;
    }
    uint32_t slot = head & IN_CLICK_MASK;
    atomic_store_explicit(&S.click_x[slot], (int32_t)(x * 16.0f),
                          memory_order_relaxed);
    atomic_store_explicit(&S.click_y[slot], (int32_t)(y * 16.0f),
                          memory_order_relaxed);
    atomic_store_explicit(&S.click_head, head + 1u, memory_order_release);
}

void in_on_mouse_button_at(int button, bool down, float x, float y) {
    if (button < 0 || button >= AME_BUTTON_MAX)
        return;
    /* SDL's button event carries the most accurate point; publish it even
     * when no motion event preceded the click. */
    in_on_mouse_move(x, y);
    atomic_store_explicit(&S.btn_down[button], down ? 1 : 0, memory_order_release);
    if (down && button == AME_BTN_LEFT)
        queue_click(x, y);
}

void in_on_mouse_button(int button, bool down) {
    int32_t ix = atomic_load_explicit(&S.mouse_x, memory_order_acquire);
    int32_t iy = atomic_load_explicit(&S.mouse_y, memory_order_acquire);
    in_on_mouse_button_at(button, down, (float)ix / 16.0f,
                          (float)iy / 16.0f);
}

void in_on_wheel(float dy) {
    atomic_fetch_add_explicit(&S.wheel_accum, (int32_t)(dy * 16.0f),
                              memory_order_release);
}

bool in_click_consumed(float *x, float *y) {
    uint32_t tail = atomic_load_explicit(&S.click_tail, memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&S.click_head, memory_order_acquire);
    if (tail == head)
        return false;
    uint32_t slot = tail & IN_CLICK_MASK;
    int32_t ix = atomic_load_explicit(&S.click_x[slot], memory_order_relaxed);
    int32_t iy = atomic_load_explicit(&S.click_y[slot], memory_order_relaxed);
    atomic_store_explicit(&S.click_tail, tail + 1u, memory_order_release);
    if (x)
        *x = (float)ix / 16.0f;
    if (y)
        *y = (float)iy / 16.0f;
    return true;
}

bool in_key_down_raw(int key) {
    if (key < 0 || key >= AME_KEYS_MAX)
        return false;
    return atomic_load_explicit(&S.key_down[key], memory_order_acquire) != 0;
}

void in_mouse_pos(float *x, float *y) {
    if (x)
        *x = (float)atomic_load_explicit(&S.mouse_x, memory_order_acquire) / 16.0f;
    if (y)
        *y = (float)atomic_load_explicit(&S.mouse_y, memory_order_acquire) / 16.0f;
}

bool in_mouse_button_raw(int button) {
    if (button < 0 || button >= AME_BUTTON_MAX)
        return false;
    return atomic_load_explicit(&S.btn_down[button], memory_order_acquire) != 0;
}

void in_begin_step(void) {
    for (int i = 0; i < S.bound_count; i++) {
        int k = S.bound_keys[i];
        S.prev_keys[k] = S.keys[k];
        S.keys[k] = atomic_load_explicit(&S.key_down[k], memory_order_acquire);
    }
    memcpy(S.prev_btn, S.btns, sizeof S.btns);
    for (int i = 0; i < AME_BUTTON_MAX; i++)
        S.btns[i] = atomic_load_explicit(&S.btn_down[i], memory_order_acquire);
    int32_t w = atomic_load_explicit(&S.wheel_accum, memory_order_acquire);
    S.wheel_step = (float)(w - S.last_wheel_read) / 16.0f;
    S.last_wheel_read = w;
    in_mouse_pos(&S.mx, &S.my);
}

static bool any_key_down(int action, const uint8_t *table) {
    if (action < 0 || action >= AME_ACTION_MAX)
        return false;
    for (int i = 0; i < S.key_bind_count[action]; i++) {
        int k = S.key_binds[action][i];
        if (table[k])
            return true;
    }
    return false;
}

static bool any_button_down(int action, const uint8_t *table) {
    if (action < 0 || action >= AME_ACTION_MAX)
        return false;
    for (int i = 0; i < S.button_bind_count[action]; i++) {
        int b = S.button_binds[action][i];
        if (table[b])
            return true;
    }
    return false;
}

static bool any_transition(int action, const uint8_t *now,
                           const uint8_t *prev, bool wanted) {
    if (action < 0 || action >= AME_ACTION_MAX)
        return false;
    for (int i = 0; i < S.key_bind_count[action]; i++) {
        int k = S.key_binds[action][i];
        if (now[k] == wanted && prev[k] != wanted)
            return true;
    }
    for (int i = 0; i < S.button_bind_count[action]; i++) {
        int b = S.button_binds[action][i];
        if (now[b] == wanted && prev[b] != wanted)
            return true;
    }
    return false;
}

bool in_held(int action) {
    return any_key_down(action, S.keys) || any_button_down(action, S.btns);
}
bool in_btn_held(int button) {
    return button >= 0 && button < AME_BUTTON_MAX && S.btns[button] != 0;
}
bool in_pressed(int action) {
    return any_transition(action, S.keys, S.prev_keys, true)
        || any_transition(action, S.btns, S.prev_btn, true);
}
bool in_released(int action) {
    return any_transition(action, S.keys, S.prev_keys, false)
        || any_transition(action, S.btns, S.prev_btn, false);
}

float in_axis(int axis) {
    switch (axis) {
    case AME_AXIS_MOUSE_X: return S.mx;
    case AME_AXIS_MOUSE_Y: return S.my;
    case AME_AXIS_WHEEL:   return S.wheel_step;
    default:               return 0.0f;
    }
}
