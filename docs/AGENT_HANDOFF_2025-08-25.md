Agent Handoff Notes – unitylike_box2d_car

Context snapshot (2025-08-25)
- Repo: a_mongoose_engine
- Example target: examples/unitylike_box2d_car
- Platform: Linux (Arch), shell zsh, SDL3 + OpenGL (glad)

High-level status
- The unitylike_box2d_car demo runs using SDL3’s callback app model with an OpenGL 4.5 core context (glad). It uses the Unity-like C++ facade (Scene, GameObject, MongooseBehaviour, Camera, SpriteRenderer) over the C engine. Rendering is executed by ame_rp_run_ecs reading SpriteRenderer data.
- Physics: Box2D world created via ame_physics_world_create, stepped in FixedUpdate by ame_physics_world_step. Car built from a dynamic chassis, two dynamic wheel bodies, and two b2WheelJoint suspensions with motors enabled for propulsion.
- Input: local asyncinput wrapper (examples/unitylike_box2d_car/input_local.*) maps keys to atomic -1/0/1 signals for move (W/S or Up/Down) and yaw/torque (A/D or Left/Right), plus quit (Esc/Q). input_begin_frame must be called once per frame.
- Camera: CarCameraController behaviour smooth-follows the car using exponential smoothing, sets zoom, and maintains viewport on resize via facade Camera get()/set().
- Visuals: A tiny procedural GL atlas (64x32) built at runtime with 3 regions: a circle (wheel), a noise tile (obstacles), and a 1x1 solid texel (tintable). UVs are computed with v0=top, v1=bottom to match the SpriteRenderer convention.

Key files (example)
- examples/unitylike_box2d_car/main.cpp: SDL3 app lifecycle, GL init, world/scene creation, fixed-step accumulator, render loop calling ame_rp_run_ecs.
- examples/unitylike_box2d_car/GameManager.h (CarGameManager): sets up physics world, procedural atlas, ground, car (CarController), camera (CarCameraController), and spawns a few static obstacles over early fixed frames.
- examples/unitylike_box2d_car/CarController.h: creates Box2D bodies (chassis/wheels), connects via b2WheelJoint, applies motor speed from input, syncs visual GameObjects’ Transform to Box2D poses.
- examples/unitylike_box2d_car/CarCameraController.h: ensures a Camera component, smooth-follows target, updates AmeCamera and viewport.
- examples/unitylike_box2d_car/input_local.{h,cpp}: asyncinput bindings and per-frame edge computation.

Pitfalls encountered (and lessons)
- SDL3 + GL context setup and viewport sizing
  - Set core profile 4.5, create/make current, then load GL via gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress). Call glViewport on init and in SDL_EVENT_WINDOW_RESIZED using the current drawable size. Update Camera viewport concurrently.
- Fixed timestep and clamped dt
  - Accumulator-based loop with fixedTimeStep=1/60f; clamp frame dt to <=0.25s to avoid spiral of death. Call scene->StepFixed in while(accumulator>=fixed) then scene->Step(dt) once.
- Physics then visuals
  - Step physics first in FixedUpdate; then mirror Box2D body positions/angles to GameObject Transform in the same step for consistent rendering.
- UV orientation vs GL texture coordinates
  - SpriteRenderer expects v0=top and v1=bottom; compute atlas UVs accordingly when uploading raw pixel buffers.
- Input edges require per-frame begin
  - input_begin_frame() computes just-pressed edges and should be called once each iterate.

Implementation details (selected)
- GL/Render
  - glad GL loaded once after context creation. Clear + ame_rp_run_ecs(world) each frame; nearest filtering on atlas; pixel-perfect alignment relies on integer-friendly sizes and the facade Camera.
- Physics
  - AmePhysicsWorld wraps b2World. CarController configures two b2WheelJoints with enableMotor=true, motorSpeed scaled by input, stiffness/damping derived from frequency/damping parameters.
- Camera
  - CarCameraController computes smooth factor = 1 - exp(-smooth * dt), lerps toward target, writes AmeCamera via Camera facade get()/set(), and keeps viewport in sync with window size changes.
- Input
  - A/D (or Left/Right): yaw_dir -> torque on chassis. W/S (or Up/Down): move_dir -> wheel motor speed. Esc/Q sets quit flag.

How to build and run
- From repo root:
  - mkdir -p build && cd build
  - cmake .. -DCMAKE_BUILD_TYPE=Debug
  - cmake --build . -j
  - ./examples/unitylike_box2d_car/unitylike_box2d_car

What you should see
- A car with two visible wheels on a ground strip; A/D keys roll/torque the car, W/S drive forward/back via wheel motors. Camera follows smoothly. Over the first few fixed steps, a handful of static obstacles appear ahead.
- Resizing the window updates the viewport and maintains correct framing.

Next actions (prioritized)
1) Physics tuning and stability
   - Revisit b2WheelJoint stiffness/damping and motor torque to reduce jitter and wheel bounce on obstacles. Consider using Box2D’s SetSpringFrequencyHz/SetSpringDampingRatio if upgrading API usage.
2) Camera framing improvements
   - Add horizontal velocity-based look-ahead; clamp vertical offset to reduce sea-sickness. Optional speed-based zoom.
3) Renderer diagnostics
   - Add optional draw-call counter and simple overlay to validate batching in ame_rp_run_ecs.
4) Input improvements
   - Support gamepad axes/buttons and a tiny remap table; optionally surface a façade Input API.
5) Assets
   - Replace procedural atlas with loaded textures or a texture array; keep nearest filtering and padding to avoid bleeding.

Search hints
- SDL/GL init: grep -R "SDL_GL_SetAttribute\|gladLoadGL" -n examples/unitylike_box2d_car
- Physics world/step: grep -R "ame_physics_world_\|b2WheelJoint" -n examples/unitylike_box2d_car src/
- Camera facade: grep -R "CameraController\|camera->viewport\|ame_camera_set_target" -n .

Notes for future agents
- Respect user edits signaled by tooling: if a patch result says "This update includes user edits!", do not revert them.
- Keep edits localized; follow existing patterns and conventions in the example and engine.
- When editing code, prefer precise diffs; avoid truncating code in search/replace patches.

