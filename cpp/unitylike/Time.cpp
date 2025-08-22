#include "Scene.h"
#include <atomic>

namespace unitylike {

namespace {
    struct TimeState {
        std::atomic<float> dt{0.0f};
        std::atomic<float> fdt{0.0f};
        std::atomic<float> since_start{0.0f};
    } g_time;
}

namespace Time {
    float deltaTime() { return g_time.dt.load(std::memory_order_relaxed); }
    float fixedDeltaTime() { return g_time.fdt.load(std::memory_order_relaxed); }
    float timeSinceLevelLoad() { return g_time.since_start.load(std::memory_order_relaxed); }
}

void unitylike_begin_update(float dt) {
    g_time.dt.store(dt, std::memory_order_relaxed);
    g_time.since_start.store(g_time.since_start.load(std::memory_order_relaxed) + dt, std::memory_order_relaxed);
}

void unitylike_set_fixed_dt(float fdt) {
    g_time.fdt.store(fdt, std::memory_order_relaxed);
}

} // namespace unitylike

