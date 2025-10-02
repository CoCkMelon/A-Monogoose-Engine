#pragma once

// Unity-like façade public API (MVP)
// Names mirror Unity C# except for MongooseBehaviour branding.

#include <string>
#include <cstdint>
#include <vector>
#include <type_traits>
#include <unordered_map>
#include <typeinfo>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/quaternion.hpp>
#include <flecs.h>

extern "C" {
#include "ame/physics.h"   // AmeTransform2D, AmePhysicsBody
#include "ame/tilemap.h"   // AmeTilemap
#include "ame/camera.h"    // AmeCamera
#include "ame/audio.h"     // AmeAudioSource
}

struct ecs_world_t; // from flecs

namespace unitylike {

class GameObject;
class Transform;
class MongooseBehaviour;
class Rigidbody2D;
class Collider2D;
class AudioSource;
class AudioListener;

// Script component template - each script type gets its own component
// This allows Flecs to handle lifecycle properly
template<typename T>
struct ScriptComponent {
    T* script = nullptr;
    bool awoken = false;
    bool started = false;
    
    // Constructor hook - called when component is added
    static void ctor(void* ptr, int32_t count, const ecs_type_info_t* ti) {
        for (int i = 0; i < count; i++) {
            ScriptComponent<T>* comp = static_cast<ScriptComponent<T>*>(ptr) + i;
            comp->script = nullptr;
            comp->awoken = false;
            comp->started = false;
        }
    }
    
    // On-set hook - called when component data is set
    static void on_set(ecs_iter_t* it) {
        for (int i = 0; i < it->count; i++) {
            ScriptComponent<T>* comp = ecs_field(it, ScriptComponent<T>, 0) + i;
            if (comp->script && !comp->awoken) {
                comp->script->Awake();
                comp->awoken = true;
            }
        }
    }
    
    // Destructor hook - called when component is removed
    static void dtor(void* ptr, int32_t count, const ecs_type_info_t* ti) {
        for (int i = 0; i < count; i++) {
            ScriptComponent<T>* comp = static_cast<ScriptComponent<T>*>(ptr) + i;
            if (comp->script) {
                comp->script->OnDestroy();
                delete comp->script;
                comp->script = nullptr;
            }
        }
    }
};

// Internal script management implementation
// Register observers/systems for a script component
void __register_script_handlers(ecs_world_t* w, ecs_entity_t comp_id);

template<typename T>
ecs_entity_t __get_script_component_id(ecs_world_t* w) {
    extern std::unordered_map<const std::type_info*, ecs_entity_t> g_script_component_registry;
    
    const std::type_info* type_id = &typeid(T);
    
    // Check if already registered
    auto it = g_script_component_registry.find(type_id);
    if (it != g_script_component_registry.end()) {
        return it->second;
    }
    
    // Register new script component with Flecs hooks
    ecs_component_desc_t desc = {0};
    ecs_entity_desc_t entity_desc = {0};
    entity_desc.name = typeid(T).name();
    desc.entity = ecs_entity_init(w, &entity_desc);
    desc.type.size = sizeof(ScriptComponent<T>);
    desc.type.alignment = alignof(ScriptComponent<T>);
    
    // Set up lifecycle hooks
    desc.type.hooks.ctor = ScriptComponent<T>::ctor;
    desc.type.hooks.dtor = ScriptComponent<T>::dtor;
    desc.type.hooks.on_set = ScriptComponent<T>::on_set;
    
    ecs_entity_t comp_id = ecs_component_init(w, &desc);
    g_script_component_registry[type_id] = comp_id;

    // Register observers and systems for this script component
    __register_script_handlers(w, comp_id);
    
    return comp_id;
}

// Forward declaration of internal component id holder
struct CompIds {
    ecs_entity_t transform;
    ecs_entity_t body;
    ecs_entity_t scale2d;
    ecs_entity_t tag;
    ecs_entity_t layer;
    ecs_entity_t sprite;
    ecs_entity_t material;
    ecs_entity_t tilemap;
    ecs_entity_t mesh;
    ecs_entity_t camera;
    ecs_entity_t text;
    ecs_entity_t collider2d;
    ecs_entity_t audio_source;
    ecs_entity_t audio_listener;
};
extern CompIds g_comp;

// Internal component PODs (façade data stored in ECS)
struct Scale2D { float sx; float sy; };
struct TagData { char tag_str[64]; };
struct LayerData { int layer; };
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
struct AudioSourceData { AmeAudioSource source; float volume; float pitch; bool mute; bool loop; bool play_on_awake; bool is_playing; int dirty; };
struct AudioListenerData { float volume; bool mute; };

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

