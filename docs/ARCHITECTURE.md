# A Mongoose Engine – Architecture

This document describes the high-level architecture of the engine and how its subsystems interact.

Overview
- Language: C for the engine and examples, with a small C++ file for Box2D integration.
- Platform: SDL3 for windowing, timing, GL context, and app lifecycle (SDL_AppInit/Event/Iterate/Quit).
- Rendering: OpenGL 4.5 core profile; simple single-pass shader for sprites and tiles; batching via VBOs.
- Input: libasyncinput for low-latency input (particularly mouse/keyboard) in a callback style.
- ECS: flecs for entity/component/system management in examples that opt-in.
- Physics: Box2D via a thin C++ bridge (src/physics.cpp) exposed to C APIs used by examples.
- Audio: Engine audio mixer (src/audio.c) with optional spatialization helper (src/audio_ray.c); Opus decoding for assets.
- Tilemaps: Simple loader and mesh/UV generators (src/tilemap.c) supporting Tiled assets.
- Camera: Pixel-perfect orthographic camera helper (src/camera.c).
- Build: CMake; third_party vendored for glad and flecs; Box2D via FetchContent.

Directory structure (key parts)
- src/
  - camera.c: Pixel camera and viewport helpers.
  - ecs.c: Thin wrapper around flecs world lifecycle.
  - tilemap.c: TMJ parsing, CPU mesh building, UV mesh building, and atlas helpers.
  - audio.c: Mixer and device sync; audio source abstraction.
  - audio_ray.c: Simple occlusion/gain/pan calculation based on world geometry.
  - physics.cpp: Box2D bridge for creating worlds, bodies, raycasts, and stepping.
  - gl_loader.c, stb headers, and other helpers.
- examples/
  - kenney_pixel-platformer/: A self-contained example that exercises input/ECS/physics/render/audio.
  - other examples: isolated demos for audio, dialogue, line drawing, etc.
- third_party/
  - glad/: OpenGL loader.
  - flecs/: ECS library (C/C++ with C bindings).
- docs/: Documentation, including this architecture doc and ROADMAP.

Runtime model
- Main thread:
  - SDL3 App lifecycle functions drive initialization (window/GL), event polling, and the render loop.
  - Rendering uses a single shader program with a few VBOs:
    - Dynamic VBOs for player/temporary geometry.
    - Static VBOs for tilemap positions and UVs.
  - The render path binds uniform state (resolution, camera) and draws tiles first, then sprites.
- Logic thread:
  - The Pixel Platformer example spawns a logic thread after world initialization.
  - The logic thread advances the ECS world at a fixed time step (fixed_dt) and steps physics.
  - It writes a small set of gameplay state to atomics (player position/frame, camera params) for the render thread to consume.
- Audio thread:
  - A dedicated thread syncs audio source state to the device/mixer at a high cadence.
  - Spatialization parameters (pan/gain) are computed from atomics and physics queries in systems.

Synchronization
- Cross-thread data exchange is done via atomic variables for small state (positions, frames, flags) to avoid heavy locking.
- Input state is captured in the asyncinput callback and stored atomically (left/right/jump), then mirrored to ECS input components each tick.
- Larger resources (meshes/textures) are created on the main thread; examples avoid hot-swapping them across threads.

Input path
- libasyncinput delivers events via a callback (non-blocking).
- The callback updates atomic booleans/ints; the logic system derives move_dir and jump edges from atomics.
- Quit requests (Esc/Q) set an atomic flag observed by all threads.

Rendering path
- GL 4.5 core; VAO configured once; separate VBOs for pos/color/uv.
- Tilemap: static position and UV VBOs built from TMJ at load time.
- Sprites: a simple quad draw using dynamic VBOs, with nearest filtering and no post-processing.
- One shader program (vertex + fragment) drives both tiles and sprites via a uniform flag (u_use_tex) and shared attributes.

Physics path
- Box2D world created with gravity and fixed time step.
- Bodies for dynamic entities (e.g., player) and static colliders derived from tilemaps.
- Ground checks use narrow raycasts; motion integrates via set velocity and jump impulse heuristics.

Audio path
- Audio mixer maintains a small set of sources (music, ambient, SFX) with gain/pan.
- Opus assets are decoded at load time; playback state is updated in the audio thread.
- Spatialization helper computes per-frame pan/gain from listener/source positions and basic occlusion.

ECS layout (examples)
- Components: CInput, CPhysicsBody, CGrounded, CSize, CAnimation, CAmbientAudio, CCamera, CTilemapRef, CTextures, CAudioRefs.
- Systems: Input gather, ground check, movement/jump, camera follow, animation, post-state mirror, audio update.

Threading summary
- Threads: main (render/event), logic (ECS/physics), audio (mixer sync).
- Communication: atomics for small state; initialization and teardown coordinated from main.

Error handling & logging
- Non-critical diagnostics should be wrapped in a DEBUG-only macro (LOGD) to avoid Release spam.
- SDL_Log can be used for critical errors that must reach users; otherwise prefer LOGD in examples.

Extending the engine
- New subsystems should follow the existing pattern: clear C APIs in src/, initialized from examples or an app layer.
- Keep the render path simple (single pass) unless there is a strong need; prefer batching over multiple passes.
- Respect the plain-ASCII rule for C strings and avoid noisy logging in examples.

See also
- README.md for quickstart and guidelines for AI agents.
- docs/ROADMAP.md for project direction and phased evolution.
