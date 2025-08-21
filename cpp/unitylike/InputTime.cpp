#include "Scene.h"
#include <atomic>
#include <cstdint>
#include <cstring>
#include <SDL3/SDL.h>

extern "C" {
#include "asyncinput.h"
}

namespace unitylike {

// Internal snapshot/state
namespace {
    struct FrameInput {
        std::atomic<int> move_dir{0};
        std::atomic<int> jump_down{0};
        std::atomic<int> jump_prev{0};
        std::atomic<int> quit{0};
    } g_input;

    struct TimeState {
        std::atomic<float> dt{0.0f};
        std::atomic<float> fdt{0.0f};
        std::atomic<float> since_start{0.0f};
    } g_time;

    // AsyncInput callback -> write atomics directly
    void on_input(const ni_event* ev, void* /*user*/) {
        if (!ev) return;
        if (ev->type == NI_EV_KEY) {
            const bool down = ev->value != 0;
            switch (ev->code) {
                case NI_KEY_A:
                case NI_KEY_LEFT:
                    // derive move_dir each press/release from A/D and LEFT/RIGHT
                    break;
                case NI_KEY_D:
                case NI_KEY_RIGHT:
                    break;
                case NI_KEY_SPACE:
                case NI_KEY_W:
                case NI_KEY_UP:
                    g_input.jump_down.store(down ? 1 : 0, std::memory_order_relaxed);
                    break;
                case NI_KEY_Q:
                case NI_KEY_ESC:
                    if (down) g_input.quit.store(1, std::memory_order_relaxed);
                    break;
                default: break;
            }
        }
        // Mouse can be added later if needed
    }

    // Maintain left/right booleans to compute move_dir
    std::atomic<int> s_left{0};
    std::atomic<int> s_right{0};

    void on_input_lr(const ni_event* ev, void*) {
        if (!ev) return;
        if (ev->type == NI_EV_KEY) {
            const bool down = ev->value != 0;
            if (ev->code == NI_KEY_A || ev->code == NI_KEY_LEFT) s_left.store(down ? 1 : 0, std::memory_order_relaxed);
            if (ev->code == NI_KEY_D || ev->code == NI_KEY_RIGHT) s_right.store(down ? 1 : 0, std::memory_order_relaxed);
            if (ev->code == NI_KEY_SPACE || ev->code == NI_KEY_W || ev->code == NI_KEY_UP) g_input.jump_down.store(down ? 1 : 0, std::memory_order_relaxed);
            if ((ev->code == NI_KEY_Q || ev->code == NI_KEY_ESC) && down) g_input.quit.store(1, std::memory_order_relaxed);
            const int md = (s_right.load(std::memory_order_relaxed) ? 1 : 0) - (s_left.load(std::memory_order_relaxed) ? 1 : 0);
            g_input.move_dir.store(md, std::memory_order_relaxed);
        }
    }

    std::atomic<int> s_inited{0};
    void ensure_asyncinput_init() {
        if (s_inited.load(std::memory_order_acquire)) return;
        if (ni_init(0) != 0) return; // silent fail; caller may still use SDL input
        ni_register_callback(on_input_lr, nullptr, 0);
        s_inited.store(1, std::memory_order_release);
    }
}

// Input facade
namespace Input {
    bool GetKey(int key) {
        ensure_asyncinput_init();
        // Minimal: only WASD/ARROWS/SPACE/ESC supported explicitly
        switch (key) {
            case NI_KEY_A:
            case NI_KEY_LEFT:  return s_left.load(std::memory_order_relaxed) != 0;
            case NI_KEY_D:
            case NI_KEY_RIGHT: return s_right.load(std::memory_order_relaxed) != 0;
            case NI_KEY_SPACE:
            case NI_KEY_W:
            case NI_KEY_UP:    return g_input.jump_down.load(std::memory_order_relaxed) != 0;
            case NI_KEY_Q:
            case NI_KEY_ESC:   return g_input.quit.load(std::memory_order_relaxed) != 0;
            default: return false;
        }
    }
    bool GetKeyDown(int key) {
        // For jump edge, we track previous in g_input.jump_prev; for others, return false for now
        if (key == NI_KEY_SPACE || key == NI_KEY_W || key == NI_KEY_UP) {
            const int cur = g_input.jump_down.load(std::memory_order_relaxed);
            const int prev = g_input.jump_prev.load(std::memory_order_relaxed);
            return cur && !prev;
        }
        return false;
    }
    bool GetKeyUp(int key) {
        if (key == NI_KEY_SPACE || key == NI_KEY_W || key == NI_KEY_UP) {
            const int cur = g_input.jump_down.load(std::memory_order_relaxed);
            const int prev = g_input.jump_prev.load(std::memory_order_relaxed);
            return !cur && prev;
        }
        return false;
    }
}

// Time facade
namespace Time {
    float deltaTime() { return g_time.dt.load(std::memory_order_relaxed); }
    float fixedDeltaTime() { return g_time.fdt.load(std::memory_order_relaxed); }
    float timeSinceLevelLoad() { return g_time.since_start.load(std::memory_order_relaxed); }
}

// Hook points for engine/app to update snapshots each frame
// These are not part of the public header yet, but could be exposed if needed.
static inline void unitylike_begin_update(float dt) {
    g_time.dt.store(dt, std::memory_order_relaxed);
    g_time.since_start.store(g_time.since_start.load(std::memory_order_relaxed) + dt, std::memory_order_relaxed);
    // Edge updates: capture prev for jump
    const int cur = g_input.jump_down.load(std::memory_order_relaxed);
    g_input.jump_prev.store(cur, std::memory_order_relaxed);
}

static inline void unitylike_set_fixed_dt(float fdt) {
    g_time.fdt.store(fdt, std::memory_order_relaxed);
}

} // namespace unitylike

