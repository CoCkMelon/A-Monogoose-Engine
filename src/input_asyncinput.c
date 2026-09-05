/* ame-next — asyncinput backend (input.txt, AME_INPUT_ASYNCINPUT builds).
 *
 * Self-guarded: compiles to an empty TU unless the target defines
 * AME_INPUT_ASYNCINPUT. The engine calls in_asyncinput_init() from app
 * setup; asyncinput runs its OWN reader thread and invokes our callback,
 * which ONLY writes the shared input atomics (never gameplay, pools, sim).
 * Key ids are CANONICAL SDL scancodes engine-wide; evdev KEY_* codes are
 * mapped here so the action/binding layer is backend-agnostic.
 */
#if defined(AME_INPUT_ASYNCINPUT)

#include <ame/input.h>

#include <SDL3/SDL.h>
#include <asyncinput.h>

#include <stdatomic.h>

static bool g_available;

bool in_asyncinput_available(void) { return g_available; }

/* evdev KEY_* -> SDL scancode (canonical engine key id). Common set. */
static int ni_to_sdl(int code) {
    switch (code) {
    case KEY_Q: return SDL_SCANCODE_Q;
    case KEY_W: return SDL_SCANCODE_W;
    case KEY_E: return SDL_SCANCODE_E;
    case KEY_R: return SDL_SCANCODE_R;
    case KEY_T: return SDL_SCANCODE_T;
    case KEY_Y: return SDL_SCANCODE_Y;
    case KEY_U: return SDL_SCANCODE_U;
    case KEY_I: return SDL_SCANCODE_I;
    case KEY_O: return SDL_SCANCODE_O;
    case KEY_P: return SDL_SCANCODE_P;
    case KEY_A: return SDL_SCANCODE_A;
    case KEY_S: return SDL_SCANCODE_S;
    case KEY_D: return SDL_SCANCODE_D;
    case KEY_F: return SDL_SCANCODE_F;
    case KEY_G: return SDL_SCANCODE_G;
    case KEY_H: return SDL_SCANCODE_H;
    case KEY_J: return SDL_SCANCODE_J;
    case KEY_K: return SDL_SCANCODE_K;
    case KEY_L: return SDL_SCANCODE_L;
    case KEY_Z: return SDL_SCANCODE_Z;
    case KEY_X: return SDL_SCANCODE_X;
    case KEY_C: return SDL_SCANCODE_C;
    case KEY_V: return SDL_SCANCODE_V;
    case KEY_B: return SDL_SCANCODE_B;
    case KEY_N: return SDL_SCANCODE_N;
    case KEY_M: return SDL_SCANCODE_M;
    case KEY_1: return SDL_SCANCODE_1;
    case KEY_2: return SDL_SCANCODE_2;
    case KEY_3: return SDL_SCANCODE_3;
    case KEY_4: return SDL_SCANCODE_4;
    case KEY_5: return SDL_SCANCODE_5;
    case KEY_6: return SDL_SCANCODE_6;
    case KEY_7: return SDL_SCANCODE_7;
    case KEY_8: return SDL_SCANCODE_8;
    case KEY_9: return SDL_SCANCODE_9;
    case KEY_0: return SDL_SCANCODE_0;
    case KEY_SPACE:     return SDL_SCANCODE_SPACE;
    case KEY_ENTER:     return SDL_SCANCODE_RETURN;
    case KEY_ESC:       return SDL_SCANCODE_ESCAPE;
    case KEY_BACKSPACE: return SDL_SCANCODE_BACKSPACE;
    case KEY_TAB:       return SDL_SCANCODE_TAB;
    case KEY_LEFTCTRL:  return SDL_SCANCODE_LCTRL;
    case KEY_LEFTSHIFT: return SDL_SCANCODE_LSHIFT;
    case KEY_LEFTALT:   return SDL_SCANCODE_LALT;
    case KEY_UP:    return SDL_SCANCODE_UP;
    case KEY_DOWN:  return SDL_SCANCODE_DOWN;
    case KEY_LEFT:  return SDL_SCANCODE_LEFT;
    case KEY_RIGHT: return SDL_SCANCODE_RIGHT;
    default: return -1;
    }
}

/* runs on asyncinput's reader thread: atomics ONLY (input.txt pattern) */
static void on_raw_input(const struct ni_event *ev, void *ud) {
    (void)ud;
    if (ni_is_key_event(ev)) {
        /* mouse buttons arrive as NI_EV_KEY with NI_BTN_* codes */
        if (ni_is_mouse_button_code(ev->code)) {
            int btn = ev->code == NI_BTN_LEFT ? AME_BTN_LEFT
                    : ev->code == NI_BTN_RIGHT ? AME_BTN_RIGHT
                    : ev->code == NI_BTN_MIDDLE ? AME_BTN_MIDDLE : -1;
            if (btn >= 0)
                in_on_mouse_button(btn, ev->value != 0);
            return;
        }
        int sc = ni_to_sdl(ev->code);
        if (sc >= 0)
            in_on_key(sc, ev->value != 0);
        return;
    }
    if (ni_is_rel_event(ev)) {
        /* accumulate raw relative motion into a window-space position for
         * picking; games wanting raw deltas read axis deltas instead.
         * Plain statics: ONLY this callback (one asyncinput reader
         * thread) touches them - in_on_mouse_move does the engine-side
         * atomic publication. (_Atomic float fetch_add does not exist
         * in GCC 14's C23 stdatomic generics either.) */
        static float rx, ry;
        if (ev->code == NI_REL_X)
            rx += (float)ev->value;
        else if (ev->code == NI_REL_Y)
            ry += (float)ev->value;
        in_on_mouse_move(rx, ry);
    }
}

bool in_asyncinput_init(void) {
    ni_register_callback(on_raw_input, NULL, 0);
    if (ni_init(0) != 0) {
        /* no permission on /dev/input/event* — game keeps running with no
         * raw keyboard/mouse (input.txt); surface a clear message */
        g_available = false;
        return false;
    }
    g_available = true;
    return true;
}

void in_asyncinput_shutdown(void) {
    if (g_available)
        ni_shutdown();
    g_available = false;
}

#else /* !AME_INPUT_ASYNCINPUT */
/* empty TU: the SDL input path in app_sdl.c is the backend */
#endif
