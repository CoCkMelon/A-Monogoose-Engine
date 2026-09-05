# ame-next

A game engine in one C23 core — **2D and 3D from a single codebase** —
built to the spec in `docs/` (the spec decides). Anchor game: a
**competitive Memory** (4×4 pairs, strict two-player alternation, two
opens per turn, turn passes on every resolve, most matches wins)
with real 3D flipping card panels and in-scene billboard UI, served by
the same single-pass renderer.

## Status

| Stage | What | State |
|---|---|---|
| 0 | local game, headless screenshots, packaged unit + clean-machine smoke | **done** |
| 1 | server-authoritative **online** Memory over TCP (loopback-tested) | **done** |
| 2 | advanced rendering: forward lighting ✓, offscreen + post pass ✓, particles ✓ (shadows next) | in progress |
| — | A-Monogoose parity: audio occlusion raytracer ✓ (ported), decoded PCM + wav ✓, tilemap (.tmj) + 2D proof ✓, dialogue (libfyaml + bake-to-C) ✓, Assimp mesh import ✓ | done (opus decode = Stage 3) |
| — | Math layer: cglm-parity API (m3/quat/frustum/curves) + SSE2 fast paths **bit-identical** to the scalar bodies; cglm is a test-only oracle/benchmark (`tests/test_math_cglm.c`), never an engine dependency | done |
| — | Lean 4 formal model (geometry/picking/shader contracts + game rules) | done, `lean/` |
| 2+ | advanced rendering, richer 3D, ports | roadmap (`docs/README.txt`) |

## Build & test (POSIX, GCC 14+ or Clang 19+)

Clone with submodules (`git clone --recurse-submodules <url>`); plain clones are told to run `git submodule update --init`.

```sh
cmake -S . -B build -GNinja && ninja -C build      # -Werror clean by default
ctest --test-dir build                             # 11 tests incl. online loopback
```

Needs: CMake ≥3.16, Ninja, SDL3 dev, EGL/GLES dev (`libsdl3-dev libegl1
libgl1-mesa-dri libgles-dev` on Debian/Ubuntu). Optional: `libfyaml-dev`
(dialogue module; engine builds without it), `libassimp-dev` (the
`assimp2c` bake tool; baked assets are committed).

## Play

**Local hot-seat** (two players share the mouse):

```sh
./build/examples/memory_game/memory_game
```

**Online, server-authoritative** (two machines or two windows):

```sh
./build/examples/memory_game/mem_server 7777        # authoritative sim
AME_SERVER=127.0.0.1:7777 ./build/examples/memory_game/memory_game   # player 1
AME_SERVER=127.0.0.1:7777 ./build/examples/memory_game/memory_game   # player 2
```

The server owns the game; clients are thin views sending card-open
intents. Never-opened pairs are **hidden on the wire** (anti-peek); a
dropped opponent pauses the game on their turn and a rejoining client
resumes from a full snapshot; click after `WIN` for a voted rematch.

### Environment

| Var | Effect |
|---|---|
| `AME_SERVER=host:port` | online mode (falls back to local if unreachable) |
| `AME_SEED=0x…` | local: replay a specific shuffle (deterministic) |
| `AME_SCREENSHOT=path.png` | write a PNG after N frames, then exit |
| `AME_SCREENSHOT_FRAMES=n` | N (default 5) |
| `AME_FAKE_MOUSE=x,y` | headless hover checks |
| `AME_WINDOW_W/H` | window size |
| `AME_AUTOPLAY=1` | local: deterministic honest-memory bot (QA captures) |
| `AME_FIXED_FRAME_DT=s` | QA: logic runs inline at s/render-frame (deterministic mid-game screenshots; e.g. 0.0166667) |
| `MEM_TIME_SCALE=k` | server: authoritative pacing (tests) |
| `MEM_IDLE_EXIT=s` | server: exit when nobody is connected |

## Headless / CI

```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIO_DRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1 \
  AME_SCREENSHOT=shot.png ./build/examples/memory_game/memory_game
```

`tools/pack.sh` builds the release **unit** (static SDL3 where the
distro has it, otherwise a full `$ORIGIN/lib` bundle) and
`tools/smoke.sh` proves it on a clean machine: dependency audit, PNG
check, and same-seed ⇒ byte-identical screenshots. CI runs both
(`package` + `clean-smoke` jobs).

## The multipass decision (Stage 2, made)

`docs/render.txt` deferred multipass until a real need; Stage 2 is it.
**Decision:** the ONE geometry program/batch is untouched — with post
enabled (`rp_desc_post`), each frame renders into an offscreen RGBA8
scene target (same clear/depth) and a tiny second program composes it
into the presentation target with a cheap effect chain (tint,
vignette; identity settings are pixel-exact with the direct path —
test-proven). The compose renders into whatever framebuffer was bound
at `rp_begin_frame` (the SDL window, or a host FBO when embedded).
Offscreen targets for shadows/etc. build on this machinery.

## Layout

```
include/ame/  engine headers (math, pool, events, camera, render, text, …)
src/          engine core (C23; per-dimension build via AME_2D/AME_3D)
examples/memory_game/  the FIRST GAME (own CMakeLists) + mem_server
tests/        ctest suites (logic, geometry, camera, text, render, net)
tools/        bake_font, pack.sh, smoke.sh
lean/         Lean 4 model (pure core, no mathlib, zero sorry)
docs/         THE SPEC (README.txt first)
```