    bool activeSelf() const;
    void SetActive(bool v);

    const std::string& name() const; // backed by an internal cache
    void name(const std::string&);
    
    // Tag and layer system
    std::string tag() const;
    void tag(const std::string& t);
    bool CompareTag(const std::string& t) const;
    int layer() const;
    void layer(int l);

    // Component helpers
    template<typename T, typename... Args>
    T& AddComponent(Args&&...);
    template<typename T>
    T* TryGetComponent();
    template<typename T>
    T& GetComponent();
    template<typename T>
    bool HasComponent() const;
    template<typename T>
    void RemoveComponent();
    template<typename T, typename... Args>
    T& GetOrAddComponent(Args&&... args);

    // Script helpers
    template<typename T, typename... Args>
    T& AddScript(Args&&... args);
    template<typename T>
    T* GetScript();
    template<typename T>
    bool HasScript();
    template<typename T>
    void RemoveScript();

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
    void position(float x, float y) { position(glm::vec3(x, y, 0.0f)); }
    void position(const glm::vec2& p) { position(glm::vec3(p.x, p.y, 0.0f)); }
    glm::quat rotation() const;
    void rotation(const glm::quat& q);
    glm::vec3 localScale() const;
    void localScale(const glm::vec3& s);
    void localScale(const glm::vec2& s) { localScale(glm::vec3(s.x, s.y, 1.0f)); }

    // World/composed accessors (read-only): computed by traversing EcsChildOf chain
    glm::vec3 worldPosition() const;
    glm::quat worldRotation() const;
    
    // Helper methods
    void Translate(const glm::vec3& translation, bool relativeTo = true); // true=self, false=world
    void Translate(const glm::vec2& translation, bool relativeTo = true) { Translate(glm::vec3(translation.x, translation.y, 0.0f), relativeTo); }
    void Rotate(float angle); // rotate by angle in radians (2D)
    void LookAt2D(const glm::vec2& worldTarget); // set rotation so +X faces toward target
    glm::vec2 right() const;  // local right direction (2D: cos(angle), sin(angle))
    glm::vec2 up() const;     // local up direction (2D: perpendicular to right)
    float eulerAngles() const; // return Z rotation angle in radians (2D only)
    void eulerAngles(float angleZ); // set Z rotation angle in radians
    
    // Space transformation helpers
    glm::vec2 TransformPoint(const glm::vec2& localPoint) const;
    glm::vec2 TransformDirection(const glm::vec2& localDir) const;
    glm::vec2 InverseTransformPoint(const glm::vec2& worldPoint) const;
    glm::vec2 InverseTransformDirection(const glm::vec2& worldDir) const;
private:
    GameObject owner_;
};

// Forward declare collision structs
struct Collision2D {
    GameObject gameObject;
    Rigidbody2D* rigidbody;
    Collider2D* collider;
    glm::vec2 relativeVelocity;
    // Contacts omitted for MVP
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
    
    // Collision callbacks (2D physics)
    virtual void OnCollisionEnter2D(const Collision2D& collision) {}
    virtual void OnCollisionStay2D(const Collision2D& collision) {}
    virtual void OnCollisionExit2D(const Collision2D& collision) {}
    
    // Trigger callbacks (2D physics)
    virtual void OnTriggerEnter2D(Collider2D* other) {}
    virtual void OnTriggerStay2D(Collider2D* other) {}
    virtual void OnTriggerExit2D(Collider2D* other) {}

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
    enum class BodyType { Dynamic = 0, Kinematic = 1, Static = 2 };
    
    explicit Rigidbody2D(GameObject owner) : owner_(owner) {}
    
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
    
    // Forces and impulses
    void AddForce(const glm::vec2& force);
    void AddForceAtPosition(const glm::vec2& force, const glm::vec2& position);
    void AddTorque(float torque);
    void AddImpulse(const glm::vec2& impulse);
    void AddAngularImpulse(float impulse);
    
