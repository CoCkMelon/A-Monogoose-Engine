/* tests — input action layer (input.txt): bindings, edges, axes. Backend
 * events are simulated with in_on_* exactly as SDL/asyncinput would call
 * them (they only write atomics; the logic thread polls). */
#include "utest.h"
#include <ame/ame.h>
#include <ame/input.h>

#include <string.h>

#define IN_TAP_FLOOD 40u /* > queue cap (32) + touch slots, forces drops */

/* backend-thread writer for the soak: only in_on_* calls, exactly what
 * SDL/asyncinput threads do; TSan validates the atomic protocol. */
#include <pthread.h>
static void *utest_soak_writer(void *arg) {
    (void)arg;
    for (long i = 0; i < 4000; i++) {
        in_on_gamepad_button(0, AME_GP_A, (i & 1) != 0);
        in_on_gamepad_axis(0, AME_GP_AXIS_LX, (float)(i % 100) / 100.0f - 0.5f);
        in_on_touch(5, 0.25f, 0.25f, true);
        in_on_touch(5, 0.25f, 0.25f, false);
        float v[3] = { 0.1f, 0.2f, 0.3f };
        in_on_sensor(AME_SENSOR_ACCEL, v, (unsigned long long)i);
        if ((i & 7) == 0)
            in_on_device_add(AME_DEV_GAMEPAD, 1, "soakpad");
        if ((i & 7) == 4)
            in_on_device_remove(AME_DEV_GAMEPAD, 1);
    }
    return NULL;
}
static pthread_t g_soak_th;
static void utest_soak_start(void) {
    pthread_create(&g_soak_th, NULL, utest_soak_writer, NULL);
}
static void utest_soak_stop(void) { pthread_join(g_soak_th, NULL); }

static void utest_soak_start(void);
static void utest_soak_stop(void);

static int utest_devices_tail(void); /* appended device-abstraction cases */

#define KEY_A 4   /* arbitrary backend codes (SDL scancodes in a real build) */
#define KEY_D 7
#define KEY_SP 44
#define KEY_R 21

int main(void) {
    printf("=== test_input ===\n");
    in_reset();

    UT_CASE("bind + held/pressed edges once");
    UT_ASSERT(in_bind_key(AME_ACT_JUMP, KEY_SP));
    UT_ASSERT(in_bind_key(AME_ACT_LEFT, KEY_A));

    in_begin_step();                    /* step 1: nothing down */
    UT_ASSERT(!in_held(AME_ACT_JUMP));
    in_on_key(KEY_SP, true);            /* backend writes atomics */
    in_begin_step();                    /* step 2: press edge */
    UT_ASSERT(in_held(AME_ACT_JUMP));
    UT_ASSERT(in_pressed(AME_ACT_JUMP));
    UT_ASSERT(!in_released(AME_ACT_JUMP));
    in_begin_step();                    /* step 3: still held, no edge */
    UT_ASSERT(in_held(AME_ACT_JUMP));
    UT_ASSERT(!in_pressed(AME_ACT_JUMP));
    in_on_key(KEY_SP, false);
    in_begin_step();                    /* step 4: release edge */
    UT_ASSERT(!in_held(AME_ACT_JUMP));
    UT_ASSERT(in_released(AME_ACT_JUMP));
    UT_ASSERT(!in_pressed(AME_ACT_JUMP));
    in_begin_step();                    /* step 5: quiet */
    UT_ASSERT(!in_released(AME_ACT_JUMP));

    UT_CASE("keyboard + second binding on one action");
    UT_ASSERT(in_bind_key(AME_ACT_JUMP, KEY_R)); /* two keys -> same action */
    in_on_key(KEY_R, true);
    in_begin_step();
    UT_ASSERT(in_pressed(AME_ACT_JUMP));

    UT_CASE("mouse pos / buttons / wheel accumulate");
    in_on_mouse_move(320.5f, 240.0f);
    in_on_mouse_button(AME_BTN_LEFT, true);
    in_on_wheel(1.0f);
    in_begin_step();
    UT_ASSERT_NEAR(in_axis(AME_AXIS_MOUSE_X), 320.5f, 1e-3);
    UT_ASSERT_NEAR(in_axis(AME_AXIS_MOUSE_Y), 240.0f, 1e-3);
    UT_ASSERT(in_mouse_button_raw(AME_BTN_LEFT));
    UT_ASSERT(in_btn_held(AME_BTN_LEFT));
    UT_ASSERT_NEAR(in_axis(AME_AXIS_WHEEL), 1.0f, 1e-3);

    UT_CASE("mouse buttons bind as actions and clicks keep coordinates");
    UT_ASSERT(in_bind_button(AME_ACT_FIRE, AME_BTN_LEFT));
    in_begin_step();
    UT_ASSERT(in_held(AME_ACT_FIRE));
    UT_ASSERT(!in_pressed(AME_ACT_FIRE)); /* rebind does not invent an edge */
    in_on_mouse_button(AME_BTN_LEFT, false);
    in_begin_step();
    UT_ASSERT(!in_btn_held(AME_BTN_LEFT));
    in_on_mouse_button_at(AME_BTN_LEFT, true, 320.5f, 240.0f);
    in_begin_step();
    UT_ASSERT(in_pressed(AME_ACT_FIRE));
    float cx = 0, cy = 0;
    UT_ASSERT(in_click_consumed(&cx, &cy));
    UT_ASSERT_NEAR(cx, 320.5f, 1e-3);
    UT_ASSERT_NEAR(cy, 240.0f, 1e-3);
    UT_ASSERT(in_click_consumed(&cx, &cy));
    UT_ASSERT_NEAR(cx, 320.5f, 1e-3);
    UT_ASSERT_NEAR(cy, 240.0f, 1e-3);
    UT_ASSERT(!in_click_consumed(NULL, NULL));
    in_begin_step();
    UT_ASSERT_NEAR(in_axis(AME_AXIS_WHEEL), 0.0f, 1e-3); /* consumed (delta) */

    UT_CASE("real-time raw read (sub-tick)");
    in_on_key(KEY_A, true);
    UT_ASSERT(in_key_down_raw(KEY_A)); /* no begin_step needed */

    utest_devices_tail();

    UT_OK();
    return ut_done("test_input");
}

