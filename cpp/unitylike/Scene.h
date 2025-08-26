#pragma once

// Unity-like façade public API (MVP)
// Names mirror Unity C# except for MongooseBehaviour branding.

#include <string>
#include <cstdint>
#include <vector>
#include <type_traits>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/quaternion.hpp>
#include <flecs.h>

extern "C" {
#include "ame/physics.h"   // AmeTransform2D, AmePhysicsBody
#include "ame/tilemap.h"   // AmeTilemap
#include "ame/camera.h"    // AmeCamera
}

struct ecs_world_t; // from flecs

namespace unitylike {

class GameObject;
class Transform;
class MongooseBehaviour;

// Internal script host storage (managed outside ECS to avoid POD constraints)
struct ScriptHost {
    std::vector<MongooseBehaviour*> scripts;
    bool awoken = false;
    bool started = false;
};

// Internal: component id for ScriptHost (registered by Scene)
extern ecs_entity_t g_comp_script_host;
// Internal: register an entity as having scripts (used by AddScript)
void __register_script_entity(ecs_world_t* w, std::uint64_t e);
// Internal: host management (implemented in SceneCore.cpp)
ScriptHost* __get_script_host(std::uint64_t e);
void __ensure_script_host(std::uint64_t e);
void __remove_script_host(std::uint64_t e);

// Forward declaration of internal component id holder
struct CompIds {
    ecs_entity_t transform;
    ecs_entity_t body;
    ecs_entity_t scale2d;
    ecs_entity_t sprite;
    ecs_entity_t material;
    ecs_entity_t tilemap;
    ecs_entity_t mesh;
    ecs_entity_t camera;
    ecs_entity_t text;
    ecs_entity_t collider2d;
};
extern CompIds g_comp;

// Internal component PODs (façade data stored in ECS)
struct Scale2D { float sx; float sy; };
struct SpriteData { std::uint32_t tex; float u0,v0,u1,v1; float w,h; float r,g,b,a; int visible; int sorting_layer; int order_in_layer; float z; int dirty; };
struct MaterialData { std::uint32_t tex; float r,g,b,a; int dirty; };
struct TilemapRefData {
    AmeTilemap* map; // pointer to CPU-side map (layer0 data)
    int layer;       // layer index in source TMX
    // GPU resources and metadata needed for rendering
    std::uint32_t atlas_tex;
    std::uint32_t gid_tex;
    int atlas_w, atlas_h;
    int tile_w, tile_h;
    int firstgid;
    int columns;
    int map_w, map_h; // store map size to avoid dangling pointers to TMX local
};
struct MeshData { const float* pos; const float* uv; const float* col; std::size_t count; };
struct TextData { const char* text_ptr; std::uint32_t font; float r,g,b,a; float size; int wrap_px; int request_set; char request_buf[256]; };
struct Col2D { int type; float w,h; float radius; int isTrigger; int dirty; };

// Internal registration helper (defined in Components.cpp)
void ensure_components_registered(ecs_world_t* w);

class Scene {
public:
    // Create a façade Scene over an existing Flecs world (owned by C core)
    explicit Scene(ecs_world_t* world);
    ~Scene();

    // Factory
    GameObject Create(const std::string& name = "");
    void Destroy(GameObject& go);
    GameObject Find(const std::string& name);

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
    bool IsValid() const;

    // Parenting API
    void SetParent(const GameObject& parent, bool keepWorld = true);
    GameObject GetParent() const;
    std::vector<GameObject> GetChildren() const;
private:
    Scene* scene_ = nullptr;
    Entity e_ = 0;
    mutable std::string name_cache_;
};

class Transform {
public:
    // Internal: constructs a Transform view bound to a specific owner GameObject
    explicit Transform(GameObject owner) : owner_(owner) {}

    // Local space accessors
    glm::vec3 position() const;
    void position(const glm::vec3& p);
    glm::quat rotation() const;
    void rotation(const glm::quat& q);
    glm::vec3 localScale() const;
    void localScale(const glm::vec3& s);