    // Mass and physics properties
    float mass() const;
    void mass(float m);
    float gravityScale() const;
    void gravityScale(float scale);
    float drag() const;
    void drag(float d);
    float angularDrag() const;
    void angularDrag(float d);
    
    // Constraints (freeze position/rotation)
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

// AudioSource component wrapper
class AudioSource {
public:
    explicit AudioSource(GameObject owner) : owner_(owner) {}
    
    // Playback control
    void Play();
    void Stop();
    void Pause();
    void UnPause();
    
    // Audio properties
    float volume() const;
    void volume(float v);
    float pitch() const;
    void pitch(float p);
    bool mute() const;
    void mute(bool m);
    bool loop() const;
    void loop(bool l);
    bool playOnAwake() const;
    void playOnAwake(bool p);
    
    // Playback state
    bool isPlaying() const;
    
    // Audio clip loading
    bool LoadOpusFile(const char* filepath, bool loop_audio = false);
    void InitSigmoidOsc(float freq_hz, float shape_k = 6.0f, float gain = 1.0f);
    void InitSawWork(float base_freq_hz, float drive = 1.0f, float noise_mix = 0.3f, float lfo_rate_hz = 4.0f, float gain = 1.0f);
    void InitSawCut(float freq_hz, float drive = 1.0f, float noise_mix = 0.5f, float duration_sec = 0.1f, float gain = 1.0f);
    
    // Pan control
    float pan() const;
    void pan(float p); // -1.0 = left, 0 = center, 1.0 = right
    
private:
    GameObject owner_;
};

// AudioListener component wrapper  
class AudioListener {
public:
    explicit AudioListener(GameObject owner) : owner_(owner) {}
    
    float volume() const;
    void volume(float v);
    bool mute() const;
    void mute(bool m);
    
    // Static access to main listener
    static AudioListener* main();
    static void SetMain(AudioListener* listener);
    
private:
    GameObject owner_;
    static AudioListener* main_listener_;
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
        std::is_same_v<T, TextRenderer> || std::is_same_v<T, Collider2D> ||
        std::is_same_v<T, AudioSource> || std::is_same_v<T, AudioListener>,
        "AddComponent<T>: MVP supports Transform, Rigidbody2D, Sprite, Material, Tilemap, Mesh, Camera, Text, Collider2D, AudioSource, AudioListener"
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
    } else if constexpr (std::is_same_v<T, AudioSource>) {
        AudioSourceData asd = {0};
        asd.volume = 1.0f;
        asd.pitch = 1.0f;
        asd.mute = false;
        asd.loop = false;
        asd.play_on_awake = false;
        asd.is_playing = false;
        asd.dirty = 1;
        ecs_set_id(w, (ecs_entity_t)e_, g_comp.audio_source, sizeof(AudioSourceData), &asd);
        static thread_local AudioSource as{ GameObject() };
        as = AudioSource{ *this };
        return as;
    } else if constexpr (std::is_same_v<T, AudioListener>) {
        AudioListenerData ald = {0};
        ald.volume = 1.0f;
        ald.mute = false;
        ecs_set_id(w, (ecs_entity_t)e_, g_comp.audio_listener, sizeof(AudioListenerData), &ald);
        static thread_local AudioListener al{ GameObject() };
        al = AudioListener{ *this };
        return al;
    }
}

