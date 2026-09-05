#include "input.h"
#include "gameplay.h"
#include "asyncinput.h"

#include <stdatomic.h>

extern _Atomic int g_quit;

static int k_w, k_s, k_a, k_d, k_up, k_dn, k_left, k_right, k_shift;

void game_input_reset(void)
{
    k_w = k_s = k_a = k_d = k_up = k_dn = k_left = k_right = k_shift = 0;
}

static void sync_holds(void)
{
    int accel = ((k_w || k_up) ? 1 : 0) - ((k_s || k_dn) ? 1 : 0);
    int yaw   = ((k_d || k_right) ? 1 : 0) - ((k_a || k_left) ? 1 : 0);
    bf_hold_accel(accel);
    bf_hold_yaw(yaw);
    bf_hold_move(yaw);
    bf_hold_boost(k_shift);
}

void game_input_on_raw(const ame_raw_event *ev, void *user)
{
    (void)user;
    if (ev->kind != AME_INPUT_KEY) return;
    int down = ev->value != 0;
    int code = ev->code;
    if (code == NI_KEY_W) k_w = down;
    if (code == NI_KEY_S) k_s = down;
    if (code == NI_KEY_A) k_a = down;
    if (code == NI_KEY_D) k_d = down;
    if (code == NI_KEY_UP) k_up = down;
    if (code == NI_KEY_DOWN) k_dn = down;
    if (code == NI_KEY_LEFT) k_left = down;
    if (code == NI_KEY_RIGHT) k_right = down;
    if (code == NI_KEY_LEFTSHIFT || code == NI_KEY_RIGHTSHIFT) k_shift = down;
    sync_holds();

    if (ev->pressed) {
        if (code == NI_KEY_ESC || code == NI_KEY_Q) {
            atomic_store(&g_quit, 1);
            return;
        }
        if (code == NI_KEY_E) bf_request_switch();
        if (code == NI_KEY_R) bf_request_restart();
        if (code == NI_KEY_ENTER) bf_request_advance();
        if (code == NI_KEY_SPACE) {
            bf_request_advance();
            bf_request_jump();
        }
    }
}
