#include "input.h"

#include "gameplay.h"

#include <ame/app.h>
#include <ame/input.h>

void game_input_bindings(void)
{
    in_bind_key(BF_ACT_UP, AME_KEY_W);
    in_bind_key(BF_ACT_UP, AME_KEY_UP);
    in_bind_key(BF_ACT_DOWN, AME_KEY_S);
    in_bind_key(BF_ACT_DOWN, AME_KEY_DOWN);
    in_bind_key(BF_ACT_LEFT, AME_KEY_A);
    in_bind_key(BF_ACT_LEFT, AME_KEY_LEFT);
    in_bind_key(BF_ACT_RIGHT, AME_KEY_D);
    in_bind_key(BF_ACT_RIGHT, AME_KEY_RIGHT);
    in_bind_key(BF_ACT_BOOST, AME_KEY_LSHIFT);
    in_bind_key(BF_ACT_BOOST, AME_KEY_RSHIFT);
    in_bind_key(BF_ACT_SWITCH, AME_KEY_E);
    in_bind_key(BF_ACT_RESTART, AME_KEY_R);
    in_bind_key(BF_ACT_ADVANCE, AME_KEY_RETURN);
    in_bind_key(BF_ACT_JUMP, AME_KEY_SPACE);
    in_bind_key(BF_ACT_QUIT, AME_KEY_ESCAPE);
    in_bind_key(BF_ACT_QUIT, AME_KEY_Q);
    in_bind_key(BF_ACT_RELOAD, AME_KEY_F5);
    /* gamepad parity (docs/input.txt CHECK): stick lean + dpad steer,
     * right trigger boosts, face buttons for the kb-only extras.
     * QUIT stays keyboard-only so a pad never exits the game by
     * accident; 4 binds per action is the capacity ceiling. */
    (void)in_bind_gamepad(BF_ACT_UP, 0, AME_GP_DPAD_UP);
    (void)in_bind_gamepad_axis(BF_ACT_UP, 0, AME_GP_AXIS_LY, -0.35f);
    (void)in_bind_gamepad(BF_ACT_DOWN, 0, AME_GP_DPAD_DOWN);
    (void)in_bind_gamepad_axis(BF_ACT_DOWN, 0, AME_GP_AXIS_LY, 0.35f);
    (void)in_bind_gamepad(BF_ACT_LEFT, 0, AME_GP_DPAD_LEFT);
    (void)in_bind_gamepad_axis(BF_ACT_LEFT, 0, AME_GP_AXIS_LX, -0.35f);
    (void)in_bind_gamepad(BF_ACT_RIGHT, 0, AME_GP_DPAD_RIGHT);
    (void)in_bind_gamepad_axis(BF_ACT_RIGHT, 0, AME_GP_AXIS_LX, 0.35f);
    (void)in_bind_gamepad_axis(BF_ACT_BOOST, 0, AME_GP_AXIS_RT, 0.5f);
    (void)in_bind_gamepad(BF_ACT_SWITCH, 0, AME_GP_X);
    (void)in_bind_gamepad(BF_ACT_JUMP, 0, AME_GP_A);
    (void)in_bind_gamepad(BF_ACT_ADVANCE, 0, AME_GP_START);
    (void)in_bind_gamepad(BF_ACT_RESTART, 0, AME_GP_BACK);
}

void game_input_reset(void)
{
    in_clear_down();
    bf_hold_accel(0);
    bf_hold_yaw(0);
    bf_hold_move(0);
    bf_hold_boost(0);
}

void game_input_poll(void)
{
    int accel = (in_held(BF_ACT_UP) ? 1 : 0) - (in_held(BF_ACT_DOWN) ? 1 : 0);
    int yaw = (in_held(BF_ACT_RIGHT) ? 1 : 0)
              - (in_held(BF_ACT_LEFT) ? 1 : 0);
    bf_hold_accel(accel);
    bf_hold_yaw(yaw);
    bf_hold_move(yaw);
    bf_hold_boost(in_held(BF_ACT_BOOST));

    /* discrete decisions: exactly one fixed step sees each press */
    if (in_pressed(BF_ACT_QUIT))
        ame_request_quit();
    if (in_pressed(BF_ACT_SWITCH))
        bf_request_switch();
    if (in_pressed(BF_ACT_RESTART))
        bf_request_restart();
    if (in_pressed(BF_ACT_ADVANCE))
        bf_request_advance();
    if (in_pressed(BF_ACT_JUMP)) {
        bf_request_advance();
        bf_request_jump();
    }
}
