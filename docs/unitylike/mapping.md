# Façade ↔ ECS Mapping (MVP)

This document maps the Unity‑named façade classes to ECS components and the C engine systems.

Scene
- Wraps AmeEcsWorld* and exposes Step/StepFixed. Lifecycle of scripts via ScriptHost (Awake/Start/Update/FixedUpdate/LateUpdate/OnDestroy).

GameObject
- Holds ecs_entity_t id. name: Flecs entity name. activeSelf/SetActive via EcsDisabled.

Transform
- AmeTransform2D { x, y, angle }. Scale2D stored separately as Scale2D { sx, sy }.

MongooseBehaviour
- ScriptHost component stores pointers to instances; façade only orchestrates calls. Engine determines update order deterministically.

Time
- deltaTime/fixedDeltaTime from loop; stored in atomics in Time.cpp.

Rigidbody2D
- Maps to AmePhysicsBody { b2Body* }. velocity get/set call ame_physics_*.

Collider2D
- Col2D { type (0=Box,1=Circle), w,h, radius, isTrigger, dirty }.
- C engine system: when dirty or on add, (re)create Box2D fixtures accordingly; set sensor from isTrigger; clear dirty.

SpriteRenderer
- SpriteData { tex, u0,v0,u1,v1, w,h, r,g,b,a, visible, sorting_layer, order_in_layer, z, dirty }.
- C engine renderer: read SpriteData + Transform + Material + Camera; sort by (sorting_layer, order_in_layer, z); draw via batch.

Material
- MaterialData { r,g,b,a, dirty }. C engine uses tint and clears dirty after applying.

TilemapRenderer
- TilemapRefData { AmeTilemap* map, int layer } consumed by tile render pass.

MeshRenderer
- MeshData { pos*, uv*, col*, count } consumed by custom geometry renderer.

Camera
- Uses AmeCamera. Renderer reads camera (zoom/viewport/position) for matrices/pixel-perfect snapping.

TextRenderer
- TextData { text_ptr, font, r,g,b,a, size, wrap_px, request_set, request_buf[256] }.
- C engine text system: if request_set!=0, allocate new heap buffer from request_buf, set text_ptr, clear request_set; free old text_ptr on replace/destroy; render from text_ptr.

Notes
- Façade is data-only; all rendering/physics/input live in C engine systems.
- Dirty flags signal to C systems when to update caches/resources. Clear them in C after processing.