    // World/composed accessors (read-only): computed by traversing EcsChildOf chain
    glm::vec3 worldPosition() const;
    glm::quat worldRotation() const;
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
protected:
    GameObject owner_{};
};

// NOTE: Input singleton removed. Projects should define their own input handling.

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

// Simple material with a tint color (RGBA)
class Material {
public:
    explicit Material(GameObject owner) : owner_(owner) {}
    glm::vec4 color() const;
    void color(const glm::vec4& c);
private:
    GameObject owner_;
};

// Sprite renderer: texture id, size in pixels, uv rect, tint, visibility
class SpriteRenderer {
public:
    explicit SpriteRenderer(GameObject owner) : owner_(owner) {}
    void texture(std::uint32_t tex);
    std::uint32_t texture() const;
    void size(const glm::vec2& s);
    glm::vec2 size() const;
    void uv(float u0, float v0, float u1, float v1);
    glm::vec4 uv() const; // (u0,v0,u1,v1)
    void color(const glm::vec4& c);
    glm::vec4 color() const;
    void enabled(bool v);
    bool enabled() const;
    void sortingLayer(int l);
    int sortingLayer() const;
    void orderInLayer(int o);
    int orderInLayer() const;
    void z(float z);
    float z() const;
private:
    GameObject owner_;
};

// Tilemap renderer component referencing an AmeTilemap
class TilemapRenderer {
public:
    explicit TilemapRenderer(GameObject owner) : owner_(owner) {}
    void map(AmeTilemap* m);
    AmeTilemap* map() const;
    void layer(int idx);
    int layer() const;
private:
    GameObject owner_;
};

// Mesh renderer (MVP placeholder): raw pointers to client vertex data
class MeshRenderer {
public:
    explicit MeshRenderer(GameObject owner) : owner_(owner) {}
    void setData(const float* positions, const float* uvs, const float* colors, std::size_t vertCount);
    std::size_t vertexCount() const;
    const float* positions() const;
    const float* uvs() const;
    const float* colors() const;
private:
    GameObject owner_;
};

// Text renderer façade (data only)
class TextRenderer {
public:
    explicit TextRenderer(GameObject owner) : owner_(owner) {}
    void text(const std::string& s);
    std::string text() const;
    void color(const glm::vec4& c);
    glm::vec4 color() const;
    void font(std::uint32_t id);
    std::uint32_t font() const;
    void size(float px);
    float size() const;
    void wrapWidth(int px);
    int wrapWidth() const;
private:
    GameObject owner_;
};

// Collider2D façade (data only)
class Collider2D {
public:
    enum class Type { Box = 0, Circle = 1 };
    explicit Collider2D(GameObject owner) : owner_(owner) {}
    void type(Type t);
    Type type() const;
    void boxSize(const glm::vec2& wh);
    glm::vec2 boxSize() const;
    void radius(float r);
    float radius() const;
    void isTrigger(bool v);
    bool isTrigger() const;
private:
    GameObject owner_;
};

// Camera component wrapper
auto constexpr kDefaultZoom = 3.0f; // hint only; engine decides default
class Camera {
public:
    explicit Camera(GameObject owner) : owner_(owner) {}
    AmeCamera get() const; // full struct copyout for C side configuration
    void set(const AmeCamera& c);
    float zoom() const;
    void zoom(float z);
    void viewport(int w, int h);
    glm::vec2 position() const; // returns top-left x,y
    void position(const glm::vec2& xy);
private:
    GameObject owner_;
};

// Template implementations

// Note: The façade only supports a small set of component types in the MVP.
// AddComponent/GetComponent/TryGetComponent are specialized via if constexpr
// for Transform and Rigidbody2D component views.

template<typename T, typename... Args>
T& GameObject::AddComponent(Args&&...) {
static_assert(
        std::is_same_v<T, Transform> || std::is_same_v<T, Rigidbody2D> ||
        std::is_same_v<T, SpriteRenderer> || std::is_same_v<T, Material> ||
        std::is_same_v<T, TilemapRenderer> || std::is_same_v<T, MeshRenderer> || std::is_same_v<T, Camera> ||
        std::is_same_v<T, TextRenderer> || std::is_same_v<T, Collider2D>,
        "AddComponent<T>: MVP supports Transform, Rigidbody2D, Sprite, Material, Tilemap, Mesh, Camera, Text, Collider2D"
    );
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
    } else if constexpr (std::is_same_v<T, SpriteRenderer>) {
        // Use the shared SpriteData definition declared at the top of this header
        SpriteData s{};
        s.tex = 0;
        s.u0 = 0.0f; s.v0 = 0.0f; s.u1 = 1.0f; s.v1 = 1.0f;
        s.w = 16.0f; s.h = 16.0f;
        s.r = 1.0f; s.g = 1.0f; s.b = 1.0f; s.a = 1.0f;
        s.visible = 1;
        s.sorting_layer = 0;
        s.order_in_layer = 0;
        s.z = 1.0f;
        s.dirty = 1;
        ecs_set_id(w, (ecs_entity_t)e_, g_comp.sprite, sizeof(SpriteData), &s);
        static thread_local SpriteRenderer sr{ GameObject() };
        sr = SpriteRenderer{ *this };
        return sr;
    } else if constexpr (std::is_same_v<T, Material>) {
        MaterialData m{};
        m.r = 1.0f; m.g = 1.0f; m.b = 1.0f; m.a = 1.0f;
        m.dirty = 1;
        ecs_set_id(w, (ecs_entity_t)e_, g_comp.material, sizeof(MaterialData), &m);
        static thread_local Material mat{ GameObject() };
        mat = Material{ *this };
        return mat;
    } else if constexpr (std::is_same_v<T, TilemapRenderer>) {
        TilemapRefData tr{};
        tr.map = nullptr;
        tr.layer = 0;
        tr.atlas_tex = 0;
        tr.gid_tex = 0;
        tr.atlas_w = 0; tr.atlas_h = 0;
        tr.tile_w = 0; tr.tile_h = 0;
        tr.firstgid = 0; tr.columns = 0;
        ecs_set_id(w, (ecs_entity_t)e_, g_comp.tilemap, sizeof(TilemapRefData), &tr);
        static thread_local TilemapRenderer t{ GameObject() };
        t = TilemapRenderer{ *this };
        return t;
    } else if constexpr (std::is_same_v<T, MeshRenderer>) {
        struct MeshData { const float* pos; const float* uv; const float* col; std::size_t count; } mr{nullptr,nullptr,nullptr,0};
        ecs_set_id(w, (ecs_entity_t)e_, g_comp.mesh, sizeof(mr), &mr);
        static thread_local MeshRenderer m{ GameObject() };
        m = MeshRenderer{ *this };
        return m;
    } else if constexpr (std::is_same_v<T, Camera>) {
        AmeCamera cam; ame_camera_init(&cam);
        ecs_set_id(w, (ecs_entity_t)e_, g_comp.camera, sizeof(cam), &cam);
        static thread_local Camera c{ GameObject() };
        c = Camera{ *this };
        return c;
    } else if constexpr (std::is_same_v<T, TextRenderer>) {
        struct TextData { const char* text_ptr; std::uint32_t font; float r,g,b,a; float size; int wrap_px; int request_set; char request_buf[256]; } td = { nullptr, 0, 1,1,1,1, 16.0f, 0, 0, {0} };
        ecs_set_id(w, (ecs_entity_t)e_, g_comp.text, sizeof(td), &td);
        static thread_local TextRenderer tr{ GameObject() };
        tr = TextRenderer{ *this };
        return tr;
    } else if constexpr (std::is_same_v<T, Collider2D>) {
        struct Col2D { int type; float w,h; float radius; int isTrigger; } cd = {0, 1,1, 0.5f, 0};
        ecs_set_id(w, (ecs_entity_t)e_, g_comp.collider2d, sizeof(cd), &cd);
        static thread_local Collider2D c2{ GameObject() };
        c2 = Collider2D{ *this };
        return c2;
    }
}

