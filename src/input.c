/* ame-next — input implementation (input.txt). One .c owns the state.
 * Backends write ATOMICS; the logic thread computes action edges per step. */
#include <ame/input.h>

#include <string.h>

#define IN_CLICK_CAP 32u
#define IN_CLICK_MASK (IN_CLICK_CAP - 1u)

/* One device-registry row, published through the project's existing seqlock
 * (AME_SNAP_DEFINE / AME_SNAP_FENCE in ame/ame.h, the same idiom
 * tests/test_snap.c exercises).
 *
 * Why a seqlock and not a plain release/acquire pair: a device row is THREE
 * values - kind, slot and a 48-byte name - and only the name is multi-byte.
 * A release store on dev_present orders the writes that precede it, but it
 * says nothing about a row that is ALREADY published. registry_add reuses a
 * slot whose dev_present is already 1 whenever the same (kind, slot) is
 * announced again - a hotplug re-announce, a name change, or remove-then-add
 * churn - and on that path the name is rewritten in place while the trailing
 * release store is a no-op, so a concurrent in_device_info() memcpy can read
 * half the old name and half the new one. Bounded by AME_DEV_NAME_MAX, so
 * never out of bounds, but torn, and TSan reports it (issue #12).
 *
 * Publishing the whole row as one snapshot makes tearing impossible: the
 * reader copies to a temporary and validates the version counter, so it
 * either sees the entire previous row or the entire new one, never a mix.
 * The read path stays LOCK-FREE - that is the point of this module - and the
 * retry loop is bounded at 64 attempts by the macro. */
AME_SNAP_DEFINE(ame_input_device)

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

    /* gamepads: backend writes atomics, logic snapshots per step */
    _Atomic uint8_t gp_btn[AME_GAMEPAD_MAX][AME_GP_BTN_COUNT];
    _Atomic int32_t gp_axis[AME_GAMEPAD_MAX][AME_GP_AXIS_COUNT]; /* *1024 */
    uint8_t gp_btn_s[AME_GAMEPAD_MAX][AME_GP_BTN_COUNT];
    uint8_t gp_btn_prev[AME_GAMEPAD_MAX][AME_GP_BTN_COUNT];
    float   gp_axis_s[AME_GAMEPAD_MAX][AME_GP_AXIS_COUNT];
    float   gp_axis_prev[AME_GAMEPAD_MAX][AME_GP_AXIS_COUNT];
    struct { int slot, btn; }            gp_binds[AME_ACTION_MAX][AME_BIND_PER_ACTION];
    int                                  gp_bind_count[AME_ACTION_MAX];
    struct { int slot, axis; int32_t thr; } ga_binds[AME_ACTION_MAX][AME_BIND_PER_ACTION];
    int                                  ga_bind_count[AME_ACTION_MAX];

    /* touchscreen slots (-1 id = free) + SPSC tap queue (click-shaped) */
    _Atomic int32_t  t_id[AME_TOUCH_MAX];
    _Atomic int32_t  t_x[AME_TOUCH_MAX], t_y[AME_TOUCH_MAX]; /* norm *4096 */
    _Atomic uint8_t  t_down[AME_TOUCH_MAX];
    _Atomic uint32_t tap_head, tap_tail;
    _Atomic int32_t  tap_x[IN_CLICK_CAP], tap_y[IN_CLICK_CAP];
    _Atomic uint32_t tap_dropped;

    /* motion sensors: value *1024 + timestamp us */
    _Atomic int32_t  sens_v[AME_SENSOR_COUNT][3];
    _Atomic uint64_t sens_ts[AME_SENSOR_COUNT];
    _Atomic uint8_t  sens_on[AME_SENSOR_COUNT];

    /* device registry: backend writes; the present flag publishes the row.
     * The row itself (kind + slot + name) goes through the seqlock above so
     * an update-in-place cannot be read torn - see ame_input_device_snap. */
    _Atomic uint8_t        dev_present[AME_DEV_MAX];
    ame_input_device_snap  dev_row[AME_DEV_MAX];
} input_state;

static const ame_input_backend *g_backend;

