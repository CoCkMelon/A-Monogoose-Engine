# ame-next

A small, fast, data-oriented game engine written in **one C23 core**, targeting
**2D and 3D from the same code** and four platforms (POSIX desktop, Windows,
Android, web/Emscripten) via SDL3 + OpenGL (ES).

This is the clean-slate successor to
[A-Monogoose-Engine](https://github.com/CoCkMelon/A-Monogoose-Engine)
(C/C++ + Flecs ECS + a Unity-style facade). Those two layers are exactly what
**does not exist** here: no ECS, no GameObject/Component wrapper — the plain
C API *is* the engine. Full design rules: [docs/principles.txt](docs/principles.txt),
architecture per module: [docs/](docs/) (README.txt is the master spec).

## The one rule

Two kinds of object (docs/principles.txt):

- **SETUP** objects (render pass, camera, audio patch): fluent builder — takes
  a pointer, mutates, returns the same pointer for chaining. Setup-time only.
- **HOT** state (entities, cards, bullets): fixed static pools, plain arrays,
  plain functions. Never builders. No allocation in the loop.

## Layout

```
include/ame/    public headers (extern "C"), one per module
src/            engine implementation (pure C23; one .c owns each module's state)
examples/       games + demos, each with its own CMakeLists (A-Mongoose style)
  memory_game/  THE FIRST GAME: hot-seat Memory with 3D flipping card panels
tests/          utest suites: math, pool, geometry (2D+3D), input, audio,
                text layout, memory sim (golden replay), golden render (GL)
tools/          build-time host tools (bake_font: ttf -> C arrays atlas)
generated/      build-time generated sources (font atlas)
third_party/    stb (headers), asyncinput (raw input backend, bundled)
docs/           the engine spec (principles, data, loop, render, ...)
assets/         fonts etc.
```

## Build & run (desktop)

```
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang-19
cmake --build build
ctest --test-dir build --output-on-failure
./build/examples/memory_game/memory_game
```

Requires: clang-19+ (or gcc-15+) for C23, SDL3, EGL+GL (Mesa works).

Headless (no display — CI / servers):

```
SDL_VIDEODRIVER=offscreen SDL_AUDIO_DRIVER=dummy \
AME_SCREENSHOT=shot.png ./build/examples/memory_game/memory_game
```

## Modules (v0)

| module | spec | status |
|---|---|---|
| pools (handles + deferred despawn) | docs/data.txt | done, tested |
| geometry/collision queries (BVH-ish grid broadphase, 2D+3D from one core) | docs/physics.txt | done, tested both dims |
| events (deferred ring, discrete gameplay events) | docs/events.txt | done, tested |
| input (actions/bindings/edges over atomics; SDL or asyncinput at compile time) | docs/input.txt | done, tested (SDL backend live; asyncinput backend compiled in variant) |
| audio (deterministic synth, SPSC cmd queue, SDL stream) | docs/audio.txt | done, golden-hash tested |
| text (baked glyph atlas, tags, wrap, world/UI draw, one layout engine 2D+3D) | docs/text.txt | done, tested |
| render (ONE program, single pass, batched quads, injected GL loader) | docs/render.txt | done, golden render test headless |
| camera (one module; 2D pixel-perfect ortho / 3D persp via AME_2D/AME_3D) | docs/loop.txt | done, tested |
| app bootstrap (SDL3 callbacks, split threads, 1000 Hz fixed logic step) | docs/loop.txt | done (desktop); web/Android shims: see handoff |

Deferred to later stages (per spec): networking (Stage 1), multipass/lighting
(Stage 2), runtime codecs (Stage 3), RT (Stage 4), libfyaml dialogue/save/
settings (not needed by the first game slice).

## Tests

`ctest` runs: math, pool, geometry (built in BOTH dimensions), input edges,
audio determinism (golden mix hash), text layout, memory-game sim (full
scripted games, determinism replay), and a **golden render** test that builds
a real GL context on EGL surfaceless + llvmpipe, draws the scene, and asserts
pixel content + frame-to-frame determinism — no display needed.

## License

GPL-3.0 (matching the A-Monogoose line). Third-party libs keep their own
licenses (stb: public domain/MIT, asyncinput: see third_party/asyncinput).
