/* tests — input action layer (input.txt): bindings, edges, axes. Backend
 * events are simulated with in_on_* exactly as SDL/asyncinput would call
 * them (they only write atomics; the logic thread polls). */
#include "utest.h"
#include <ame/ame.h>
#include <ame/input.h>

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
    UT_ASSERT_NEAR(in_axis(AME_AXIS_WHEEL), 1.0f, 1e-3);
    in_begin_step();
    UT_ASSERT_NEAR(in_axis(AME_AXIS_WHEEL), 0.0f, 1e-3); /* consumed (delta) */

    UT_CASE("real-time raw read (sub-tick)");
    in_on_key(KEY_A, true);
    UT_ASSERT(in_key_down_raw(KEY_A)); /* no begin_step needed */

    UT_OK();
    return ut_done("test_input");
}