/* --- device abstraction (input.h): registry, touch, gamepads, sensors ----
 * Appended cases; same simulation style: in_on_* as any backend would. */

static bool g_fake_enable_called;
static bool g_fake_enable(int sensor) { (void)sensor; g_fake_enable_called = true; return true; }
static void g_fake_disable(int sensor) { (void)sensor; }
static const ame_input_backend g_fake_backend = {
    .enable = g_fake_enable, .disable = g_fake_disable,
};
/* a second identity: the swap cases must be refused whatever it points at */
static const ame_input_backend g_other_backend = {
    .enable = g_fake_enable, .disable = g_fake_disable,
};

static int utest_devices_tail(void) {
    UT_CASE("registry: keyboard+mouse always present, add/remove pad");
    in_reset();
    UT_ASSERT(in_device_present(AME_DEV_KEYBOARD, 0));
    UT_ASSERT(in_device_present(AME_DEV_MOUSE, 0));
    UT_ASSERT(in_device_count() >= 2);
    in_on_device_add(AME_DEV_GAMEPAD, 0, "testpad");
    UT_ASSERT(in_device_present(AME_DEV_GAMEPAD, 0));
    int n = in_device_count();
    ame_input_device d;
    bool found = false;
    for (int i = 0; i < n; i++)
        if (in_device_info(i, &d) && d.kind == AME_DEV_GAMEPAD) {
            found = d.slot == 0 && strcmp(d.name, "testpad") == 0;
        }
    UT_ASSERT(found);
    in_on_device_remove(AME_DEV_GAMEPAD, 0);
    UT_ASSERT(!in_device_present(AME_DEV_GAMEPAD, 0));

    UT_CASE("gamepad button binds into the action system");
    UT_ASSERT(in_bind_gamepad(AME_ACT_JUMP, 0, AME_GP_A));
    in_begin_step();
    UT_ASSERT(!in_held(AME_ACT_JUMP));
    in_on_gamepad_button(0, AME_GP_A, true);
    in_begin_step();
    UT_ASSERT(in_held(AME_ACT_JUMP));
    UT_ASSERT(in_pressed(AME_ACT_JUMP));
    in_begin_step();
    UT_ASSERT(!in_pressed(AME_ACT_JUMP));
    in_on_gamepad_button(0, AME_GP_A, false);
    in_begin_step();
    UT_ASSERT(in_released(AME_ACT_JUMP));
    UT_ASSERT(in_gamepad_button(0, AME_GP_A) == false);

    UT_CASE("gamepad axis binds with signed threshold");
    UT_ASSERT(in_bind_gamepad_axis(AME_ACT_BOOST, 0, AME_GP_AXIS_RT, 0.5f));
    UT_ASSERT(in_bind_gamepad_axis(AME_ACT_LEFT, 0, AME_GP_AXIS_LX, -0.5f));
    in_on_gamepad_axis(0, AME_GP_AXIS_RT, 0.7f);
    in_on_gamepad_axis(0, AME_GP_AXIS_LX, -0.8f);
    in_begin_step();
    UT_ASSERT(in_held(AME_ACT_BOOST));
    UT_ASSERT(in_held(AME_ACT_LEFT));
    UT_ASSERT(in_gamepad_axis(0, AME_GP_AXIS_RT) > 0.65f);
    in_on_gamepad_axis(0, AME_GP_AXIS_RT, 0.3f);
    in_on_gamepad_axis(0, AME_GP_AXIS_LX, 0.8f); /* wrong sign: not held */
    in_begin_step();
    UT_ASSERT(!in_held(AME_ACT_BOOST));
    UT_ASSERT(!in_held(AME_ACT_LEFT));

    UT_CASE("touch points + one-shot taps");
    in_on_touch(11, 0.25f, 0.5f, true);
    in_on_touch(12, 0.75f, 0.5f, true);
    UT_ASSERT(in_touch_count() == 2);
    ame_input_touch t;
    UT_ASSERT(in_touch_point(0, &t) && t.id == 11 && t.x > 0.24f && t.x < 0.26f);
    UT_ASSERT(in_touch_point(1, &t) && t.id == 12);
    UT_ASSERT(!in_touch_point(2, &t));
    in_on_touch(11, 0.25f, 0.5f, false);
    UT_ASSERT(in_touch_count() == 1);
    float tx = -1, ty = -1;
    UT_ASSERT(in_tap_consumed(&tx, &ty));
    UT_ASSERT(tx > 0.24f && tx < 0.26f && ty > 0.49f && ty < 0.51f);
    UT_ASSERT(!in_tap_consumed(&tx, &ty));   /* consumed exactly once */
    in_on_touch(12, 0.75f, 0.5f, false);
    UT_ASSERT(in_touch_count() == 0);

    UT_CASE("sensors: enable via backend ops, values + timestamps");
    UT_ASSERT(!in_sensor_enabled(AME_SENSOR_ACCEL));
    UT_ASSERT(!in_sensor_enable(AME_SENSOR_ACCEL));  /* no backend yet */
    in_set_backend(&g_fake_backend);
    UT_ASSERT(in_sensor_enable(AME_SENSOR_ACCEL));
    UT_ASSERT(g_fake_enable_called);
    UT_ASSERT(in_sensor_enable(AME_SENSOR_ACCEL));   /* idempotent */
    float v[3] = { 0.1f, -9.8f, 0.3f };
    in_on_sensor(AME_SENSOR_ACCEL, v, 1234567ull);
    float out[3]; unsigned long long ts = 0;
    UT_ASSERT(in_sensor_get(AME_SENSOR_ACCEL, out, &ts));
    UT_ASSERT(out[1] < -9.7f && out[1] > -9.9f && ts == 1234567ull);
    UT_ASSERT(!in_sensor_get(AME_SENSOR_GYRO, out, &ts)); /* not enabled */
    /* swapping the backend out from under a live sensor is the crash the
     * audit's "install-once" note was about: the enabled sensor would call
     * into a struct the app may already have freed */
    UT_CASE("backend cannot be swapped while a sensor is enabled");
    ame_clear_error();
    in_set_backend(&g_other_backend);
    UT_ASSERTF(ame_last_error_code() == AME_ERR_INPUT,
               "swap under a live sensor was accepted (err=%s)",
               ame_error_name(ame_last_error_code()));
    UT_ASSERT(g_fake_enable_called); /* the old backend is still in place */
    in_set_backend(NULL);            /* same rule for clearing it */
    UT_ASSERT(ame_last_error_code() == AME_ERR_INPUT);

    in_sensor_disable(AME_SENSOR_ACCEL);
    UT_ASSERT(!in_sensor_enabled(AME_SENSOR_ACCEL));
    UT_ASSERT(!in_sensor_get(AME_SENSOR_ACCEL, out, &ts));
    /* with nothing enabled the backend may be replaced or cleared */
    ame_clear_error();
    in_set_backend(&g_other_backend);
    UT_ASSERT(ame_last_error_code() == AME_ERR_OK);
    in_set_backend(&g_fake_backend);
    UT_CASE("dpad buttons bind like face buttons");
    in_reset();
    UT_ASSERT(in_bind_gamepad(AME_ACT_LEFT, 0, AME_GP_DPAD_LEFT));
    in_on_gamepad_button(0, AME_GP_DPAD_LEFT, true);
    in_begin_step();
    UT_ASSERT(in_held(AME_ACT_LEFT));
    UT_ASSERT(in_pressed(AME_ACT_LEFT));
    in_on_gamepad_button(0, AME_GP_DPAD_LEFT, false);
    in_begin_step();
    UT_ASSERT(in_released(AME_ACT_LEFT));

    UT_CASE("rumble: graceful without backend or pad");
    UT_ASSERT(!in_gamepad_rumble(0, 0.5f, 0.5f, 100));   /* no backend ops */
    in_set_backend(&g_fake_backend);                     /* ops w/o rumble */
    UT_ASSERT(!in_gamepad_rumble(0, 0.5f, 0.5f, 100));   /* NULL fn ptr */
    UT_ASSERT(!in_gamepad_rumble(0, 0.5f, 0.5f, 0));     /* bad duration */
    UT_ASSERT(!in_gamepad_rumble(AME_GAMEPAD_MAX, 0.5f, 0.5f, 10));
    in_set_backend(NULL);

    UT_CASE("tap queue overflow counts drops");
    in_reset();
    for (long id = 1; id <= IN_TAP_FLOOD; id++) {
        in_on_touch(id, 0.5f, 0.5f, true);
        in_on_touch(id, 0.5f, 0.5f, false);
    }
    unsigned dropped = in_tap_dropped();
    float fx, fy;
    unsigned consumed = 0;
    while (in_tap_consumed(&fx, &fy))
        consumed++;
    UT_ASSERT(consumed + dropped == IN_TAP_FLOOD);
    UT_ASSERT(dropped > 0 && consumed > 0);

    UT_CASE("in_clear_down clears EVERY down state (hunt regression)");
    in_reset();
    in_on_key(KEY_A, true);
    in_on_mouse_button(AME_BTN_LEFT, true);
    in_on_gamepad_button(0, AME_GP_A, true);
    in_on_touch(77, 0.5f, 0.5f, true);
    in_on_touch(77, 0.5f, 0.5f, false);      /* queues a tap */
    in_clear_down();
    UT_ASSERT(!in_key_down_raw(KEY_A));
    UT_ASSERT(!in_mouse_button_raw(AME_BTN_LEFT));
    UT_ASSERT(in_touch_count() == 0);
    float qx, qy;
    UT_ASSERT(!in_tap_consumed(&qx, &qy));   /* tap queue drained */
    in_begin_step();
    UT_ASSERT(!in_gamepad_button(0, AME_GP_A));

    UT_CASE("removed gamepad leaves no ghost stick/button (hunt regression)");
    in_reset();
    in_on_device_add(AME_DEV_GAMEPAD, 0, "pad");
    in_on_gamepad_button(0, AME_GP_A, true);
    in_on_gamepad_axis(0, AME_GP_AXIS_LX, -1.0f);
    in_begin_step();
    UT_ASSERT(in_gamepad_button(0, AME_GP_A));
    UT_ASSERT(in_gamepad_axis(0, AME_GP_AXIS_LX) < -0.9f);
    in_on_device_remove(AME_DEV_GAMEPAD, 0);  /* yanked mid-tilt */
    in_begin_step();
    UT_ASSERT(!in_gamepad_button(0, AME_GP_A));
    UT_ASSERT(in_gamepad_axis(0, AME_GP_AXIS_LX) == 0.0f);
    bool saw_pad = false;             /* registry keeps other kinds: reset
                                       * clears state, not physical devices */
    for (int i = 0; i < in_device_count(); i++) {
        ame_input_device d;
        if (in_device_info(i, &d) && d.kind == AME_DEV_GAMEPAD)
            saw_pad = true;
    }
    UT_ASSERT(!saw_pad);

    UT_CASE("threaded soak: backend writer vs logic reader (TSan bait)");
    in_reset();
    utest_soak_start();
    for (int step = 0; step < 400; step++) {
        in_begin_step();
        (void)in_held(AME_ACT_JUMP);
        (void)in_pressed(AME_ACT_LEFT);
        (void)in_touch_count();
        ame_input_touch tp;
        (void)in_touch_point(0, &tp);
        float ax, ay;
        (void)in_tap_consumed(&ax, &ay);
        float v[3]; unsigned long long ts;
        (void)in_sensor_get(AME_SENSOR_ACCEL, v, &ts);
        ame_input_device dv;
        for (int i = 0; i < in_device_count(); i++)
            (void)in_device_info(i, &dv);
    }
    utest_soak_stop();
    UT_ASSERT(in_touch_count() >= 0);

    in_set_backend(NULL);
    return 0;
}