static void registry_add(int kind, int slot, const char *name);
static bool registry_row_is(int i, int kind, int slot);

static input_state S;

void in_reset(void) {
    memset(&S, 0, sizeof S);
    /* The memset above already zeroes each seqlock gate (version 0 = "not
     * mid-publish"), but initializing through the macro is what both other
     * AME_SNAP_DEFINE users do (tests/test_snap.c, tests/test_math.c), and it
     * keeps the invariant stated where the rows are reset rather than implied
     * by a byte pattern. */
    for (int i = 0; i < AME_DEV_MAX; i++)
        ame_input_device_snap_init(&S.dev_row[i]);
    for (int i = 0; i < AME_TOUCH_MAX; i++)
        atomic_store_explicit(&S.t_id[i], -1, memory_order_relaxed);
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
    atomic_store_explicit(&S.tap_head, 0, memory_order_relaxed);
    atomic_store_explicit(&S.tap_tail, 0, memory_order_relaxed);
    atomic_store_explicit(&S.tap_dropped, 0, memory_order_relaxed);
    /* the two always-there devices head the registry */
    registry_add(AME_DEV_KEYBOARD, 0, "keyboard");
    registry_add(AME_DEV_MOUSE, 0, "mouse");
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
    /* the contract is ALL down state: gamepad buttons, touch points and
     * queued taps included, or a restart on a touch/pad device keeps
     * ghosts (bug found in the 2026-11-14 hunt) */
    for (int g = 0; g < AME_GAMEPAD_MAX; g++)
        for (int b = 0; b < AME_GP_BTN_COUNT; b++)
            atomic_store_explicit(&S.gp_btn[g][b], 0, memory_order_relaxed);
    for (int i = 0; i < AME_TOUCH_MAX; i++) {
        atomic_store_explicit(&S.t_down[i], 0, memory_order_relaxed);
        atomic_store_explicit(&S.t_id[i], -1, memory_order_relaxed);
    }
    atomic_store_explicit(&S.tap_tail,
                          atomic_load_explicit(&S.tap_head,
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
    for (int g = 0; g < AME_GAMEPAD_MAX; g++) {
        memcpy(S.gp_btn_prev[g], S.gp_btn_s[g], sizeof S.gp_btn_s[g]);
        memcpy(S.gp_axis_prev[g], S.gp_axis_s[g], sizeof S.gp_axis_s[g]);
        for (int b = 0; b < AME_GP_BTN_COUNT; b++)
            S.gp_btn_s[g][b] =
                atomic_load_explicit(&S.gp_btn[g][b], memory_order_acquire);
        for (int a = 0; a < AME_GP_AXIS_COUNT; a++)
            S.gp_axis_s[g][a] = (float)atomic_load_explicit(&S.gp_axis[g][a],
                                    memory_order_acquire) / 1024.0f;
    }
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

static bool gp_axis_held(int action,
                         const float axes[AME_GAMEPAD_MAX][AME_GP_AXIS_COUNT]) {
    if (action < 0 || action >= AME_ACTION_MAX)
        return false;
    for (int i = 0; i < S.ga_bind_count[action]; i++) {
        float v = axes[S.ga_binds[action][i].slot]
                      [S.ga_binds[action][i].axis];
        int32_t thr = S.ga_binds[action][i].thr;
        if (thr >= 0 ? v >= (float)thr / 1024.0f : v <= (float)thr / 1024.0f)
            return true;
    }
    return false;
}

static bool any_gp_down(int action) {
    if (action < 0 || action >= AME_ACTION_MAX)
        return false;
    for (int i = 0; i < S.gp_bind_count[action]; i++)
        if (S.gp_btn_s[S.gp_binds[action][i].slot]
                      [S.gp_binds[action][i].btn])
            return true;
    return gp_axis_held(action, S.gp_axis_s);
}

static bool any_gp_transition(int action, bool wanted) {
    if (action < 0 || action >= AME_ACTION_MAX)
        return false;
    for (int i = 0; i < S.gp_bind_count[action]; i++) {
        int g = S.gp_binds[action][i].slot, b = S.gp_binds[action][i].btn;
        if ((S.gp_btn_s[g][b] != 0) == wanted
            && (S.gp_btn_prev[g][b] != 0) != wanted)
            return true;
    }
    bool now = gp_axis_held(action, S.gp_axis_s);
    bool prev = gp_axis_held(action, S.gp_axis_prev);
    return now == wanted && prev != wanted;
}

bool in_held(int action) {
    return any_key_down(action, S.keys) || any_button_down(action, S.btns)
        || any_gp_down(action);
}
bool in_btn_held(int button) {
    return button >= 0 && button < AME_BUTTON_MAX && S.btns[button] != 0;
}
bool in_pressed(int action) {
    return any_transition(action, S.keys, S.prev_keys, true)
        || any_transition(action, S.btns, S.prev_btn, true)
        || any_gp_transition(action, true);
}
bool in_released(int action) {
    return any_transition(action, S.keys, S.prev_keys, false)
        || any_transition(action, S.btns, S.prev_btn, false)
        || any_gp_transition(action, false);
}

float in_axis(int axis) {
    switch (axis) {
    case AME_AXIS_MOUSE_X: return S.mx;
    case AME_AXIS_MOUSE_Y: return S.my;
    case AME_AXIS_WHEEL:   return S.wheel_step;
    default:               return 0.0f;
    }
}

/* --- device registry -------------------------------------------------------- */

/* The kind/slot pair a row holds, read out of its published snapshot. Used
 * for the lookup scans, which need to match a row without copying its name.
 * A row that has never been published reads back zeroed (kind 0 is
 * AME_DEV_KEYBOARD), which is why every scan gates on dev_present first. */
static bool registry_row_is(int i, int kind, int slot) {
    ame_input_device row;
    if (!ame_input_device_latest_copy(&S.dev_row[i], &row))
        return false; /* every attempt overlapped a publish: no match */
    return row.kind == kind && row.slot == slot;
}

static void registry_add(int kind, int slot, const char *name) {
    if (kind < 0 || kind >= AME_DEV_KIND_COUNT)
        return;
    int free_slot = -1;
    for (int i = 0; i < AME_DEV_MAX; i++) {
        if (atomic_load_explicit(&S.dev_present[i], memory_order_relaxed)
            && registry_row_is(i, kind, slot)) {
            free_slot = i; /* update in place */
            break;
        }
        if (free_slot < 0
            && !atomic_load_explicit(&S.dev_present[i], memory_order_relaxed))
            free_slot = i;
    }
    if (free_slot < 0) {
        ame_set_errorf(AME_ERR_INPUT, "device registry full (%d)", AME_DEV_MAX);
        return;
    }
    /* Build the whole row locally, then publish it in one step. Writing
     * straight into the shared row is what made the update-in-place path
     * tear (issue #12). */
    ame_input_device row = { .kind = kind, .slot = slot, .name = { 0 } };
    size_t n = name ? strlen(name) : 0;
    if (n >= AME_DEV_NAME_MAX)
        n = AME_DEV_NAME_MAX - 1;
    memcpy(row.name, name ? name : "", n);
    row.name[n] = '\0';
    ame_input_device_publish(&S.dev_row[free_slot], &row);
    /* present stays the publication flag for the LOOKUP scans and for
     * in_device_count/in_device_present, so removal is unchanged: clearing
     * it retires the row without touching the snapshot. */
    atomic_store_explicit(&S.dev_present[free_slot], 1, memory_order_release);
}

void in_on_device_add(int kind, int slot, const char *name) {
    registry_add(kind, slot, name);
}

void in_on_device_remove(int kind, int slot) {
    for (int i = 0; i < AME_DEV_MAX; i++)
        if (atomic_load_explicit(&S.dev_present[i], memory_order_relaxed)
            && registry_row_is(i, kind, slot))
            atomic_store_explicit(&S.dev_present[i], 0, memory_order_release);
    /* Hunt 2026-11-14: a yanked gamepad must not leave ghost state --
     * a stick pulled while tilted (or a held button) would drive binds
     * forever because nothing ever writes that slot again. Zero the
     * ATOMICS only; the next in_step() re-snapshots zeros on the logic
     * thread and fires one released edge per stuck button. */
    if (kind == AME_DEV_GAMEPAD && slot >= 0 && slot < AME_GAMEPAD_MAX) {
        for (int b = 0; b < AME_GP_BTN_COUNT; b++)
            atomic_store_explicit(&S.gp_btn[slot][b], 0,
                                  memory_order_release);
        for (int a = 0; a < AME_GP_AXIS_COUNT; a++)
            atomic_store_explicit(&S.gp_axis[slot][a], 0,
                                  memory_order_release);
    }
}

int in_device_count(void) {
    int n = 0;
    for (int i = 0; i < AME_DEV_MAX; i++)
        n += atomic_load_explicit(&S.dev_present[i], memory_order_acquire) != 0;
    return n;
}

bool in_device_info(int idx, ame_input_device *out) {
    if (!out || idx < 0)
        return false;
    for (int i = 0; i < AME_DEV_MAX; i++) {
        if (!atomic_load_explicit(&S.dev_present[i], memory_order_acquire))
            continue;
        if (idx-- > 0)
            continue;
        /* One validated snapshot copy: either the whole previous row or the
         * whole new one. *out is left unchanged if every attempt overlapped a
         * publish, so the caller never sees a half-written row. */
        if (!ame_input_device_latest_copy(&S.dev_row[i], out))
            return false;
        out->name[AME_DEV_NAME_MAX - 1] = '\0';
        return true;
    }
    return false;
}

bool in_device_present(int kind, int slot) {
    for (int i = 0; i < AME_DEV_MAX; i++)
        if (atomic_load_explicit(&S.dev_present[i], memory_order_acquire)
            && registry_row_is(i, kind, slot))
            return true;
    return false;
}

/* --- touchscreen ------------------------------------------------------------ */

static void queue_tap(float x, float y) {
    uint32_t head = atomic_load_explicit(&S.tap_head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&S.tap_tail, memory_order_acquire);
    if (head - tail >= IN_CLICK_CAP) {
        /* bounded queue, newest tap loses - counted, like clicks */
        atomic_fetch_add_explicit(&S.tap_dropped, 1u, memory_order_relaxed);
        return;
    }
    uint32_t slot = head & IN_CLICK_MASK;
    atomic_store_explicit(&S.tap_x[slot], (int32_t)(x * 4096.0f),
                          memory_order_relaxed);
    atomic_store_explicit(&S.tap_y[slot], (int32_t)(y * 4096.0f),
                          memory_order_relaxed);
    atomic_store_explicit(&S.tap_head, head + 1u, memory_order_release);
}

void in_on_touch(long id, float x, float y, bool down) {
    int slot = -1;
    for (int i = 0; i < AME_TOUCH_MAX; i++)
        if (atomic_load_explicit(&S.t_id[i], memory_order_relaxed) == (int32_t)id) {
            slot = i;
            break;
        }
    if (slot < 0 && !down)
        return;
    if (slot < 0) {
        for (int i = 0; i < AME_TOUCH_MAX; i++)
            if (atomic_load_explicit(&S.t_id[i], memory_order_relaxed) == -1) {
                slot = i;
                break;
            }
        if (slot < 0)
            return;
        atomic_store_explicit(&S.t_id[slot], (int32_t)id, memory_order_relaxed);
    }
    atomic_store_explicit(&S.t_x[slot], (int32_t)(x * 4096.0f),
                          memory_order_relaxed);
    atomic_store_explicit(&S.t_y[slot], (int32_t)(y * 4096.0f),
                          memory_order_relaxed);
    bool was = atomic_load_explicit(&S.t_down[slot], memory_order_relaxed) != 0;
    atomic_store_explicit(&S.t_down[slot], down ? 1 : 0, memory_order_release);
    if (was && !down) {
        queue_tap(x, y);
        atomic_store_explicit(&S.t_id[slot], -1, memory_order_release);
    }
}

int in_touch_count(void) {
    int n = 0;
    for (int i = 0; i < AME_TOUCH_MAX; i++)
        n += atomic_load_explicit(&S.t_down[i], memory_order_acquire) != 0;
    return n;
}

bool in_touch_point(int i, ame_input_touch *out) {
    if (!out || i < 0)
        return false;
    for (int s = 0; s < AME_TOUCH_MAX; s++) {
        if (!atomic_load_explicit(&S.t_down[s], memory_order_acquire))
            continue;
        if (i-- > 0)
            continue;
        out->id = atomic_load_explicit(&S.t_id[s], memory_order_acquire);
        out->x = (float)atomic_load_explicit(&S.t_x[s], memory_order_acquire)
                 / 4096.0f;
        out->y = (float)atomic_load_explicit(&S.t_y[s], memory_order_acquire)
                 / 4096.0f;
        out->down = true;
        return true;
    }
    return false;
}

unsigned in_tap_dropped(void) {
    return atomic_load_explicit(&S.tap_dropped, memory_order_relaxed);
}

bool in_tap_consumed(float *x, float *y) {
    uint32_t tail = atomic_load_explicit(&S.tap_tail, memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&S.tap_head, memory_order_acquire);
    if (tail == head)
        return false;
    uint32_t slot = tail & IN_CLICK_MASK;
    int32_t ix = atomic_load_explicit(&S.tap_x[slot], memory_order_relaxed);
    int32_t iy = atomic_load_explicit(&S.tap_y[slot], memory_order_relaxed);
    atomic_store_explicit(&S.tap_tail, tail + 1u, memory_order_release);
    if (x)
        *x = (float)ix / 4096.0f;
    if (y)
        *y = (float)iy / 4096.0f;
    return true;
}

/* --- gamepads --------------------------------------------------------------- */

void in_on_gamepad_button(int slot, int btn, bool down) {
    if (slot < 0 || slot >= AME_GAMEPAD_MAX || btn < 0 || btn >= AME_GP_BTN_COUNT)
        return;
    atomic_store_explicit(&S.gp_btn[slot][btn], down ? 1 : 0,
                          memory_order_release);
}

void in_on_gamepad_axis(int slot, int axis, float value) {
    if (slot < 0 || slot >= AME_GAMEPAD_MAX || axis < 0
        || axis >= AME_GP_AXIS_COUNT)
        return;
    atomic_store_explicit(&S.gp_axis[slot][axis], (int32_t)(value * 1024.0f),
                          memory_order_release);
}

bool in_gamepad_button(int slot, int btn) {
    if (slot < 0 || slot >= AME_GAMEPAD_MAX || btn < 0 || btn >= AME_GP_BTN_COUNT)
        return false;
    return S.gp_btn_s[slot][btn] != 0;
}

float in_gamepad_axis(int slot, int axis) {
    if (slot < 0 || slot >= AME_GAMEPAD_MAX || axis < 0
        || axis >= AME_GP_AXIS_COUNT)
        return 0.0f;
    return S.gp_axis_s[slot][axis];
}

static bool bind_capacity(int action) {
    if (action < 0 || action >= AME_ACTION_MAX) {
        ame_set_errorf(AME_ERR_INPUT, "action id %d is out of range", action);
        return false;
    }
    if (S.bind_total[action] >= AME_BIND_PER_ACTION) {
        ame_set_errorf(AME_ERR_INPUT, "action %d already has %d bindings",
                       action, AME_BIND_PER_ACTION);
        return false;
    }
    return true;
}

bool in_bind_gamepad(int action, int slot, int btn) {
    if (!bind_capacity(action))
        return false;
    if (slot < 0 || slot >= AME_GAMEPAD_MAX || btn < 0
        || btn >= AME_GP_BTN_COUNT) {
        ame_set_errorf(AME_ERR_INPUT, "gamepad %d button %d out of range",
                       slot, btn);
        return false;
    }
    int i = S.gp_bind_count[action]++;
    S.gp_binds[action][i].slot = slot;
    S.gp_binds[action][i].btn = btn;
    S.bind_total[action]++;
    /* no invented pressed edge on the next step (matches in_bind_button) */
    uint8_t live = atomic_load_explicit(&S.gp_btn[slot][btn],
                                        memory_order_acquire);
    S.gp_btn_s[slot][btn] = live;
    S.gp_btn_prev[slot][btn] = live;
    return true;
}

bool in_bind_gamepad_axis(int action, int slot, int axis, float threshold) {
    if (!bind_capacity(action))
        return false;
    if (slot < 0 || slot >= AME_GAMEPAD_MAX || axis < 0
        || axis >= AME_GP_AXIS_COUNT) {
        ame_set_errorf(AME_ERR_INPUT, "gamepad %d axis %d out of range",
                       slot, axis);
        return false;
    }
    int i = S.ga_bind_count[action]++;
    S.ga_binds[action][i].slot = slot;
    S.ga_binds[action][i].axis = axis;
    S.ga_binds[action][i].thr = (int32_t)(threshold * 1024.0f);
    S.bind_total[action]++;
    float live = (float)atomic_load_explicit(&S.gp_axis[slot][axis],
                                             memory_order_acquire) / 1024.0f;
    S.gp_axis_s[slot][axis] = live;
    S.gp_axis_prev[slot][axis] = live;
    return true;
}

/* --- motion sensors --------------------------------------------------------- */

/* Install-once semantics in the only place it matters: the sensor hooks call
 * INTO this struct's function pointers, so replacing (or clearing) it while a
 * sensor is enabled leaves that sensor pointing at a backend the app may
 * already have freed - the crash arrives in in_sensor_disable(), far from the
 * call that caused it. Replacing with nothing enabled, or clearing after
 * disabling, stays legal (state-audit.txt #5). */
void in_set_backend(const ame_input_backend *ops) {
    if (ops != g_backend) {
        for (int s = 0; s < AME_SENSOR_COUNT; s++) {
            if (atomic_load_explicit(&S.sens_on[s], memory_order_acquire)) {
                ame_set_errorf(AME_ERR_INPUT,
                               "in_set_backend while sensor %d is enabled: "
                               "disable sensors before replacing the backend",
                               s);
                return;
            }
        }
    }
    g_backend = ops;
}

bool in_sensor_enable(int sensor) {
    if (sensor < 0 || sensor >= AME_SENSOR_COUNT)
        return false;
    if (atomic_load_explicit(&S.sens_on[sensor], memory_order_acquire))
        return true;
    if (!g_backend || !g_backend->enable || !g_backend->enable(sensor))
        return false;
    atomic_store_explicit(&S.sens_on[sensor], 1, memory_order_release);
    return true;
}

void in_sensor_disable(int sensor) {
    if (sensor < 0 || sensor >= AME_SENSOR_COUNT)
        return;
    atomic_store_explicit(&S.sens_on[sensor], 0, memory_order_release);
    if (g_backend && g_backend->disable)
        g_backend->disable(sensor);
}

bool in_sensor_enabled(int sensor) {
    return sensor >= 0 && sensor < AME_SENSOR_COUNT
        && atomic_load_explicit(&S.sens_on[sensor], memory_order_acquire) != 0;
}

bool in_sensor_get(int sensor, float out[3], unsigned long long *ts_us) {
    if (!in_sensor_enabled(sensor) || !out)
        return false;
    for (int i = 0; i < 3; i++)
        out[i] = (float)atomic_load_explicit(&S.sens_v[sensor][i],
                                             memory_order_acquire) / 1024.0f;
    if (ts_us)
        *ts_us = atomic_load_explicit(&S.sens_ts[sensor], memory_order_acquire);
    return true;
}

bool in_gamepad_rumble(int slot, float low, float high, int duration_ms) {
    if (slot < 0 || slot >= AME_GAMEPAD_MAX || duration_ms <= 0)
        return false;
    if (!g_backend || !g_backend->rumble)
        return false;
    return g_backend->rumble(slot, low, high, duration_ms);
}

void in_on_sensor(int sensor, const float v[3], unsigned long long ts_us) {
    if (sensor < 0 || sensor >= AME_SENSOR_COUNT || !v)
        return;
    for (int i = 0; i < 3; i++)
        atomic_store_explicit(&S.sens_v[sensor][i], (int32_t)(v[i] * 1024.0f),
                              memory_order_relaxed);
    atomic_store_explicit(&S.sens_ts[sensor], ts_us, memory_order_release);
}
