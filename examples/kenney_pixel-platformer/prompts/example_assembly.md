Title: Example Assembly – Build and Run the World

Goal
Guide to instantiate the world: register components, create entities/resources, register systems, and run threads that mirror the original example.

Steps
1) Create ECS world and register components
- Create world: ame_ecs_world_create(); ecs_world_t* w = ame_ecs_world_ptr(world)
- Register components: CPlayerTag, CSize, CPhysicsBody, CGrounded, CInput, CAnimation, CAmbientAudio, CCamera, CTilemapRef, CTextures, CAudioRefs
- Create named entities: World, Camera, Player

2) Load tilemap and GPU data
- Parse TMX and referenced TSX to produce layers (up to 16)
- For each layer: store AmeTilemap, columns, firstgid, atlas size; create GL R32I gid texture
- Heuristic collision layer: first non-empty tiles layer with name containing "Tiles"; fallback to first non-empty
- Expose first layer via CTilemapRef on World

3) Physics
- Create physics world: ame_physics_world_create(0, 100, fixed_dt)
- Build tilemap collision from chosen collision layer using ame_physics_create_tilemap_collision
- Create dynamic body for player at start position with size=16x16; set on Player via CPhysicsBody

4) Sprites and audio
- Create 4 simple 16x16 textures for player frames (or load from disk)
- Set CTextures on World with player[0..3]
- Init audio at 48k; load music/ambient/jump opus files; set gains
- Set CAudioRefs on World; set CAmbientAudio(x,y,base_gain)

5) Camera
- Init AmeCamera with zoom=3; set viewport (g_w,g_h); attach to Camera entity via CCamera

6) Register systems (OnUpdate order)
- SysInputGather (CInput)
- SysGroundCheck (CPhysicsBody, CGrounded, [optional] CSize)
- SysMovementAndJump (CPhysicsBody, CInput, CGrounded)
- SysCameraFollow (CCamera, CPhysicsBody from Player)
- SysAnimation (CAnimation, CPhysicsBody, CGrounded)
- SysPostStateMirror (CPhysicsBody)
- SysAudioUpdate (CAmbientAudio, CPhysicsBody from Player, CInput from Player)

7) Threads
- Logic thread: fixed timestep loop
  - Accumulate real time; run multiple steps of: ecs_progress(w, fixed_dt) then ame_physics_world_step(physics)
  - Small sleep (e.g., 0.2ms) to reduce CPU
- Audio thread: ~1ms loop
  - Apply ambient pan/gain atomics
  - If jump_sfx_request toggled, restart jump sample
  - Call ame_audio_sync_sources_refs([...])

8) Render loop (main thread)
- Compute dt, update local camera snapshot from atomics
- Clear, set shaders
- Pass 1: full-screen tile shader compositing
  - Uniforms: u_res, u_camera, u_map_size, u_tile_size, u_layer_count
  - For i in layers: bind atlas texture and gid texture; set firstgid, columns, atlas_tex_size arrays
  - Draw 3-vertex fullscreen triangle (1 draw call)
- Pass 2: sprite batch
  - Build AmeScene2DBatch: append player quad using an.frame mirrored atomic and snapped px,py
  - Upload interleaved VBO; for each texture range set sampler and draw
- Swap window

9) Shutdown
- Set should_quit=true; join logic/audio threads
- Destroy physics world, free tile/layer resources and textures, shutdown audio, GL, and SDL

Notes
- Maintain pixel-perfect snapping on camera and player sprite position (floor(x+0.5)).
- Avoid depth testing; enable alpha blending.
- Use atomics for cross-thread mirroring only; ECS/physics access remains single-threaded in logic thread.

