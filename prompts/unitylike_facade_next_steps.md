# Prompts for Future LLM Agents – Unity‑like C++ Facade (MVP First)

Context
- Engine core is C with SDL3, asyncinput, OpenGL 4.5, Flecs, Box2D bridge (src/physics.cpp), audio mixer. Examples show a logic thread stepping ECS/physics and render/audio threads reading atomics.
- Objective is to add a thin C++ façade that provides Unity‑style scripting and hides ECS details for beginners.

Constraints
- MVP is 2D‑only. Do not introduce 3D, editor, networking, or heavy coroutine systems in MVP.
- Keep C core stable; implement façade as a separate C++ target linking the core.
- Scripts run on the logic thread. Never mutate ECS from render/audio threads.
- Prefer World/Scene factories over hidden globals; if adding singletons (Input/Time), document initialization order.

Deliverables (incremental)
1) Target
- Add a new CMake target: ame_unitylike (C++), which links against the existing C core and third_party as needed.

2) Types and registration
- Implement: World, GameObject, Transform2D, MongooseBehaviour, Input, Time.
- Provide static registration for façade component types to map T -> flecs id.
- Disallow multiple instances of the same component per entity.

3) Script system
- Add a ScriptHost component that stores a pointer to a type‑erased MongooseBehaviour instance.
- Provide World::RegisterScriptType<T>() and GameObject::AddScript<T>(args...).
- Add systems that iterate ScriptHost and call Awake/Start once, then Update and FixedUpdate each tick.
- Queue OnDestroy calls and entity destruction to end‑of‑frame.

4) Input edges
- Mirror asyncinput atomics into a façade frame snapshot at the start of Update.
- Provide GetKey/GetKeyDown/GetKeyUp semantics; include minimal KeyCode mapping to asyncinput/SDL where practical.

5) Physics2D wrappers
- Map Rigidbody2D to AmePhysicsBody (Box2D b2Body*). Expose velocity get/set and kinematic flag.
- Map Collider2D (box) via ame_physics_create_body parameters; circle optional later.
- Expose simple Raycast helpers mapping to ame_physics_raycast.

6) Camera and rendering hooks
- Camera component that wraps AmeCamera. Provide Camera.main and a simple Follow API.
- Optional: SpriteRenderer component (texture id, rect). Rendering should remain in render thread; façade writes atomics or ECS that render code reads.

7) Example
- Add a façade example that mirrors examples/kenney_pixel-platformer movement/jump using scripts, Input, Rigidbody2D, and Camera.

Coding guidelines
- Follow const‑correct C++ and avoid exceptions in the façade; return pointers or optional‑like behavior for TryGetComponent.
- Match existing naming where it maps to C components (e.g., AmePhysicsBody, AmeTransform2D).
- Keep logging behind DEBUG guards in C and minimal in C++.

Out of scope (for now)
- 3D rendering/camera, Animator state machines, editor UIs, networking, complex serialization. Document placeholders but do not implement.

Testing notes
- Add a small headless unit for Input edge computation if possible.
- Validate pixel‑perfect camera snapping matches platformer example.

