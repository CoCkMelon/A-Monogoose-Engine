# Unity-like C++ API Reference

**A Mongoose Engine - cpp/unitylike**

Complete API reference for the Unity-style C++ facade over Flecs ECS and A Mongoose Engine's C core.

---

## Table of Contents

1. [Core Classes](#core-classes)
2. [Scene Management](#scene-management)
3. [GameObject](#gameobject)
4. [Transform](#transform)
5. [Components](#components)
6. [Physics2D](#physics2d)
7. [Scripting](#scripting)
8. [Input](#input)
9. [Time](#time)
10. [Audio](#audio)
11. [Utilities](#utilities)

---

## Core Classes

### Scene

Container for GameObjects and ECS world. Manages entity lifecycle and drives script execution.

```cpp
namespace unitylike {
    class Scene {
    public:
        explicit Scene(ecs_world_t* world);
        ~Scene();
        
        // Factory
        GameObject Create(const std::string& name = "");
        void Destroy(GameObject& go);
        GameObject Find(const std::string& name);
        
        // Update
        void Step(float deltaTime);
        void StepFixed(float fixedDeltaTime);
        
        // Access
        ecs_world_t* world() const;
    };
}
```

**Usage:**
```cpp
ecs_world_t* world = ecs_init();
unitylike::Scene scene(world);

GameObject player = scene.Create("Player");
scene.Step(0.016f); // 60 FPS
```

---

## GameObject

Represents an entity in the scene. Handles components, scripts, hierarchy, and tags.

### Creation & Lifetime

```cpp
class GameObject {
public:
    // Lifetime
    bool IsValid() const;
    
    // Identity
    const std::string& name() const;
    void name(const std::string& n);
    Entity id() const;
    Scene* scene() const;
    
    // Active state
    bool activeSelf() const;
    void SetActive(bool active);
    
    // Tag & Layer
    std::string tag() const;
    void tag(const std::string& t);
    bool CompareTag(const std::string& t) const;
    int layer() const;
    void layer(int l);
};
```

### Component Management

```cpp
// Add component
template<typename T, typename... Args>
T& AddComponent(Args&&... args);

// Get component (adds if missing for Transform)
template<typename T>
T& GetComponent();

// Try get component (returns nullptr if missing)
template<typename T>
T* TryGetComponent();
```

**Supported Components:**
- `Transform`
- `Rigidbody2D`
- `Collider2D`
- `SpriteRenderer`
- `Material`
- `TilemapRenderer`
- `MeshRenderer`
- `Camera`
- `TextRenderer`

**Example:**
```cpp
GameObject player = scene.Create("Player");
auto& sprite = player.AddComponent<SpriteRenderer>();
sprite.texture(myTexture);

if (auto* rb = player.TryGetComponent<Rigidbody2D>()) {
    rb->AddForce(glm::vec2(100, 0));
}
```

### Script Management

```cpp
// Add custom script
template<typename T, typename... Args>
T& AddScript(Args&&... args);
```

**Example:**
```cpp
class PlayerController : public MongooseBehaviour {
    void Update(float dt) override {
        // Player logic
    }
};

player.AddScript<PlayerController>();
```

### Hierarchy

```cpp
// Parenting
void SetParent(const GameObject& parent, bool keepWorld = true);
GameObject GetParent() const;
std::vector<GameObject> GetChildren() const;
```

**Example:**
```cpp
GameObject parent = scene.Create("Parent");
GameObject child = scene.Create("Child");
child.SetParent(parent, true); // Keep world position
```

### Transform Access

```cpp
Transform& transform();
```

---

## Transform

Handles position, rotation, scale in 2D space. Supports hierarchy with world/local transforms.

### Properties

```cpp
class Transform {
public:
    // Local space
    glm::vec3 position() const;
    void position(const glm::vec3& p);
    
    glm::quat rotation() const;
    void rotation(const glm::quat& q);
    
    glm::vec3 localScale() const;
    void localScale(const glm::vec3& s);
    
    // World space (read-only, computed from hierarchy)
    glm::vec3 worldPosition() const;
    glm::quat worldRotation() const;
    
    // 2D rotation helpers
    float eulerAngles() const;         // Angle in radians
    void eulerAngles(float angleZ);
    
    // Direction vectors
    glm::vec2 right() const;  // Local X axis
    glm::vec2 up() const;     // Local Y axis
};
```

### Methods

```cpp
// Movement
void Translate(const glm::vec3& translation, bool relativeTo = true);
void Rotate(float angleRadians);

// Space transformation
glm::vec2 TransformPoint(const glm::vec2& localPoint) const;
glm::vec2 TransformDirection(const glm::vec2& localDir) const;
glm::vec2 InverseTransformPoint(const glm::vec2& worldPoint) const;
glm::vec2 InverseTransformDirection(const glm::vec2& worldDir) const;
```

**Example:**
```cpp
Transform& tr = player.transform();
tr.position(glm::vec3(100, 50, 0));
tr.Rotate(3.14159f / 4.0f);  // 45 degrees

glm::vec2 forward = tr.right();
tr.Translate(glm::vec3(forward.x, forward.y, 0) * speed * dt, true);
```

---

## Components

### SpriteRenderer

Renders 2D sprites with texture, color, and sorting.

```cpp
class SpriteRenderer {
public:
    // Texture
    void texture(uint32_t tex);
    uint32_t texture() const;
    
    // Size (world units)
    void size(const glm::vec2& s);
    glm::vec2 size() const;
    
    // UV coordinates
    void uv(float u0, float v0, float u1, float v1);
    glm::vec4 uv() const;
    
    // Tint color
    void color(const glm::vec4& c);
    glm::vec4 color() const;
    
    // Visibility
    void enabled(bool v);
    bool enabled() const;
    
    // Sorting
    void sortingLayer(int layer);
    int sortingLayer() const;
    void orderInLayer(int order);
    int orderInLayer() const;
    void z(float z);
    float z() const;
};
```

**Example:**
```cpp
auto& sprite = go.AddComponent<SpriteRenderer>();
sprite.texture(LoadTexture("player.png"));
sprite.size(glm::vec2(32, 32));
sprite.color(glm::vec4(1, 1, 1, 1));
sprite.sortingLayer(10);
```

### Material

Simple material with tint color.

```cpp
class Material {
public:
    glm::vec4 color() const;
    void color(const glm::vec4& c);
};
```

### TilemapRenderer

Renders tilemaps from TMX data.

```cpp
class TilemapRenderer {
public:
    void map(AmeTilemap* m);
    AmeTilemap* map() const;
    void layer(int idx);
    int layer() const;
};
```

### MeshRenderer

Custom mesh rendering with vertex data.

```cpp
class MeshRenderer {
public:
    void setData(const float* positions, const float* uvs, 
                 const float* colors, size_t vertCount);
    size_t vertexCount() const;
    const float* positions() const;
    const float* uvs() const;
    const float* colors() const;
};
```

### TextRenderer

Renders text with font and styling.

```cpp
class TextRenderer {
public:
    void text(const std::string& s);
    std::string text() const;
    
    void color(const glm::vec4& c);
    glm::vec4 color() const;
    
    void font(uint32_t fontId);
    uint32_t font() const;
    
    void size(float pixels);
    float size() const;
    
    void wrapWidth(int pixels);
    int wrapWidth() const;
};
```

### Camera

Camera component for rendering viewpoint.

```cpp
class Camera {
public:
    AmeCamera get() const;
    void set(const AmeCamera& c);
    
    float zoom() const;
    void zoom(float z);
    
    void viewport(int w, int h);
    
    glm::vec2 position() const;
    void position(const glm::vec2& xy);
    
    // Static access
    static Camera* main();
    
    // Screen/world conversion
    glm::vec2 ScreenToWorldPoint(const glm::vec2& screenPos) const;
    glm::vec2 WorldToScreenPoint(const glm::vec2& worldPos) const;
};
```

---

## Physics2D

### Rigidbody2D

2D physics body with forces, velocity, and constraints.

```cpp
class Rigidbody2D {
public:
    enum class BodyType { Dynamic, Kinematic, Static };
    
    // Velocity
    glm::vec2 velocity() const;
    void velocity(const glm::vec2& v);
    float angularVelocity() const;
    void angularVelocity(float v);
    
    // Body type
    BodyType bodyType() const;
    void bodyType(BodyType type);
    bool isKinematic() const;
    void isKinematic(bool v);
    
    // Forces (applied per frame)
    void AddForce(const glm::vec2& force);
    void AddForceAtPosition(const glm::vec2& force, const glm::vec2& position);
    void AddTorque(float torque);
    
    // Impulses (instant velocity change)
    void AddImpulse(const glm::vec2& impulse);
    void AddAngularImpulse(float impulse);
    
    // Properties
    float mass() const;
    void mass(float m);
    float gravityScale() const;
    void gravityScale(float scale);
    float drag() const;
    void drag(float d);
    float angularDrag() const;
    void angularDrag(float d);
    
    // Constraints
    enum Constraints {
        None = 0,
        FreezePositionX = 1 << 0,
        FreezePositionY = 1 << 1,
        FreezeRotation = 1 << 2,
        FreezePosition = FreezePositionX | FreezePositionY,
        FreezeAll = FreezePosition | FreezeRotation
    };
    int constraints() const;
    void constraints(int c);
};
```

**Example:**
```cpp
auto& rb = player.AddComponent<Rigidbody2D>();
rb.bodyType(Rigidbody2D::BodyType::Dynamic);
rb.gravityScale(1.0f);
rb.drag(0.1f);

// Apply jump
if (Input::GetKeyDown(SPACE)) {
    rb.AddImpulse(glm::vec2(0, 500));
}

// Apply movement force
float move = Input::GetAxis("Horizontal");
rb.AddForce(glm::vec2(move * 1000, 0));
```

### Collider2D

2D collider shape (box or circle).

```cpp
class Collider2D {
public:
    enum class Type { Box, Circle };
    
    void type(Type t);
    Type type() const;
    
    // Box collider
    void boxSize(const glm::vec2& wh);
    glm::vec2 boxSize() const;
    
    // Circle collider
    void radius(float r);
    float radius() const;
    
    // Trigger
    void isTrigger(bool v);
    bool isTrigger() const;
};
```

**Example:**
```cpp
auto& col = player.AddComponent<Collider2D>();
col.type(Collider2D::Type::Box);
col.boxSize(glm::vec2(16, 32));
col.isTrigger(false);
```

### Physics2D Namespace

Static physics queries (raycasts, overlaps).

```cpp
namespace Physics2D {
    // Raycasting
    struct RaycastHit {
        bool hit;
        GameObject gameObject;
        glm::vec2 point;
        glm::vec2 normal;
        float distance;
    };
    
    RaycastHit Raycast(const glm::vec2& origin, const glm::vec2& direction, float distance);
    std::vector<RaycastHit> RaycastAll(const glm::vec2& origin, const glm::vec2& direction, float distance);
    
    // Overlap queries
    std::vector<Collider2D*> OverlapCircle(const glm::vec2& point, float radius);
    std::vector<Collider2D*> OverlapBox(const glm::vec2& point, const glm::vec2& size, float angle);
}
```

**Example:**
```cpp
auto hit = Physics2D::Raycast(transform().worldPosition(), 
                              transform().right(), 
                              100.0f);
if (hit.hit) {
    SDL_Log("Hit: %s at distance %.2f", hit.gameObject.name().c_str(), hit.distance);
}
```

---

## Scripting

### MongooseBehaviour

Base class for custom game logic scripts.

```cpp
class MongooseBehaviour {
public:
    virtual ~MongooseBehaviour() = default;
    
    // Lifecycle callbacks
    virtual void Awake() {}
    virtual void Start() {}
    virtual void Update(float deltaTime) {}
    virtual void FixedUpdate(float fixedDeltaTime) {}
    virtual void LateUpdate() {}
    virtual void OnDestroy() {}
    
    // Collision callbacks (2D)
    virtual void OnCollisionEnter2D(const Collision2D& collision) {}
    virtual void OnCollisionStay2D(const Collision2D& collision) {}
    virtual void OnCollisionExit2D(const Collision2D& collision) {}
    
    // Trigger callbacks (2D)
    virtual void OnTriggerEnter2D(Collider2D* other) {}
    virtual void OnTriggerStay2D(Collider2D* other) {}
    virtual void OnTriggerExit2D(Collider2D* other) {}
    
    // Component callbacks
    virtual void OnEnable() {}
    virtual void OnDisable() {}
    
    // Access
    GameObject& gameObject();
    Transform& transform();
    
    // Coroutines
    void StartCoroutine(IEnumerator routine);
    void StopCoroutine(IEnumerator routine);
    void StopAllCoroutines();
};
```

### Collision2D

Collision information passed to collision callbacks.

```cpp
struct Collision2D {
    GameObject gameObject;        // The other GameObject involved
    Rigidbody2D* rigidbody;      // The other Rigidbody2D (may be null)
    Collider2D* collider;        // The other Collider2D
    glm::vec2 relativeVelocity;  // Relative velocity of collision
    // Contact points omitted in MVP
};
```

**Example Script:**
```cpp
class PlayerController : public MongooseBehaviour {
    float speed = 100.0f;
    float jumpForce = 500.0f;
    bool isGrounded = false;
    
    void Start() override {
        SDL_Log("Player controller started on %s", gameObject().name().c_str());
    }
    
    void Update(float dt) override {
        // Get input (project-specific)
        float moveX = Input::GetAxis("Horizontal");
        
        // Move
        auto& rb = gameObject().GetComponent<Rigidbody2D>();
        rb.velocity(glm::vec2(moveX * speed, rb.velocity().y));
        
        // Jump
        if (isGrounded && Input::GetKeyDown(SPACE)) {
            rb.AddImpulse(glm::vec2(0, jumpForce));
        }
    }
    
    void OnCollisionEnter2D(const Collision2D& collision) override {
        if (collision.gameObject.CompareTag("Ground")) {
            isGrounded = true;
        }
    }
    
    void OnCollisionExit2D(const Collision2D& collision) override {
        if (collision.gameObject.CompareTag("Ground")) {
            isGrounded = false;
        }
    }
};

// Usage
player.AddScript<PlayerController>();
```

### Coroutines

Yield-based async operations.

```cpp
// Yield instructions
class WaitForSeconds {
public:
    explicit WaitForSeconds(float seconds);
};

class WaitForEndOfFrame {};
class WaitForFixedUpdate {};

// Coroutine example
IEnumerator PlayerController::FireRoutine() {
    while (true) {
        Fire();
        co_yield WaitForSeconds(0.5f);
    }
}

void Start() override {
    StartCoroutine(FireRoutine());
}
```

---

## Input

**Note:** Input is project-defined. Use asyncinput library directly or create wrapper.

### Recommended Pattern

```cpp
namespace Input {
    // Keyboard
    bool GetKey(int keycode);
    bool GetKeyDown(int keycode);
    bool GetKeyUp(int keycode);
    
    // Mouse
    bool GetMouseButton(int button);
    bool GetMouseButtonDown(int button);
    bool GetMouseButtonUp(int button);
    glm::vec2 mousePosition();
    glm::vec2 mouseScrollDelta();
    
    // Axes
    float GetAxis(const char* axisName);     // Smooth
    float GetAxisRaw(const char* axisName);  // Instant
    
    // Virtual buttons
    bool GetButton(const char* buttonName);
    bool GetButtonDown(const char* buttonName);
    bool GetButtonUp(const char* buttonName);
}
```

**Example Integration (asyncinput):**
```cpp
// In your project's input.cpp
#include <asyncinput.h>

namespace Input {
    static uint8_t prev_keys[256] = {0};
    
    void Update() {
        // Called at frame start
        memcpy(prev_keys, current_keys, 256);
    }
    
    bool GetKey(int keycode) {
        return asyncinput_key(keycode);
    }
    
    bool GetKeyDown(int keycode) {
        return asyncinput_key(keycode) && !prev_keys[keycode];
    }
    
    float GetAxis(const char* axisName) {
        if (strcmp(axisName, "Horizontal") == 0) {
            float axis = 0.0f;
            if (GetKey(ASYNC_KEY_A)) axis -= 1.0f;
            if (GetKey(ASYNC_KEY_D)) axis += 1.0f;
            return axis;
        }
        return 0.0f;
    }
}
```

---

## Time

Global time information.

```cpp
namespace Time {
    float deltaTime();           // Time since last frame (variable)
    float fixedDeltaTime();      // Fixed timestep for physics
    float timeSinceLevelLoad();  // Time since scene loaded
    float time();                // Alias for timeSinceLevelLoad
    float unscaledDeltaTime();   // deltaTime ignoring timeScale
    float unscaledTime();        // time ignoring timeScale
    float timeScale();           // Time scale multiplier (1.0 = normal)
    void timeScale(float scale); // Set time scale (0.5 = half speed)
    int frameCount();            // Frames since start
}
```

**Example:**
```cpp
void Update(float dt) override {
    float t = Time::timeSinceLevelLoad();
    float offset = std::sin(t * 2.0f) * 10.0f;
    transform().position(glm::vec3(offset, 0, 0));
}
```

---

## Audio

### AudioSource

Component for playing audio.

```cpp
class AudioSource {
public:
    // Playback
    void Play();
    void PlayOneShot(uint32_t clip);
    void Pause();
    void Stop();
    bool isPlaying() const;
    
    // Audio clip
    void clip(uint32_t clipId);
    uint32_t clip() const;
    
    // Properties
    void volume(float v);
    float volume() const;
    void pitch(float p);
    float pitch() const;
    void loop(bool l);
    bool loop() const;
    
    // 3D audio
    void spatialBlend(float blend);  // 0 = 2D, 1 = 3D
    float spatialBlend() const;
    void minDistance(float d);
    float minDistance() const;
    void maxDistance(float d);
    float maxDistance() const;
};
```

**Example:**
```cpp
auto& audio = player.AddComponent<AudioSource>();
audio.clip(LoadAudioClip("jump.wav"));
audio.volume(0.8f);
audio.spatialBlend(0.0f); // 2D sound

void Jump() {
    audio.Play();
}
```

### AudioListener

Camera audio listener (one per scene).

```cpp
class AudioListener {
public:
    static AudioListener* main();
};
```

---

## Utilities

### Object Instantiation

```cpp
namespace Object {
    // Clone GameObject with all components and children
    GameObject Instantiate(const GameObject& original);
    GameObject Instantiate(const GameObject& original, const glm::vec3& position);
    GameObject Instantiate(const GameObject& original, const glm::vec3& position, const glm::quat& rotation);
    
    // Destroy with optional delay
    void Destroy(GameObject& obj, float delay = 0.0f);
    void DestroyImmediate(GameObject& obj);
    
    // Don't destroy on scene load
    void DontDestroyOnLoad(GameObject& obj);
}
```

**Example:**
```cpp
GameObject bulletPrefab = scene.Create("Bullet");
// ... configure bullet ...

// Spawn bullets
GameObject bullet = Object::Instantiate(bulletPrefab, 
                                        transform().worldPosition(),
                                        transform().worldRotation());

// Destroy after 5 seconds
Object::Destroy(bullet, 5.0f);
```

### Debug

```cpp
namespace Debug {
    void Log(const char* message);
    void LogWarning(const char* message);
    void LogError(const char* message);
    
    void DrawLine(const glm::vec2& start, const glm::vec2& end, 
                  const glm::vec4& color, float duration = 0.0f);
    void DrawRay(const glm::vec2& start, const glm::vec2& direction,
                 const glm::vec4& color, float duration = 0.0f);
}
```

### SceneManager

```cpp
namespace SceneManager {
    void LoadScene(const std::string& sceneName);
    void LoadSceneAsync(const std::string& sceneName, std::function<void()> onComplete);
    void UnloadSceneAsync(const std::string& sceneName);
    
    Scene& GetActiveScene();
    int sceneCount();
    Scene& GetSceneAt(int index);
}
```

---

## Complete Example

```cpp
#include "unitylike/Scene.h"

using namespace unitylike;

class PlayerController : public MongooseBehaviour {
    float speed = 200.0f;
    float jumpForce = 800.0f;
    int health = 100;
    
    void Start() override {
        gameObject().tag("Player");
        gameObject().layer(8);
        
        auto& rb = gameObject().GetComponent<Rigidbody2D>();
        rb.gravityScale(2.0f);
        rb.drag(0.5f);
    }
    
    void Update(float dt) override {
        float moveX = Input::GetAxis("Horizontal");
        auto& rb = gameObject().GetComponent<Rigidbody2D>();
        
        // Move
        glm::vec2 vel = rb.velocity();
        vel.x = moveX * speed;
        rb.velocity(vel);
        
        // Jump
        if (Input::GetButtonDown("Jump") && IsGrounded()) {
            rb.AddImpulse(glm::vec2(0, jumpForce));
        }
        
        // Shoot
        if (Input::GetMouseButtonDown(0)) {
            Shoot();
        }
    }
    
    void OnCollisionEnter2D(const Collision2D& collision) override {
        if (collision.gameObject.CompareTag("Enemy")) {
            TakeDamage(10);
        }
    }
    
    bool IsGrounded() {
        auto hit = Physics2D::Raycast(
            transform().worldPosition(),
            glm::vec2(0, -1),
            0.6f
        );
        return hit.hit && hit.gameObject.CompareTag("Ground");
    }
    
    void Shoot() {
        GameObject bullet = Object::Instantiate(bulletPrefab,
                                                transform().worldPosition(),
                                                transform().worldRotation());
        auto& rb = bullet.GetComponent<Rigidbody2D>();
        rb.velocity(transform().right() * 500.0f);
        Object::Destroy(bullet, 3.0f);
    }
    
    void TakeDamage(int damage) {
        health -= damage;
        if (health <= 0) {
            Die();
        }
    }
    
    void Die() {
        Debug::Log("Player died!");
        gameObject().SetActive(false);
    }
    
private:
    GameObject bulletPrefab;
};

// Main setup
int main() {
    ecs_world_t* world = ecs_init();
    Scene scene(world);
    
    // Create player
    GameObject player = scene.Create("Player");
    player.transform().position(glm::vec3(0, 100, 0));
    
    auto& sprite = player.AddComponent<SpriteRenderer>();
    sprite.texture(LoadTexture("player.png"));
    sprite.size(glm::vec2(32, 48));
    
    auto& rb = player.AddComponent<Rigidbody2D>();
    rb.bodyType(Rigidbody2D::BodyType::Dynamic);
    
    auto& col = player.AddComponent<Collider2D>();
    col.type(Collider2D::Type::Box);
    col.boxSize(glm::vec2(32, 48));
    
    player.AddScript<PlayerController>();
    
    // Game loop
    float accumulator = 0.0f;
    const float fixedDt = 1.0f / 60.0f;
    
    while (running) {
        float dt = GetFrameTime();
        accumulator += dt;
        
        // Fixed update
        while (accumulator >= fixedDt) {
            scene.StepFixed(fixedDt);
            accumulator -= fixedDt;
        }
        
        // Variable update
        scene.Step(dt);
        
        // Render
        RenderScene(scene);
    }
    
    ecs_fini(world);
    return 0;
}
```

---

## Implementation Status

See [IMPLEMENTATION_STATUS.md](IMPLEMENTATION_STATUS.md) for current implementation progress.

## Threading Model

- **Logic Thread:** Scene.Step(), Scene.StepFixed(), all MongooseBehaviour callbacks
- **Render Thread:** Read-only access to components (Transform, SpriteRenderer, etc.)
- **Audio Thread:** Read-only access to AudioSource components
- **Scripts:** Must not mutate state from render/audio threads

## Performance Tips

1. **Cache Components:** Call GetComponent in Start(), not every Update()
2. **Batch Operations:** Group similar operations to reduce ECS queries
3. **Avoid Frequent Parent Changes:** SetParent triggers transform recalculation
4. **Use Tags for Queries:** Faster than name lookups
5. **Pool Objects:** Reuse GameObjects instead of Instantiate/Destroy

---

**End of API Reference**