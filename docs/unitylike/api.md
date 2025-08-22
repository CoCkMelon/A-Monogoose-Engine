# Unity‑like C++ Façade – API (MVP)

Status: Implemented as a thin, data-only C++ layer over the existing C engine.

For current project state and pending action items, consult ../../AGENT_HANDOFF.md.

Design rule
- Match Unity C# API names and semantics where practical. The façade only manipulates ECS component data and does not perform rendering, physics, or input — those live in the C engine.
- No engine prefixes in façade types. Branding exception: MongooseBehaviour replaces MonoBehaviour.

Core types (façade)
- Scene: factory/context that owns lifecycle entry points.
- GameObject: thin handle over an ECS entity.
- Transform: 2D position/rotation/scale (world-space only in MVP).
- MongooseBehaviour: script base (Awake/Start/Update/FixedUpdate/LateUpdate/OnDestroy).
- Time: static-style access to dt/fixedDt/time.
- Rigidbody2D: velocity get/set over AmePhysicsBody.
- Collider2D: data-only collider config (Type=Box|Circle, size/radius, isTrigger, dirty).
- SpriteRenderer: data for sprites (texture id, uv, size, color, enabled, sortingLayer, orderInLayer, z, dirty).
- Material: tint color (RGBA, dirty).
- TilemapRenderer: reference to AmeTilemap* and layer index.
- MeshRenderer: pointers to client vertex data (positions/uvs/colors, count).
- Camera: wraps AmeCamera (zoom, viewport, position).
- TextRenderer: engine-managed pointer model with façade request buffer (text_ptr managed by C engine).

Scene
- GameObject Create(const std::string& name="");
- void Destroy(GameObject&);
- void Step(float dt);
- void StepFixed(float fdt);

GameObject
- bool activeSelf() const; void SetActive(bool);
- const std::string& name() const; void name(const std::string&);
- template<typename T, typename... Args> T& AddComponent(Args&&...);
- template<typename T> T* TryGetComponent(); template<typename T> T& GetComponent();
- template<typename T, typename... Args> T& AddScript(Args&&...);
- Transform& transform();

Transform (2D)
- glm::vec3 position() const;         void position(const glm::vec3&);
- glm::quat rotation() const;         void rotation(const glm::quat&);
- glm::vec3 localScale() const;       void localScale(const glm::vec3&);

MongooseBehaviour
- virtual void Awake(); Start(); Update(float); FixedUpdate(float); LateUpdate(); OnDestroy();
- GameObject& gameObject(); Transform& transform();

Time
- static float deltaTime(); static float fixedDeltaTime(); static float timeSinceLevelLoad();

Physics2D façade
- Rigidbody2D: glm::vec2 velocity() const; void velocity(const glm::vec2&);
- Collider2D: enum class Type { Box, Circle }; setters/getters for type, box size, radius, isTrigger (sets dirty internally).

Rendering façade (data only)
- SpriteRenderer: texture(uint32_t), size(vec2), uv(u0,v0,u1,v1), color(vec4), enabled(bool), sortingLayer(int), orderInLayer(int), z(float). Setters mark dirty so the C engine can react.
- Material: color(vec4). Set marks dirty.
- TilemapRenderer: map(AmeTilemap*), layer(int).
- MeshRenderer: setData(const float* pos, const float* uv, const float* col, size_t count) and accessors.
- Camera: get()/set(AmeCamera), zoom, viewport, position.
- TextRenderer: text(std::string) writes into a request buffer and sets a flag; font(uint32_t), color(vec4), size(float px), wrapWidth(int px).

Notes
- The façade is data-only. All rendering, physics body creation/fixtures, input, and resource lifetime management are performed by the C engine via systems that read/write these components.
- TextRenderer uses a heap-managed pointer model: façade sets request_buf + request_set; a C system allocates/frees heap memory and updates text_ptr.
- Collider2D, SpriteRenderer, Material include simple dirty flags for C engine systems to consume.
- Multiple instances of the same component per entity are not supported (Unity-like).
