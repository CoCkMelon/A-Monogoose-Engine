# A Mongoose Engine – Tutorial

This tutorial walks you through installing prerequisites, building the engine, running examples, and creating your own app using both the C APIs and the Unity-like C++ facade.

The engine is a minimal 2D scaffold built around SDL3 (windowing/events), OpenGL 4.5 (rendering), PortAudio + opusfile (audio), Flecs (ECS), Box2D (physics), and an optional Unity-like C++ façade for scripting ergonomics.


## 1) Prerequisites

Install the following on your system (package names vary by distro):
- CMake >= 3.16, a C/C++ compiler (e.g., gcc/clang)
- SDL3 development headers (SDL3, SDL3_ttf for some examples, SDL3_image for tile/texture loading)
- OpenGL 4.5 capable GPU/driver
- PortAudio (portaudio-2.0 dev), libopusfile (opusfile dev)
- pthreads (typically part of libc), glad is vendored; Flecs is vendored
- Box2D is fetched automatically by CMake (no system package required)

Linux input permissions:
- Some examples optionally use libasyncinput to read /dev/input/event*. Ensure your user can read those devices (e.g., add to the input group or set udev rules), or run examples that don’t require asyncinput.

Optional environment:
- AME_AUDIO_HOST can influence PortAudio host selection: pulse, alsa, jack, etc. Example: export AME_AUDIO_HOST=pulse


## 2) Build the engine and examples

Out-of-source builds are enforced.

- Release build with examples:
  - mkdir -p build && cd build
  - cmake -DAME_BUILD_EXAMPLES=ON -DCMAKE_BUILD_TYPE=Release ..
  - cmake --build . -j

- Minimal engine-only build:
  - mkdir -p build && cd build
  - cmake -DAME_BUILD_EXAMPLES=OFF ..
  - cmake --build . -j

Notes:
- The build fetches Box2D and (by default) a sibling or remote asyncinput if present. SDL3_image is required for texture loading; SDL3_ttf is required only for text UI examples.


## 3) Run selected examples

From the build directory after a successful build (AME_BUILD_EXAMPLES=ON):

- Tilemap (procedural atlas colors):
  - ./tilemap_render

- Audio panning (sigmoid oscillator, entity moving left/right):
  - ./audio_pan_example

- Audio Opus playback (loads entire opus file to memory; loop by default):
  - ./audio_opus_example /path/to/file.opus [--no-loop]
  - Controls: Space (play/pause), L (loop), arrows (pan/gain), mouse drag for pan, R (restart)

- Audio ray tracing demo with occlusion and reflections:
  - ./audio_ray_example
  - Controls: WASD move listener, Space toggles debug rays

- Dialogue (console):
  - ./dialogue_example

- Dialogue with UI (requires SDL3_ttf and asyncinput optional):
  - ./dialogue_ui_example

- Flecs GL point cloud (2s demo):
  - ./flecs_scene

- Flecs Script scene (inline or file):
  - ./scene_script_example --expr "..." or --script file.ffs [--dump-json]

- Unity-like façade minimal C++ demo:
  - ./unitylike_minimal

- Unity-like platformer (ECS pipeline renderer):
  - ./unitylike_platformer_ecs

- Unity-like Box2D car (ECS pipeline renderer):
  - ./unitylike_box2d_car

- Unity-like pixel platformer (TMX tilemaps + physics, C++ behaviours):
  - ./unitylike_pixel_platformer


## 4) Project structure overview

