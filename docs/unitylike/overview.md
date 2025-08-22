Title: Unity‑like C++ Facade – Overview

Goal
Introduce a Unity‑lite C++ API layered over AME’s C core. The C++ facade targets ergonomic, Unity‑style scripting using MongooseBehaviour while preserving access to low‑level C/C++ modules when needed.

Principles
- Thin facade: map script-friendly calls to existing ECS components/systems and engine subsystems.
- Unity naming: API names mirror Unity C# (GameObject, Transform, Input, Time, Rigidbody2D, Camera), with one branding exception: MongooseBehaviour replaces MonoBehaviour. No engine prefixes in façade types.
- 2D‑first MVP: camera, transforms, input, time, physics2D, sprites; 3D later.
- Dot‑style usage in scripts: prefer instance properties/functions accessible with '.' within MongooseBehaviour scripts. Reserve '::' for occasional globals or factory helpers.
- Single-threaded scripting: scripts run on the logic thread (Update/FixedUpdate). Renderer/audio threads remain read‑only via atomics.
- Interop friendly: keep handles (entity IDs, pointers) accessible for advanced users.

Minimum Viable API (MVP)
- GameObject (create/destroy, name, active, component access)
- Transform2D (position/rotation/scale; parenting optional in MVP)
- MongooseBehaviour (Awake/Start/Update/FixedUpdate/LateUpdate/OnDestroy)
- Time (deltaTime, fixedDeltaTime)
- Physics2D (Rigidbody2D, Collider2D data-only)
- Rendering data-only: SpriteRenderer, Material, TilemapRenderer, MeshRenderer, Camera, TextRenderer (C engine consumes)

Constraints and threading
- Logic thread drives ecs_progress and physics step at fixed_dt.
- Scripts must not mutate from render or audio threads.
- Scheduling of lifecycle events is deterministic per frame.

File layout (docs)
- docs/unitylike/overview.md (this doc)
- docs/unitylike/api.md (C++ API surface)
- docs/unitylike/mapping.md (facade↔ECS mapping)
- docs/unitylike/roadmap.md (phased plan)
- docs/unitylike/example.md (short script + setup example)
