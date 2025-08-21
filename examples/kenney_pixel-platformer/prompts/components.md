Title: ECS Components and Tags for AME Pixel Platformer

Goal
Define all ECS-side components and tags needed by systems. Keep data minimal, thread-safe where required, and aligned with engine APIs (ame_*).

Components
- CPlayerTag (tag)
  - Purpose: identify the player entity.
  - Fields: none (placeholder int if required by API).

- CSize
  - Fields: float w, h
  - Purpose: logical size of an entity (used by ground check rays).

- CPhysicsBody
  - Fields: b2Body* body
  - Purpose: link entity to physics body in AmePhysicsWorld.

- CGrounded
  - Fields: bool value
  - Purpose: cached grounded flag updated per tick.

- CInput
  - Fields:
    - int move_dir  // -1 left, 0 none, 1 right
    - bool jump_down
    - bool prev_jump_down
    - float coyote_timer
    - float jump_buffer
    - bool jump_trigger // set true exactly when jump is applied this tick
  - Purpose: per-tick input state + jump buffers.

- CAnimation
  - Fields: int frame; float time
  - Purpose: pick sprite frame (0 idle, 1 walk1, 2 walk2, 3 jump) and advance time.

- CAmbientAudio
  - Fields: float x, y; float base_gain
  - Purpose: world-space position of an ambient sound and base loudness.

- CCamera
  - Fields: AmeCamera cam
  - Purpose: camera state with pixel-perfect snapping and zoom.

- CTilemapRef
  - Fields: AmeTilemap* map; AmeTilemapUvMesh* uvmesh; GLuint atlas_tex
  - Purpose: expose a map layer to systems (render/physics).

- CTextures
  - Fields: GLuint player[4]
  - Purpose: player sprite textures for frames 0..3.

- CAudioRefs
  - Fields: AmeAudioSource* music, *ambient, *jump
  - Purpose: references to audio sources managed by audio thread.

Notes
- Keep components POD. Initialization/wiring of pointers happens in example_assembly.md.
- For tags, provide a struct to satisfy APIs but treat them as markers.