- include/ame/*.h – Public C headers (audio, physics, tilemap, camera, ECS wrapper, etc.)
- src/* – Engine implementation: rendering pipeline, audio mixer, physics bridge, tilemap loaders, dialogue
- cpp/unitylike/* – Unity-like C++ façade: GameObject, Transform, Rigidbody2D, SpriteRenderer, TextRenderer, TilemapRenderer, Camera, Time
- examples/* – Standalone C and C++ demos of subsystems and façade
- docs/* – Architecture and roadmap documentation


## 5) Core engine concepts (C APIs)

- ECS wrapper (Flecs): Lightweight helpers so C demos can create worlds and register POD components.
- Rendering: Two paths
  - Immediate GL in examples (manual VBOs and shaders)
  - Engine tilemap compositor (full-screen pass sampling a GID texture + tileset atlas)
- Audio: Mixer thread via PortAudio; supports a simple oscillator and decoded Opus buffers; per-source gain/pan; stereo law is constant power
- Physics: Box2D wrapper; create worlds/bodies; raycasts; sync ECS transforms
- Tilemaps: Load TMJ or TMX (+ TSX + PNG), build GID texture and atlas for GPU compositing
- Camera: Pixel-perfect top-left orthographic helpers with integer snapping
- Dialogue: Small runtime for embedded dialogue scenes (lines, choices, triggers)


## 6) Minimal C app: window, audio tone, quit loop

```c path=null start=null
// Build with your CMake project linking SDL3::SDL3 and ame
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "ame/ecs.h"
#include "ame/audio.h"

static AmeEcsWorld* g_world = NULL;
static ecs_entity_t g_audio_id = 0;
static ecs_entity_t g_e = 0;

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
  (void)appstate; (void)argc; (void)argv;
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) return SDL_APP_FAILURE;
  if (!ame_audio_init(48000)) return SDL_APP_FAILURE;
  g_world = ame_ecs_world_create();
  ecs_world_t* w = (ecs_world_t*)ame_ecs_world_ptr(g_world);
  g_audio_id = (ecs_entity_t)ame_audio_register_component(g_world);
  g_e = ecs_entity_init(w, &(ecs_entity_desc_t){0});
  AmeAudioSource src; ame_audio_source_init_sigmoid(&src, 440.0f, 8.0f, 0.1f);
  ecs_set_id(w, g_e, g_audio_id, sizeof src, &src);
  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
  (void)appstate;
  ecs_world_t* w = (ecs_world_t*)ame_ecs_world_ptr(g_world);
  AmeAudioSource* s = ecs_get_id(w, g_e, g_audio_id);
  AmeAudioSourceRef ref = { s, (uint64_t)g_e };
  ame_audio_sync_sources_refs(&ref, s ? 1 : 0);
  return SDL_APP_CONTINUE; // press window close to exit
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *ev) {
  if (ev->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
  return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
  ame_audio_shutdown();
  ame_ecs_world_destroy(g_world);
  SDL_Quit();
}
```


## 7) Physics quick start (C)

- Create world: AmePhysicsWorld* ame_physics_world_create(gravity_x, gravity_y, fixed_dt)
- Create body: ame_physics_create_body(world, x, y, width, height, AME_BODY_DYNAMIC|KINEMATIC|STATIC, is_sensor, user_data)
- Step: ame_physics_world_step(world)
- Move: ame_physics_set_velocity(body, vx, vy)
- Raycast: ame_physics_raycast / ame_physics_raycast_all

Attach acoustic materials to bodies via user_data to influence audio occlusion (see include/ame/acoustics.h). The audio_ray helper blends distance, air absorption, and occlusion from raycasts.


## 8) Tilemap rendering (GPU GID+atlas path)

Two options:
- Simple quads: load .tmj with ame_tilemap_load_tmj, build a UV mesh with ame_tilemap_build_uv_mesh, draw with your shader and a procedural atlas from ame_tilemap_make_test_atlas_texture.
- Full-screen compositor: load a .tmx via ame_tilemap_load_tmx_for_gpu, then call ame_tilemap_render_layers(camera, screen_w, screen_h, map_w, map_h, layers[], layer_count). This draws one full-screen triangle and samples tile GIDs and atlas texels per fragment.

The compositor uses engine coordinate conventions: world is pixels, Y-up, origin bottom-left; camera uses top-left position and integer snapping for pixel perfection.


## 9) Unity-like C++ façade

The façade exposes familiar concepts: Scene, GameObject, Transform, Rigidbody2D, SpriteRenderer, Material, TilemapRenderer, TextRenderer, Collider2D, Camera, and a MongooseBehaviour base class with Awake/Start/Update/FixedUpdate/LateUpdate lifecycles.

Typical usage:
- Create a Scene scene(world)
- GameObject go = scene.Create("Player");
- go.AddComponent<Transform>().position({x,y,0});
- go.AddComponent<SpriteRenderer>().size({w,h});
- go.AddScript<MyBehaviour>();
- Drive logic with scene.Step(dt) and scene.StepFixed(fixed_dt)
- Render using the ECS pipeline: ame_rp_run_ecs(world)

See examples/unitylike_minimal (manual batching) and examples/unitylike_platformer_ecs (façade + ECS renderer) for concrete setups.


## 10) Rendering pipeline for façade/ECS

- Collects TilemapRefData and Sprite data from ECS, batches and draws them in z/layer order.
- Invoke once per frame after updating scripts:
  - ecs_world_t* w = (ecs_world_t*)ame_ecs_world_ptr(ameWorld);
  - ame_rp_run_ecs(w);

If you manage your own shader, you can also build AmeScene2DBatch and draw it yourself (see include/ame/scene2d.h and unitylike_minimal’s draw helpers).


## 11) Input

- The engine itself does not force a single input API. Many examples use a small project-local input layer (examples/unitylike_minimal/input_local.*) or asyncinput directly for low-latency devices.
- If you use asyncinput, ensure device permissions for /dev/input/event*.
- For callback-style SDL3 apps, handle SDL_AppEvent to respond to window and keyboard events.


## 12) Dialogue runtime

- Embed scenes via src/embedded_dialogues.c and optional generated sources (include/ame_dialogue_generated.h)
- At runtime: ame_dialogue_runtime_init, ame_dialogue_play_current, ame_dialogue_advance, ame_dialogue_select_choice
- UI example builds text into a texture via SDL3_ttf and draws it with a trivial textured quad shader


## 13) Audio tips

- Initialize once: ame_audio_init(sample_rate)
- Use AmeAudioSourceRef with stable IDs to preserve oscillator phase/opus cursor across frames
- Stereo law is constant power; use ame_audio_constant_power_gains if you need L/R directly
- For spatialization, compute gains with ame_audio_ray_compute (distance, occlusion, air absorption) or your own multi-ray approach (see audio_ray_example)


## 14) Troubleshooting

- Linker errors for SDL3_ttf: build examples with AME_BUILD_EXAMPLES=ON and ensure SDL3_ttf dev package is installed; some examples require it (dialogue_ui_example)
- No sound / noisy backend logs: set AME_AUDIO_HOST=pulse (or alsa/jack) and verify PortAudio host/device; check stderr for the selected host/device
- GL function pointers NULL: ensure GL 4.5 core context creation succeeded; examples use glad
- Input not working with asyncinput: check user permissions for /dev/input/event*
- TMX textures missing: verify tileset image paths relative to .tmx; the loader resolves ../ up-levels and loads via SDL_image.


## 15) Where to go next

- Read docs/ARCHITECTURE.md for threading, ECS, physics, rendering details
- Explore examples/ to learn each subsystem in isolation
- For the façade, see docs/unitylike/* and examples/unitylike_* for API mapping and patterns
- Consider adding your own systems and data components in C, and wrapping them with thin C++ convenience APIs if you prefer the façade style

