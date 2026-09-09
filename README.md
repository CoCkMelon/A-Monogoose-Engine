# ame-next — Memory + Biscuit Fuel

Local hotseat Memory. One C23 core, one GL pass, 3D card slabs under an
**orthographic camera looking down −Z**. Picking is **2D** (cursor XY vs
card rectangles via `ame_geo_point_in_aabb_xy`). The OS cursor is hidden;
a **3D pointer mesh** is the cursor.

SDL3 is window + GL context + hide-cursor + audio device. **All game
input is asyncinput**. No SDL keyboard or mouse is read for gameplay.

## Rules

- 4×4 grid, 8 pairs.
- Two local players, strict turn alternation.
- A turn is two opens. Match → those cards stay up and that player
  scores 1, then the turn still passes.
- Mismatch → both flip closed after a short hold, turn passes.
- Most matches wins. 4–4 is a tie.
- `R` reshuffles. `Esc` / `Q` quits.

## Build

```
sudo apt install cmake ninja-build libsdl3-dev libgl1-mesa-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/memory
./build/biscuit
```

Headless checks (no window):

```
./build/memory --selftest          # writes preview.bmp (relative to cwd)
./build/memory --selftest out.bmp  # or wherever you want it
./build/memory --dump-bmp board.bmp
./build/biscuit --selftest         # writes biscuit.bmp (relative to cwd)
```

`--selftest` writes next to the caller, not to a fixed directory, so it works
in any checkout. `ctest` runs both self-tests in a scratch directory as
`test_selftest_paths` to keep it that way.


## Library, not a framework

`ame` is a **static library**. You own init. Nothing opens a window, starts
audio, or spawns a logic thread until your code says so.

```c
#include <ame.h>

ame_settings S;
ame_settings_load_file(&S, "settings.yaml");   /* runtime YAML subset */

ame_app app;
ame_app_open(ame_app_size(ame_app_title(ame_app_reset(&app), "Game"),
                          ame_settings_get_i(&S, "window.width", 1280),
                          ame_settings_get_i(&S, "window.height", 720)));
/* Or skip ame_app and call ame_gl_load(your_get_proc) after your own window. */

ame_logic L;
ame_logic_reset(&L);
ame_logic_rate(&L, ame_settings_get_f(&S, "logic.hz", 1000.f));
L.step = my_fixed_step;   /* YOU implement physics — not in the library */
ame_logic_start(&L);      /* optional; or pump on the main thread */

/* main: copy snap → draw → ame_app_swap */
ame_logic_stop(&L);
ame_app_close(&app);
```

Physics is **not** an engine module. Biscuit’s solver lives under
`examples/biscuit/physics.*`. The library gives geo queries, pools, batch
renderer, audio synth, settings, and the logic-thread helper.

### Biscuit loop (after this branch)

| Mode | Who steps physics | How rate is chosen |
|------|-------------------|--------------------|
| `logic.enabled: true` (default) | `ame_logic` thread calls `bf_logic_step` | `logic.hz` in `settings.yaml` (default 1000) |
| `logic.enabled: false` | main thread `bf_tick` accumulator | same `logic.hz` / `bf_set_fixed_dt` |
| `--selftest` | main thread only | compile-time default |

Render always: `bf_snapshot_latest` (seqlock copy-out) → `bf_view_draw`.

Edit `examples/biscuit/settings.yaml` without rebuilding.

## Biscuit Fuel

Side-view 2.5D remake of the Brackeys 2025.2 jam game. Same camera as
Memory (ortho, looking down −Z onto XY, Y up). **No Box2D.** The car is
an AABB chassis on **two circle wheels** with **spring-damper struts**
(lateral wheel-joint constraints, motor through contact). Human on foot
can jump / wall-jump. `E` switches when close.

- `W/S` gas, `A/D` yaw, `Shift` boost (burns biscuit fuel)
- Cookie pickups refuel; cookie mines explode; saws cut
- Reach the big biscuit on the right
- `Enter` / `Space` advances the intro; `R` respawns

The course is cubic Beziers in `assets/level/biscuit.json` (ramps, a gap,
a semiloop after the goal, background hills). `tools/level_to_c.py`
tessellates a 3D ribbon (OBJ) and emits C (`build/gen/level_gen.c`) at
build time. Runtime collision is the sampled one-sided segments plus AABB
wall/shelf; wheels stay circles. The first stretch is flat y=0 so the
spawn / fuel-at-x=8 corridor is unchanged.

Default `./build/memory` is unchanged.

Net (loopback, server-authoritative). Default `./build/memory` is still
local hotseat. Three processes:

```
./build/memory --listen 4242 --seed 42
./build/memory --connect 127.0.0.1 4242
./build/memory --connect 127.0.0.1 4242
```

The server owns the board and turn. Clients send card-open by index.
Face-down pairs are hidden on the wire. A client drop forfeits to the
peer. `ctest` `test_net` plays a full loopback game and a drop case.

## Input (required)

asyncinput reads `/dev/input/event*` (and optionally `/dev/input/mice`).
Your user needs the `input` group:

```
sudo usermod -aG input $USER
# log out and back in
```

There is **no SDL-input fallback**. If raw devices cannot be opened the
game still runs and draws `NO INPUT - ADD USER TO INPUT GROUP`.

## Loop (this game)

- asyncinput **callback** (reader thread): move software cursor, 2D
  hit-test, hover, card-open, `R` / `Esc`.
- `SDL_AppIterate` **update then render**: flip tweens, mismatch hold
  timer, drain events (SFX), one GL batch.
