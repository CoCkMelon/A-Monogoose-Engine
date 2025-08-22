# A Mongoose Engine – Unity‑like C++ Facade (Draft)

Overview
- Goal: Provide a Unity‑lite, beginner‑friendly C++ API layered over the existing C core (SDL3, asyncinput, OpenGL 4.5, Flecs, Box2D, audio). Scripts use dot‑style access on objects within C++ (e.g., input.GetKey(), go.transform.position()).
- Scope (MVP): 2D only. GameObject, Transform2D, MongooseBehaviour, Input, Time, basic Physics2D wrappers, and optional SpriteRenderer. 3D, coroutines, Animator, networking, and editor are out of scope for MVP.
- Layering: Keep the C core, update when needed. Expose a separate C++ façade target that wraps ecs_entity_t and component ids from Flecs. Scripts run on the logic thread; render/audio threads remain read‑only.

Notes on C++ dot usage
- C++ supports member access with '.', so Unity‑style scripting is natural. For globals like Input/Time, expose singleton objects accessible as Input.GetKey(...) if desired; or prefer instance access via a World/Scene if avoiding hidden globals.

Core API (MVP, 2D)
- GameObject
  - Created via a World/Scene factory: GameObject go = scene.Create("Player");
  - Methods: AddComponent<T>(), GetComponent<T>(), TryGetComponent<T>(), SetActive(bool), activeSelf(), name(), setName(...)
- Transform2D
  - position(), setPosition(vec2), rotation(), setRotation(float radians), scale(), setScale(vec2)
  - Local vs world: MVP can start with world‑space only; parenting optional later
- MongooseBehaviour (script base)
  - virtual void Awake(), Start(), Update(float dt), FixedUpdate(float fdt), LateUpdate(), OnDestroy()
  - References: GameObject& gameObject(), Transform2D& transform()
- TextRenderer (data-only)
  - std::string text(); void text(const std::string&); uint32_t font(); void font(uint32_t);
  - glm::vec4 color(); void color(const glm::vec4&); float size(); void size(float);
  - int wrapWidth(); void wrapWidth(int);
  - Engine-managed heap: façade writes request_buf + request_set; C engine copies to heap and updates text_ptr.
- Time
  - float deltaTime(), float fixedDeltaTime(), float timeSinceStart()
- Physics2D (thin wrappers over existing Box2D bridge)
  - Rigidbody2D: velocity get/set
  - Collider2D: Type Box/Circle, box size/radius, isTrigger (data-only, engine creates fixtures)
- Rendering (data-only façade)
  - SpriteRenderer: texture id, uv, size, color, enabled, sortingLayer, orderInLayer, z, dirty
  - Material: tint color (RGBA, dirty)
  - TilemapRenderer: AmeTilemap* map, layer index
  - MeshRenderer: pointers to positions/uvs/colors and vertex count
  - Camera: AmeCamera wrapper (zoom, viewport, position)
  - TextRenderer: heap-managed text via request buffer; C engine updates text_ptr

Mapping to current code (examples/kenney_pixel-platformer)
- Input edges: derive from atomics gathered in asyncinput callback (see SysInputGather). Store prev states to compute GetKeyDown/GetKeyUp.
- Movement and jump: mirrors SysMovementAndJump. Rigidbody2D in façade sets velocity directly on underlying AmePhysicsBody.
- Ground check: mirrors SysGroundCheck using 3 raycasts. Façade can offer a CharacterMotor2D helper later; MVP exposes raw raycast helpers.
- Camera: Camera component stores AmeCamera; façade exposes Camera.main and follows a target; keep pixel‑perfect snapping.
- Animation: keep as a later “Animator” façade; MVP can set texture frame index directly.

Threading and safety
- Logic only mutates ECS/physics; render/audio threads read atomics. Document that MongooseBehaviour callbacks run on logic thread.
- Destruction queues to end of frame to avoid iterator invalidation.

Documentation set
- See docs/unitylike/overview.md, api.md, mapping.md, roadmap.md, and example.md for structured details.
