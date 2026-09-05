#include "ame/input.h"

#include "asyncinput.h"

#include <stdatomic.h>
#include <string.h>

static ame_input_handler g_handler;
static void *g_user;
static int g_open;
static _Atomic int g_btn_left;

static void on_ni(const struct ni_event *ev, void *ud)
{
    (void)ud;
    if (!g_handler || !ev) return;

    ame_raw_event out;
    memset(&out, 0, sizeof(out));

    if (ev->type == NI_EV_REL) {
        out.kind = AME_INPUT_MOVE;
        if (ev->code == NI_REL_X) out.dx = (float)ev->value;
        if (ev->code == NI_REL_Y) out.dy = (float)ev->value;
        if (out.dx == 0.0f && out.dy == 0.0f) return;
        g_handler(&out, g_user);
        return;
    }

    if (ev->type == NI_EV_KEY) {
        int down = ev->value != 0;
        if (ev->code == NI_BTN_LEFT) {
            int prev = atomic_exchange(&g_btn_left, down);
            out.kind = AME_INPUT_BUTTON;
            out.code = ev->code;
            out.value = down;
            out.pressed = (down && !prev) ? 1 : 0;
            g_handler(&out, g_user);
            return;
        }
        out.kind = AME_INPUT_KEY;
        out.code = ev->code;
        out.value = down;
        out.pressed = down ? 1 : 0; /* keys: treat non-zero as press; skip repeats if value==2 */
        if (ev->value == 2) out.pressed = 0;
        g_handler(&out, g_user);
    }
}

int ame_input_open(ame_input_handler handler, void *user)
{
    g_handler = handler;
    g_user = user;
    atomic_store(&g_btn_left, 0);
    if (ni_init(0) != 0) return 0;
    ni_enable_mice(1);
    if (ni_register_callback(on_ni, NULL, 0) != 0) {
        ni_shutdown();
        return 0;
    }
    g_open = 1;
    return 1;
}

void ame_input_close(void)
{
    if (g_open) ni_shutdown();
    g_open = 0;
    g_handler = NULL;
    g_user = NULL;
}

int ame_input_device_count(void)
{
    return g_open ? ni_device_count() : 0;
}
