# Unity‑like C++ Facade – API (MVP)

Status: Draft aligned with current C core and examples.

Design rule
- Match Unity C# API names and semantics as closely as possible. Differences only where C++ requires (templates, refs/values, no GC). No engine prefixes in façade types. Branding exception: MongooseBehaviour replaces MonoBehaviour.

Core types (façade)
- Scene: factory/context that owns the runtime and lifecycle.
- GameObject: thin handle over an entity.
- Transform: position/rotation/scale (2D semantics in MVP; 3D later).
- MongooseBehaviour: base class for scripts.
- Input: static‑style access to per‑frame key/mouse state.
- Time: static‑style access to dt/fixedDt/time.
- Rigidbody2D, Collider2D: physics wrappers.
- Camera: pixel‑perfect orthographic camera in MVP.

Scene
- GameObject Create(const std::string& name="");
- void Destroy(GameObject&);
- template<typename T> void RegisterComponent(); // bind T to an internal id
- template<typename T> void RegisterScript();    // MongooseBehaviour derivatives
- void Step(float dt);        // Update loop
- void StepFixed(float fdt);  // FixedUpdate loop

GameObject
- bool activeSelf() const;
- void SetActive(bool);
- const std::string& name() const;
- void name(const std::string&);
- template<typename T, typename... Args> T& AddComponent(Args&&...);
- template<typename T> T* TryGetComponent();
- template<typename T> T& GetComponent();
- template<typename T, typename... Args> T& AddScript(Args&&...); // T : MongooseBehaviour
- Transform& transform();

Transform (2D semantics in MVP)
- glm::vec3 position() const;         void position(const glm::vec3&);
- glm::quat rotation() const;         void rotation(const glm::quat&);
- glm::vec3 localScale() const;       void localScale(const glm::vec3&);

MongooseBehaviour
- virtual void Awake();
- virtual void Start();
- virtual void Update(float deltaTime);
- virtual void FixedUpdate(float fixedDeltaTime);
- virtual void LateUpdate();
- virtual void OnDestroy();
- GameObject& gameObject();
- Transform& transform();

Input
- static bool GetKey(KeyCode key);
- static bool GetKeyDown(KeyCode key);
- static bool GetKeyUp(KeyCode key);
- static glm::vec2 mousePosition();   // optional in MVP

Time
- static float deltaTime();
- static float fixedDeltaTime();
- static float timeSinceLevelLoad();

Physics2D
- Rigidbody2D
  - glm::vec2 velocity() const;   void velocity(const glm::vec2&);
  - bool isKinematic() const;     void isKinematic(bool);
- Collider2D
  - enum class Type { Box, Circle };
  - void SetBox(const glm::vec2& size, bool isTrigger=false);

Notes
- All of the above map to C functions/types already present (ECS wrapper, physics.cpp). The first implementation can store component data in Flecs with plain C structs and provide C++ views.
- Multiple instances of the same component per entity are disallowed (Unity‑like).
- Prefer a Scene factory (avoid hidden globals). Input/Time are static‑style for Unity ergonomics.