- No 1000 Hz logic thread in this slice.

## Layout

Engine is mongoose-shaped (`include/ame` + matching `src`). Games sit in
`examples/` like mongoose examples; Biscuit Fuel inside that uses the
brackeysjam tree (`main` / `app` / `config` / `gameplay` / `entities` /
`render`).

```
include/ame/          engine public API
  handle.h            packed (index, generation); generation 0 = invalid
  pool.h              spawn / deferred despawn
  events.h            bounded queue + subscribe/drain
  geo.h               AABB / OBB / ray / tri / circle queries (no solver, no BVH)
  audio.h             synth mixer (callback only mixes)
  math.h              v2/v3/v4 / quat / m3/m4 / Transform (Unity names; cglm backend)
  camera.h            SETUP camera: ortho, perspective, look-at
  gfx.h               SETUP pipeline + HOT batch (ONE struct pointer per call)
  mesh.h              CPU mesh + GPU upload/draw
  obj.h               Wavefront OBJ → ame_mesh (no Flecs)
  dialogue.h          mongoose dialogue runtime + YAML→C registry
  app.h               SDL window/GL/audio host (chainable SETUP)
  coords.h            Y-up world / TMX Y-down helpers
  tilemap.h           Tiled JSON (tmj), Y-up GIDs, solid AABBs
  time.h              Unity Time (delta, scale, unscaled)
  prefab.h            name → spawn callback (handles, no Flecs)
  audio_ray.h         stereo gains + AABB occlusion (no Box2D)
  log.h               LOGD (DEBUG only)
  debug.h             Unity Debug.DrawLine / DrawRay / circle
  text.h              5x7 atlas font, one quad per glyph
  input.h             asyncinput wrapper
  gl.h                function-pointer GL loader
  memory.h            Memory simulation (no GL)
  net.h               framed TCP (u16le length + type + payload)
  memnet.h            Memory protocol + dedicated server / thin client
src/                  matching engine .c files + font5x7.inc
examples/memory/      Memory (mongoose example)
  main.c              thin SDL callbacks
  app.c / app.h       window, asyncinput, net listen/connect
  config.h
  render/mem_draw.c   atlas, cards, cursor, HUD
examples/biscuit/     Biscuit Fuel (jam layout)
  main.c              thin SDL callbacks
  app.c / app.h       window + iterate
  config.h            title, spawn, camera height, substep
  input.c             asyncinput → hold/request
  abilities.c         boost / fuel use / jump numbers
  gameplay.c          tick / snapshot / course from generated mesh
  physics.c           circle wheels, AABB walls, track segs, struts (no Box2D)
  triggers.c          pickups, mines, saws, goal
  dialogue_manager.c  intro lines (see dialogues/)
  ui.c                HUD
  entities/car.c      chassis + round wheels
  entities/human.c    on-foot AABB
  render/pipeline.c   atlas, world draw
  tools/dialogue_yaml_to_c.py
assets/level/         bezier JSON + committed OBJ
tools/level_to_c.py   JSON → OBJ + C (CMake runs this into build/gen)
dialogues/            intro YAML (embedded copy in gameplay.c)
tests/                test_pool, test_events, test_geo, test_math, test_audio,
                      test_memory, test_net, test_biscuit, test_coords, test_obj,
                      test_dialogue, test_batch, test_app, test_time,
                      test_prefab, test_tilemap, test_audio_ray
lean/                 Lean 4 model (handles, pools, events, geo+circle-seg,
                      90° yaw, Memory rules)
external/asyncinput
external/cglm         recp/cglm v0.9.6 headers (wrapped by math.h)
```

SETUP objects (`ame_camera`, `ame_pipeline`, `ame_font`) are mutated in
place and the same pointer is returned, so initialisation can chain.

Static world meshes (Biscuit level ribbon) upload once and draw with
`ame_pipeline_draw_mesh` — same shader/program as the dynamic batch, not a
second pass.

HOT renderer rule: every batch push / flush / font draw takes **one
struct pointer** (the pipeline lives inside the args). No multi-arg
HOT signatures — call sites use a compound literal:

```c
ame_batch_xy_rect(&(ame_batch_xy_rect_args){
    .p = pipe, .x = 0, .y = 0, .z = 0, .w = 1, .h = 1,
    .uv = white, .color = one
});
```

Gameplay owns the snapshot (`BfSnap` / `MemSnap`); render only reads it.
Sim modules never include GL headers. Cards live in an `ame_pool`
(handles with generations; optional free-list via `ame_pool_bind_fast`).
Gameplay pushes `MEM_EV_OPEN` / `MATCH` / `MISMATCH` / `TURN` / `WIN`;
the main thread drains those into synth cues (flip click, match dyad,
miss, win).

Headless benches (optional, default ON):

```
./build/bench_core
```

## Lean 4 model

A mathlib-free formal model of the engine core lives in `lean/`:

```
export PATH="$HOME/.elan/bin:$PATH"
cd lean && lake build
```

It proves handle generation, deferred despawn, the event FIFO,
AABB picking, integer circle-vs-segment, 90° yaw, and Memory rules
(match scores and passes the turn, mismatch closes, click-during-resolve
is ignored). Quats and perspective live in C (`ame/math.h`, `ame/camera.h`).

Kernels (handle pack, AABB XY, pool spawn, Memory click/resolve)
are pretty-printed to `generated/ame_gen.{h,c}` as a reference:
`cd lean && lake exe ame-gen ../generated`.
