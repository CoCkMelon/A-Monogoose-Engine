Title: Resources and Global State Exposed to Systems

Goal
Describe non-ECS resources used by systems and threads so implementation can keep tight interfaces.

Resources
- AmeEcsWorld* world
- AmePhysicsWorld* physics
- SDL_Window* window; SDL_GLContext gl
- Viewport: int g_w, g_h
- Camera mirror (atomic): float cam_x, cam_y, cam_zoom
- Player state mirrors (atomic): float player_x, player_y; int player_frame
- Input (atomic): bool left_down, right_down, jump_down; int move_dir; bool should_quit
- Audio
  - AmeAudioSource music, ambient, jump_sfx
  - Atomic: float ambient_pan, ambient_gain; bool jump_sfx_request
- Tilemap layers (array up to 16)
  - For each layer: AmeTilemap map; GLuint atlas_tex; int columns; int firstgid; GLuint gid_tex; bool is_collision; atlas size
- Rendering
  - GL programs: batch program (vs/fs) and full-screen tile program
  - Batch: AmeScene2DBatch for sprites

Contracts
- Logic thread owns ecs_progress and physics step at fixed_dt.
- Audio thread polls atomics and syncs audio sources at ~1ms cadence.
- Main thread draws: first tile pass, then sprite batch.
- Pixel-perfect camera: position snap to nearest pixel in world coordinates before uniforms.

