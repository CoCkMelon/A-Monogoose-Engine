# Facade ↔ ECS Mapping (MVP)

This document maps the Unity‑named façade classes to the underlying ECS components/systems and C core code.

Scene
- Wraps AmeEcsWorld* and owns system registration.
- Lifecycle: ecs_progress(w, dt) on logic thread; ame_physics_world_step for Box2D.

GameObject
- Holds ecs_entity_t id.
- name: Flecs entity name.
- activeSelf/SetActive: implemented via an Active tag or Flecs enable/disable.

Transform
- Backed by a C struct (e.g., AmeTransform2D { x,y,angle,sx,sy }).
- Can reuse the transform component id registered via physics.cpp helpers or a dedicated registration.

MongooseBehaviour
- Backed by a ScriptHost component storing a pointer to a type‑erased script instance.
- Systems per lifecycle phase: Awake/Start once, then Update/FixedUpdate/LateUpdate each tick.

Input
- Derived from asyncinput atomics captured in the input callback and mirrored into an ECS CInput each tick (see SysInputGather).
- GetKeyDown/GetKeyUp derived from a per‑frame snapshot captured at the start of Update.

Time
- deltaTime from ecs_iter_t->delta_time; fixedDeltaTime from the logic loop’s fixed step.

Rigidbody2D / Collider2D
- Rigidbody2D maps to AmePhysicsBody { b2Body* }.
- Collider2D created/configured through ame_physics_create_body parameters (box in MVP).
- Raycasts via ame_physics_raycast for ground checks and probes.

Rendering
- SpriteRenderer component stores texture handle (GLuint) and rect; draw via AmeScene2DBatch in the render thread.

Camera
- Wraps AmeCamera; pixel‑perfect snapping performed on logic thread (similar to SysCameraFollow). Camera.main exposed by façade.

Audio
- Audio sources are AmeAudioSource components synced in the audio thread via ame_audio_sync_sources_refs.