template<typename T>
T* GameObject::TryGetComponent() {
static_assert(
        std::is_same_v<T, Transform> || std::is_same_v<T, Rigidbody2D> ||
        std::is_same_v<T, SpriteRenderer> || std::is_same_v<T, Material> ||
        std::is_same_v<T, TilemapRenderer> || std::is_same_v<T, MeshRenderer> || std::is_same_v<T, Camera> ||
        std::is_same_v<T, TextRenderer> || std::is_same_v<T, Collider2D>,
        "TryGetComponent<T>: supported types are Transform, Rigidbody2D, Sprite, Material, Tilemap, Mesh, Camera, Text, Collider2D"
    );
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
    } else if constexpr (std::is_same_v<T, SpriteRenderer>) {
        if (ecs_get_id(w, (ecs_entity_t)e_, g_comp.sprite)) {
            static thread_local SpriteRenderer sr{ GameObject() };
            sr = SpriteRenderer{ *this };
            return &sr;
        }
        return nullptr;
    } else if constexpr (std::is_same_v<T, Material>) {
        if (ecs_get_id(w, (ecs_entity_t)e_, g_comp.material)) {
            static thread_local Material m{ GameObject() };
            m = Material{ *this };
            return &m;
        }
        return nullptr;
    } else if constexpr (std::is_same_v<T, TilemapRenderer>) {
        if (ecs_get_id(w, (ecs_entity_t)e_, g_comp.tilemap)) {
            static thread_local TilemapRenderer t{ GameObject() };
            t = TilemapRenderer{ *this };
            return &t;
        }
        return nullptr;
    } else if constexpr (std::is_same_v<T, MeshRenderer>) {
        if (ecs_get_id(w, (ecs_entity_t)e_, g_comp.mesh)) {
            static thread_local MeshRenderer mr{ GameObject() };
            mr = MeshRenderer{ *this };
            return &mr;
        }
        return nullptr;
    } else if constexpr (std::is_same_v<T, Camera>) {
        if (ecs_get_id(w, (ecs_entity_t)e_, g_comp.camera)) {
            static thread_local Camera c{ GameObject() };
            c = Camera{ *this };
            return &c;
        }
        return nullptr;
    } else if constexpr (std::is_same_v<T, TextRenderer>) {
        if (ecs_get_id(w, (ecs_entity_t)e_, g_comp.text)) {
            static thread_local TextRenderer tr{ GameObject() };
            tr = TextRenderer{ *this };
            return &tr;
        }
        return nullptr;
    } else if constexpr (std::is_same_v<T, Collider2D>) {
        if (ecs_get_id(w, (ecs_entity_t)e_, g_comp.collider2d)) {
            static thread_local Collider2D c2{ GameObject() };
            c2 = Collider2D{ *this };
            return &c2;
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
    // Ensure host exists (managed outside ECS to avoid moving non-POD types)
    ecs_world_t* w = scene_->world();
    __ensure_script_host(e_);
    ScriptHost* host = __get_script_host(e_);
    // Create script and attach
    T* script = new T(std::forward<Args>(args)...);
    script->__set_owner(*this);
    host->scripts.push_back(script);
    __register_script_entity(w, e_);
    return *script;
}


} // namespace unitylike