template<typename T>
T* GameObject::TryGetComponent() {
static_assert(
        std::is_same_v<T, Transform> || std::is_same_v<T, Rigidbody2D> ||
        std::is_same_v<T, SpriteRenderer> || std::is_same_v<T, Material> ||
        std::is_same_v<T, TilemapRenderer> || std::is_same_v<T, MeshRenderer> || std::is_same_v<T, Camera> ||
        std::is_same_v<T, TextRenderer> || std::is_same_v<T, Collider2D> ||
        std::is_same_v<T, AudioSource> || std::is_same_v<T, AudioListener>,
        "TryGetComponent<T>: supported types are Transform, Rigidbody2D, Sprite, Material, Tilemap, Mesh, Camera, Text, Collider2D, AudioSource, AudioListener"
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
    } else if constexpr (std::is_same_v<T, AudioSource>) {
        if (ecs_get_id(w, (ecs_entity_t)e_, g_comp.audio_source)) {
            static thread_local AudioSource as{ GameObject() };
            as = AudioSource{ *this };
            return &as;
        }
        return nullptr;
    } else if constexpr (std::is_same_v<T, AudioListener>) {
        if (ecs_get_id(w, (ecs_entity_t)e_, g_comp.audio_listener)) {
            static thread_local AudioListener al{ GameObject() };
            al = AudioListener{ *this };
            return &al;
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

template<typename T>
bool GameObject::HasComponent() const {
    static_assert(
        std::is_same_v<T, Transform> || std::is_same_v<T, Rigidbody2D> ||
        std::is_same_v<T, SpriteRenderer> || std::is_same_v<T, Material> ||
        std::is_same_v<T, TilemapRenderer> || std::is_same_v<T, MeshRenderer> || std::is_same_v<T, Camera> ||
        std::is_same_v<T, TextRenderer> || std::is_same_v<T, Collider2D> ||
        std::is_same_v<T, AudioSource> || std::is_same_v<T, AudioListener>,
        "HasComponent<T>: supported types are Transform, Rigidbody2D, Sprite, Material, Tilemap, Mesh, Camera, Text, Collider2D, AudioSource, AudioListener"
    );
    if (!scene_ || !e_) return false;
    ecs_world_t* w = scene_->world();
    ensure_components_registered(w);
    if constexpr (std::is_same_v<T, Transform>) return ecs_get_id(w, (ecs_entity_t)e_, g_comp.transform) != nullptr;
    else if constexpr (std::is_same_v<T, Rigidbody2D>) return ecs_get_id(w, (ecs_entity_t)e_, g_comp.body) != nullptr;
    else if constexpr (std::is_same_v<T, SpriteRenderer>) return ecs_get_id(w, (ecs_entity_t)e_, g_comp.sprite) != nullptr;
    else if constexpr (std::is_same_v<T, Material>) return ecs_get_id(w, (ecs_entity_t)e_, g_comp.material) != nullptr;
    else if constexpr (std::is_same_v<T, TilemapRenderer>) return ecs_get_id(w, (ecs_entity_t)e_, g_comp.tilemap) != nullptr;
    else if constexpr (std::is_same_v<T, MeshRenderer>) return ecs_get_id(w, (ecs_entity_t)e_, g_comp.mesh) != nullptr;
    else if constexpr (std::is_same_v<T, Camera>) return ecs_get_id(w, (ecs_entity_t)e_, g_comp.camera) != nullptr;
    else if constexpr (std::is_same_v<T, TextRenderer>) return ecs_get_id(w, (ecs_entity_t)e_, g_comp.text) != nullptr;
    else if constexpr (std::is_same_v<T, Collider2D>) return ecs_get_id(w, (ecs_entity_t)e_, g_comp.collider2d) != nullptr;
    else if constexpr (std::is_same_v<T, AudioSource>) return ecs_get_id(w, (ecs_entity_t)e_, g_comp.audio_source) != nullptr;
    else if constexpr (std::is_same_v<T, AudioListener>) return ecs_get_id(w, (ecs_entity_t)e_, g_comp.audio_listener) != nullptr;
}

template<typename T>
void GameObject::RemoveComponent() {
    static_assert(
        std::is_same_v<T, Transform> || std::is_same_v<T, Rigidbody2D> ||
        std::is_same_v<T, SpriteRenderer> || std::is_same_v<T, Material> ||
        std::is_same_v<T, TilemapRenderer> || std::is_same_v<T, MeshRenderer> || std::is_same_v<T, Camera> ||
        std::is_same_v<T, TextRenderer> || std::is_same_v<T, Collider2D> ||
        std::is_same_v<T, AudioSource> || std::is_same_v<T, AudioListener>,
        "RemoveComponent<T>: supported types are Transform, Rigidbody2D, Sprite, Material, Tilemap, Mesh, Camera, Text, Collider2D, AudioSource, AudioListener"
    );
    if (!scene_ || !e_) return;
    ecs_world_t* w = scene_->world();
    ensure_components_registered(w);
    if constexpr (std::is_same_v<T, Transform>) ecs_remove_id(w, (ecs_entity_t)e_, g_comp.transform);
    else if constexpr (std::is_same_v<T, Rigidbody2D>) ecs_remove_id(w, (ecs_entity_t)e_, g_comp.body);
    else if constexpr (std::is_same_v<T, SpriteRenderer>) ecs_remove_id(w, (ecs_entity_t)e_, g_comp.sprite);
    else if constexpr (std::is_same_v<T, Material>) ecs_remove_id(w, (ecs_entity_t)e_, g_comp.material);
    else if constexpr (std::is_same_v<T, TilemapRenderer>) ecs_remove_id(w, (ecs_entity_t)e_, g_comp.tilemap);
    else if constexpr (std::is_same_v<T, MeshRenderer>) ecs_remove_id(w, (ecs_entity_t)e_, g_comp.mesh);
    else if constexpr (std::is_same_v<T, Camera>) ecs_remove_id(w, (ecs_entity_t)e_, g_comp.camera);
    else if constexpr (std::is_same_v<T, TextRenderer>) ecs_remove_id(w, (ecs_entity_t)e_, g_comp.text);
    else if constexpr (std::is_same_v<T, Collider2D>) ecs_remove_id(w, (ecs_entity_t)e_, g_comp.collider2d);
    else if constexpr (std::is_same_v<T, AudioSource>) ecs_remove_id(w, (ecs_entity_t)e_, g_comp.audio_source);
    else if constexpr (std::is_same_v<T, AudioListener>) ecs_remove_id(w, (ecs_entity_t)e_, g_comp.audio_listener);
}

template<typename T, typename... Args>
T& GameObject::GetOrAddComponent(Args&&... args) {
    if (auto* p = TryGetComponent<T>()) return *p;
    return AddComponent<T>(std::forward<Args>(args)...);
}

template<typename T, typename... Args>
T& GameObject::AddScript(Args&&... args) {
    static_assert(std::is_base_of_v<MongooseBehaviour, T>, "Script must inherit from MongooseBehaviour");
    
    ecs_world_t* w = scene_->world();
    ensure_components_registered(w);
    
    // Get or create the component type for this script
    ecs_entity_t script_component_id = __get_script_component_id<T>(w);
    
    // Create the script instance
    T* script = new T(std::forward<Args>(args)...);
    script->__set_owner(*this);
    
    // Create and set the script component
    ScriptComponent<T> comp;
    comp.script = script;
    comp.awoken = false;
    comp.started = false;
    
    ecs_set_id(w, (ecs_entity_t)e_, script_component_id, sizeof(ScriptComponent<T>), &comp);
    
    return *script;
}

template<typename T>
T* GameObject::GetScript() {
    static_assert(std::is_base_of_v<MongooseBehaviour, T>, "Script must inherit from MongooseBehaviour");
    
    ecs_world_t* w = scene_->world();
    if (!w || !e_) return nullptr;
    
    // Get the component ID for this script type
    ecs_entity_t script_component_id = __get_script_component_id<T>(w);
    if (script_component_id == 0) return nullptr;
    
    // Get the ScriptComponent
    const ScriptComponent<T>* comp = (const ScriptComponent<T>*)ecs_get_id(w, (ecs_entity_t)e_, script_component_id);
    if (!comp || !comp->script) return nullptr;
    
    return comp->script;
}

template<typename T>
bool GameObject::HasScript() {
    static_assert(std::is_base_of_v<MongooseBehaviour, T>, "Script must inherit from MongooseBehaviour");
    ecs_world_t* w = scene_->world();
    if (!w || !e_) return false;
    ecs_entity_t id = __get_script_component_id<T>(w);
    if (!id) return false;
    const ScriptComponent<T>* comp = (const ScriptComponent<T>*)ecs_get_id(w, (ecs_entity_t)e_, id);
    return comp && comp->script;
}

template<typename T>
void GameObject::RemoveScript() {
    static_assert(std::is_base_of_v<MongooseBehaviour, T>, "Script must inherit from MongooseBehaviour");
    ecs_world_t* w = scene_->world();
    if (!w || !e_) return;
    ecs_entity_t id = __get_script_component_id<T>(w);
    if (!id) return;
    ecs_remove_id(w, (ecs_entity_t)e_, id);
}


} // namespace unitylike
