#pragma once

// Unity-like façade public API (MVP)
// Names mirror Unity C# except for MongooseBehaviour branding.

#include <string>
#include <cstdint>
#include <vector>
#include <type_traits>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>
#include <flecs.h>

struct ecs_world_t; // from flecs

namespace unitylike {

class GameObject;
class Transform;
class MongooseBehaviour;

// Internal script host component stored on entities that have scripts
struct ScriptHost {
    std::vector<MongooseBehaviour*> scripts;
    bool awoken = false;
    bool started = false;
};

// Internal: component id for ScriptHost (registered by Scene)
extern ecs_entity_t g_comp_script_host;
// Internal: register an entity as having scripts (used by AddScript)
void __register_script_entity(ecs_world_t* w, std::uint64_t e);

class Scene {
public:
    // Create a façade Scene over an existing Flecs world (owned by C core)
    explicit Scene(ecs_world_t* world);

    // Factory
    GameObject Create(const std::string& name = "");
    void Destroy(GameObject& go);

    // Tick
    void Step(float dt);
    void StepFixed(float fdt);

    // Access underlying world
    ecs_world_t* world() const { return world_; }
private:
    ecs_world_t* world_ = nullptr; // not owned
};

class GameObject {
public:
    using Entity = std::uint64_t; // ecs_entity_t compatible
    GameObject() = default;
    GameObject(Scene* scene, Entity e) : scene_(scene), e_(e) {}

    bool activeSelf() const; // TODO: map to enable/disable
    void SetActive(bool v);

    const std::string& name() const; // backed by an internal cache
    void name(const std::string&);

    template<typename T, typename... Args>
    T& AddComponent(Args&&...);
    template<typename T>
    T* TryGetComponent();
    template<typename T>
    T& GetComponent();

    template<typename T, typename... Args>
    T& AddScript(Args&&...);

    Transform& transform();

    Entity id() const { return e_; }
    Scene* scene() const { return scene_; }
private:
    Scene* scene_ = nullptr;
    Entity e_ = 0;
    mutable std::string name_cache_;
};

class Transform {
public:
    // Internal: constructs a Transform view bound to a specific owner GameObject
    explicit Transform(GameObject owner) : owner_(owner) {}

    glm::vec3 position() const;
    void position(const glm::vec3& p);
    glm::quat rotation() const;
    void rotation(const glm::quat& q);
    glm::vec3 localScale() const;
    void localScale(const glm::vec3& s);
private:
    GameObject owner_;
};

class MongooseBehaviour {
public:
    virtual ~MongooseBehaviour() = default;
    virtual void Awake() {}
    virtual void Start() {}
    virtual void Update(float deltaTime) {}
    virtual void FixedUpdate(float fixedDeltaTime) {}
    virtual void LateUpdate() {}
    virtual void OnDestroy() {}

    GameObject& gameObject() { return owner_; }
    Transform& transform();

    void __set_owner(const GameObject& go) { owner_ = go; }
private:
    GameObject owner_{};
};

namespace Input {
    bool GetKey(int key);
    bool GetKeyDown(int key);
    bool GetKeyUp(int key);
}

namespace Time {
    float deltaTime();
    float fixedDeltaTime();
    float timeSinceLevelLoad();
}

class Rigidbody2D {
public:
    explicit Rigidbody2D(GameObject owner) : owner_(owner) {}
    glm::vec2 velocity() const;
    void velocity(const glm::vec2& v);
    bool isKinematic() const;
    void isKinematic(bool v);
private:
    GameObject owner_;
};

// Template implementations

// Note: The façade only supports a small set of component types in the MVP.
// AddComponent/GetComponent/TryGetComponent are specialized via if constexpr
// for Transform and Rigidbody2D component views.

template<typename T, typename... Args>
T& GameObject::AddComponent(Args&&...) {
    static_assert(std::is_same_v<T, Transform> || std::is_same_v<T, Rigidbody2D>,
                  "AddComponent<T>: MVP only supports Transform and Rigidbody2D");
    ecs_world_t* w = scene_->world();
    // Ensure underlying component ids are registered
    extern void ensure_components_registered(ecs_world_t*);
    ensure_components_registered(w);

    if constexpr (std::is_same_v<T, Transform>) {
        // Create or update AmeTransform2D on the entity if missing
        AmeTransform2D tr = {0.0f, 0.0f, 0.0f};
        if (auto* cur = (AmeTransform2D*)ecs_get_id(w, (ecs_entity_t)e_, g_comp.transform)) {
            tr = *cur;
        }
        ecs_set_id(w, (ecs_entity_t)e_, g_comp.transform, sizeof(AmeTransform2D), &tr);
        return transform();
    } else if constexpr (std::is_same_v<T, Rigidbody2D>) {
        AmePhysicsBody body = {0};
        if (auto* cur = (AmePhysicsBody*)ecs_get_id(w, (ecs_entity_t)e_, g_comp.body)) {
            body = *cur;
        }
        ecs_set_id(w, (ecs_entity_t)e_, g_comp.body, sizeof(AmePhysicsBody), &body);
        static thread_local Rigidbody2D rb{ GameObject() };
        rb = Rigidbody2D{ *this };
        return rb;
    }
}

template<typename T>
T* GameObject::TryGetComponent() {
    static_assert(std::is_same_v<T, Transform> || std::is_same_v<T, Rigidbody2D>,
                  "TryGetComponent<T>: MVP only supports Transform and Rigidbody2D");
    ecs_world_t* w = scene_->world();
    extern void ensure_components_registered(ecs_world_t*);
    ensure_components_registered(w);

    if constexpr (std::is_same_v<T, Transform>) {
        if (ecs_get_id(w, (ecs_entity_t)e_, g_comp.transform)) {
            static thread_local Transform t{ GameObject() };
            t = Transform{ *this };
            return &t;
        }
        return nullptr;
    } else if constexpr (std::is_same_v<T, Rigidbody2D>) {
        if (ecs_get_id(w, (ecs_entity_t)e_, g_comp.body)) {
            static thread_local Rigidbody2D rb{ GameObject() };
            rb = Rigidbody2D{ *this };
            return &rb;
        }
        return nullptr;
    }
}

template<typename T>
T& GameObject::GetComponent() {
    T* p = TryGetComponent<T>();
    // For MVP, auto-add Transform if requested but missing; Rigidbody2D must be added explicitly
    if (!p) {
        if constexpr (std::is_same_v<T, Transform>) {
            return AddComponent<Transform>();
        }
    }
    // If still null, this is a logic error for the caller
    // Using a simple fallback to AddComponent for Rigidbody2D as well for now
    if (!p) {
        return AddComponent<T>();
    }
    return *p;
}

template<typename T, typename... Args>
T& GameObject::AddScript(Args&&... args) {
    // Ensure ScriptHost component exists on this entity
    ecs_world_t* w = scene_->world();
    ScriptHost host_local;
    ScriptHost* host = (ScriptHost*)ecs_get_id(w, (ecs_entity_t)e_, g_comp_script_host);
    if (!host) {
        host_local = ScriptHost{};
        ecs_set_id(w, (ecs_entity_t)e_, g_comp_script_host, sizeof(ScriptHost), &host_local);
        host = (ScriptHost*)ecs_get_id(w, (ecs_entity_t)e_, g_comp_script_host);
    }
    // Create script and attach
    T* script = new T(std::forward<Args>(args)...);
    script->__set_owner(*this);
    host->scripts.push_back(script);
    __register_script_entity(w, e_);
    return *script;
}

inline Transform& MongooseBehaviour::transform() { return owner_.transform(); }

} // namespace unitylike
