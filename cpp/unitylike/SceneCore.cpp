#include "Scene.h"
#include "TransformHierarchy.h"
#include <flecs.h>
#include <cassert>
#include <unordered_map>
#include <typeinfo>
#include <SDL3/SDL.h>

namespace unitylike {

// Global script component registry
std::unordered_map<const std::type_info*, ecs_entity_t> g_script_component_registry;
static bool g_systems_registered = false;
static float g_current_dt = 0.0f;
static float g_fixed_dt = 1.0f/60.0f;


// Observer that runs when script components are added - handles Awake automatically
static void ScriptOnAddObserver(ecs_iter_t* it) {
    // This runs for each component type that triggers it
    size_t comp_size = ecs_field_size(it, 0);
    void* components = ecs_field_w_size(it, comp_size, 0);
    
    for (int i = 0; i < it->count; i++) {
        struct BaseScriptComponent { void* script; bool awoken; bool started; };
        BaseScriptComponent* base = (BaseScriptComponent*)((char*)components + i * comp_size);
        
        if (base->script && !base->awoken) {
            ((MongooseBehaviour*)base->script)->Awake();
            base->awoken = true;
        }
    }
}

// Observer that runs when script components are removed - handles OnDestroy
static void ScriptOnRemoveObserver(ecs_iter_t* it) {
    size_t comp_size = ecs_field_size(it, 0);
    void* components = ecs_field_w_size(it, comp_size, 0);
    
    for (int i = 0; i < it->count; i++) {
        struct BaseScriptComponent { void* script; bool awoken; bool started; };
        BaseScriptComponent* base = (BaseScriptComponent*)((char*)components + i * comp_size);
        
        if (base->script) {
            ((MongooseBehaviour*)base->script)->OnDestroy();
            // Note: Don't delete here - the ScriptComponent destructor will handle it
        }
    }
}

// Update system - handles Start and Update
static void ScriptUpdateSystem(ecs_iter_t* it) {
    size_t comp_size = ecs_field_size(it, 0);
    void* components = ecs_field_w_size(it, comp_size, 0);
    
    for (int i = 0; i < it->count; i++) {
        struct BaseScriptComponent { void* script; bool awoken; bool started; };
        BaseScriptComponent* base = (BaseScriptComponent*)((char*)components + i * comp_size);
        
        if (base->script && base->awoken) {
            if (!base->started) {
                ((MongooseBehaviour*)base->script)->Start();
                base->started = true;
            }
            ((MongooseBehaviour*)base->script)->Update(g_current_dt);
        }
    }
}

// LateUpdate system
static void ScriptLateUpdateSystem(ecs_iter_t* it) {
    size_t comp_size = ecs_field_size(it, 0);
    void* components = ecs_field_w_size(it, comp_size, 0);
    
    for (int i = 0; i < it->count; i++) {
        struct BaseScriptComponent { void* script; bool awoken; bool started; };
        BaseScriptComponent* base = (BaseScriptComponent*)((char*)components + i * comp_size);
        
        if (base->script && base->started) {
            ((MongooseBehaviour*)base->script)->LateUpdate();
        }
    }
}

// FixedUpdate system
static void ScriptFixedUpdateSystem(ecs_iter_t* it) {
    size_t comp_size = ecs_field_size(it, 0);
    void* components = ecs_field_w_size(it, comp_size, 0);
    
    for (int i = 0; i < it->count; i++) {
        struct BaseScriptComponent { void* script; bool awoken; bool started; };
        BaseScriptComponent* base = (BaseScriptComponent*)((char*)components + i * comp_size);
        
        if (base->script && base->started) {
            ((MongooseBehaviour*)base->script)->FixedUpdate(g_fixed_dt);
        }
    }
}

// Forward declarations
static void register_script_update_systems(ecs_world_t* world, ecs_entity_t comp_id);

// Register observers and systems for a specific script component type
static void register_script_observers_and_systems(ecs_world_t* world, ecs_entity_t comp_id) {
    // Avoid registering empty comp_id
    if (!comp_id) return;
    
    SDL_Log("[UnityLike] Registering observers/systems for script component %lu", (unsigned long)comp_id);
    
    // OnAdd observer for Awake
    ecs_observer_desc_t on_add_desc = {0};
    ecs_entity_desc_t on_add_entity_desc = {0};
    on_add_entity_desc.name = "ScriptOnAdd";
    on_add_desc.entity = ecs_entity_init(world, &on_add_entity_desc);
    on_add_desc.query.terms[0].id = comp_id;
    on_add_desc.events[0] = EcsOnAdd;
    on_add_desc.callback = ScriptOnAddObserver;
    ecs_entity_t observer1 = ecs_observer_init(world, &on_add_desc);
    SDL_Log("[UnityLike] Registered OnAdd observer: %lu", (unsigned long)observer1);
    
    // OnRemove observer for OnDestroy
    ecs_observer_desc_t on_remove_desc = {0};
    ecs_entity_desc_t on_remove_entity_desc = {0};
    on_remove_entity_desc.name = "ScriptOnRemove";
    on_remove_desc.entity = ecs_entity_init(world, &on_remove_entity_desc);
    on_remove_desc.query.terms[0].id = comp_id;
    on_remove_desc.events[0] = EcsOnRemove;
    on_remove_desc.callback = ScriptOnRemoveObserver;
    ecs_entity_t observer2 = ecs_observer_init(world, &on_remove_desc);
    SDL_Log("[UnityLike] Registered OnRemove observer: %lu", (unsigned long)observer2);
    
    // Update system
    ecs_system_desc_t update_desc = {0};
    ecs_entity_desc_t update_entity_desc = {0};
    update_entity_desc.name = "ScriptUpdate";
    update_desc.entity = ecs_entity_init(world, &update_entity_desc);
    update_desc.query.terms[0].id = comp_id;
    update_desc.callback = ScriptUpdateSystem;
    update_desc.multi_threaded = true;
    ecs_entity_t system1 = ecs_system_init(world, &update_desc);
    SDL_Log("[UnityLike] Registered Update system: %lu", (unsigned long)system1);
    
    // LateUpdate system
    ecs_system_desc_t late_update_desc = {0};
    ecs_entity_desc_t late_update_entity_desc = {0};
    late_update_entity_desc.name = "ScriptLateUpdate";
    late_update_desc.entity = ecs_entity_init(world, &late_update_entity_desc);
    late_update_desc.query.terms[0].id = comp_id;
    late_update_desc.callback = ScriptLateUpdateSystem;
    late_update_desc.multi_threaded = true;
    ecs_entity_t system2 = ecs_system_init(world, &late_update_desc);
    SDL_Log("[UnityLike] Registered LateUpdate system: %lu", (unsigned long)system2);
    
    // FixedUpdate system
    ecs_system_desc_t fixed_update_desc = {0};
    ecs_entity_desc_t fixed_update_entity_desc = {0};
    fixed_update_entity_desc.name = "ScriptFixedUpdate";
    fixed_update_desc.entity = ecs_entity_init(world, &fixed_update_entity_desc);
    fixed_update_desc.query.terms[0].id = comp_id;
    fixed_update_desc.callback = ScriptFixedUpdateSystem;
    fixed_update_desc.multi_threaded = true;
    ecs_entity_t system3 = ecs_system_init(world, &fixed_update_desc);
    SDL_Log("[UnityLike] Registered FixedUpdate system: %lu", (unsigned long)system3);
}

// Expose registration for template to call
void __register_script_handlers(ecs_world_t* world, ecs_entity_t comp_id) {
    // Use simplified system registration
    register_script_update_systems(world, comp_id);
}

// Simple system registration without observers - just systems to handle Update loops
static void register_script_update_systems(ecs_world_t* world, ecs_entity_t comp_id) {
    if (!comp_id) return;
    
    SDL_Log("[UnityLike] Registering Update systems for script component %lu", (unsigned long)comp_id);
    
    // Update system for Start and Update calls
    ecs_system_desc_t update_desc = {0};
    ecs_entity_desc_t update_entity_desc = {0};
    std::string update_name = "ScriptUpdate_" + std::to_string(comp_id);
    update_entity_desc.name = update_name.c_str();
    update_desc.entity = ecs_entity_init(world, &update_entity_desc);
    update_desc.query.terms[0].id = comp_id;
    update_desc.callback = ScriptUpdateSystem;
    ecs_entity_t system1 = ecs_system_init(world, &update_desc);
    SDL_Log("[UnityLike] Registered Update system: %lu", (unsigned long)system1);
    
    // LateUpdate system
    ecs_system_desc_t late_update_desc = {0};
    ecs_entity_desc_t late_update_entity_desc = {0};
    std::string late_update_name = "ScriptLateUpdate_" + std::to_string(comp_id);
    late_update_entity_desc.name = late_update_name.c_str();
    late_update_desc.entity = ecs_entity_init(world, &late_update_entity_desc);
    late_update_desc.query.terms[0].id = comp_id;
    late_update_desc.callback = ScriptLateUpdateSystem;
    ecs_entity_t system2 = ecs_system_init(world, &late_update_desc);
    SDL_Log("[UnityLike] Registered LateUpdate system: %lu", (unsigned long)system2);
    
    // FixedUpdate system
    ecs_system_desc_t fixed_update_desc = {0};
    ecs_entity_desc_t fixed_update_entity_desc = {0};
    std::string fixed_update_name = "ScriptFixedUpdate_" + std::to_string(comp_id);
    fixed_update_entity_desc.name = fixed_update_name.c_str();
    fixed_update_desc.entity = ecs_entity_init(world, &fixed_update_entity_desc);
    fixed_update_desc.query.terms[0].id = comp_id;
    fixed_update_desc.callback = ScriptFixedUpdateSystem;
    ecs_entity_t system3 = ecs_system_init(world, &fixed_update_desc);
    SDL_Log("[UnityLike] Registered FixedUpdate system: %lu", (unsigned long)system3);
}

// Register the script systems using wildcard queries to catch any script component
static void register_script_systems(ecs_world_t* world) {
    if (g_systems_registered) return;
    
    ensure_components_registered(world);
    
    // Systems that run on any entity - they'll check for script components internally
    // This approach allows proper multithreading while being flexible
    
    ecs_entity_desc_t awake_entity_desc = {0};
    awake_entity_desc.name = "UnitylikeScriptAwake";
    ecs_system_desc_t awake_desc = {0};
    awake_desc.entity = ecs_entity_init(world, &awake_entity_desc);
    awake_desc.callback = ScriptUpdateSystem;
    awake_desc.multi_threaded = true;
    ecs_system_init(world, &awake_desc);
    
    ecs_entity_desc_t update_entity_desc = {0};
    update_entity_desc.name = "UnitylikeScriptUpdate";
    ecs_system_desc_t update_desc = {0};
    update_desc.entity = ecs_entity_init(world, &update_entity_desc);
    update_desc.callback = ScriptUpdateSystem;
    update_desc.multi_threaded = true;
    ecs_system_init(world, &update_desc);
    
    ecs_entity_desc_t late_update_entity_desc = {0};
    late_update_entity_desc.name = "UnitylikeScriptLateUpdate";
    ecs_system_desc_t late_update_desc = {0};
    late_update_desc.entity = ecs_entity_init(world, &late_update_entity_desc);
    late_update_desc.callback = ScriptLateUpdateSystem;
    late_update_desc.multi_threaded = true;
    ecs_system_init(world, &late_update_desc);
    
    ecs_entity_desc_t fixed_update_entity_desc = {0};
    fixed_update_entity_desc.name = "UnitylikeScriptFixedUpdate";
    ecs_system_desc_t fixed_update_desc = {0};
    fixed_update_desc.entity = ecs_entity_init(world, &fixed_update_entity_desc);
    fixed_update_desc.callback = ScriptFixedUpdateSystem;
    fixed_update_desc.multi_threaded = true;
    ecs_system_init(world, &fixed_update_desc);
    
    g_systems_registered = true;
    SDL_Log("[UnityLike] Script execution systems registered with ECS pipeline (multithreaded)");
}

// Scene core
Scene::Scene(ecs_world_t* world) : world_(world) {
    assert(world_ != nullptr);
    ensure_components_registered(world_);
    register_script_systems(world_);
}

Scene::~Scene() {
    // Scripts are now managed by Flecs components with proper destructors
    // No manual cleanup needed - Flecs will call the component destructors
}

GameObject Scene::Create(const std::string& name) {
    ensure_components_registered(world_);
    ecs_entity_desc_t ed = {0};
    if (!name.empty()) ed.name = name.c_str();
    ecs_entity_t e = ecs_entity_init(world_, &ed);
    GameObject go(this, (GameObject::Entity)e);
    if (!name.empty()) { go.name(name); }
    return go;
}

void Scene::Destroy(GameObject& go) {
    if (!go.id()) return;
    // Flecs will automatically call component destructors, including script cleanup
    ecs_delete(world_, (ecs_entity_t)go.id());
}

GameObject Scene::Find(const std::string& name) {
    if (!world_ || name.empty()) return GameObject();
    ecs_entity_t e = ecs_lookup(world_, name.c_str());
    if (!e) return GameObject();
    return GameObject(this, (GameObject::Entity)e);
}

extern void unitylike_begin_update(float dt);
extern void unitylike_set_fixed_dt(float fdt);

void Scene::Step(float dt) {
    ensure_components_registered(world_);
    unitylike_begin_update(dt);
    g_current_dt = dt;
    
    // Let Flecs run all registered systems - they will handle script execution
    // The systems are properly registered with multithreading support
    ecs_progress(world_, 0);
}

void Scene::StepFixed(float fdt) {
    ensure_components_registered(world_);
    unitylike_set_fixed_dt(fdt);
    g_fixed_dt = fdt;
    
    // Fixed update systems will be called by the main Step() method
    // since they're registered as regular ECS systems
}

// GameObject basics
bool GameObject::activeSelf() const {
    if (!scene_ || !e_) return false;
    ecs_world_t* w = scene_->world();
    return !ecs_has_id(w, (ecs_entity_t)e_, EcsDisabled);
}

void GameObject::SetActive(bool v) {
    if (!scene_ || !e_) return;
    ecs_world_t* w = scene_->world();
    if (v) ecs_remove_id(w, (ecs_entity_t)e_, EcsDisabled);
    else ecs_add_id(w, (ecs_entity_t)e_, EcsDisabled);
}

const std::string& GameObject::name() const {
    if (!name_cache_.empty()) return name_cache_;
    if (!scene_ || !e_) return name_cache_;
    const char* n = ecs_get_name(scene_->world(), (ecs_entity_t)e_);
    if (n) const_cast<std::string&>(name_cache_).assign(n);
    return name_cache_;
}

void GameObject::name(const std::string& n) {
    name_cache_ = n;
    if (!scene_ || !e_) return;
    ecs_set_name(scene_->world(), (ecs_entity_t)e_, n.c_str());
}

Transform& GameObject::transform() {
    static thread_local Transform t{ GameObject() };
    t = Transform{*this};
    return t;
}

bool GameObject::IsValid() const {
    if (!scene_ || !e_) return false;
    ecs_world_t* w = scene_->world();
    return ecs_is_alive(w, (ecs_entity_t)e_);
}

std::string GameObject::tag() const {
    if (!scene_ || !e_) return "";
    ecs_world_t* w = scene_->world();
    ensure_components_registered(w);
    TagData* td = (TagData*)ecs_get_id(w, (ecs_entity_t)e_, g_comp.tag);
    if (!td) return "Untagged";
    return std::string(td->tag_str);
}

void GameObject::tag(const std::string& t) {
    if (!scene_ || !e_) return;
    ecs_world_t* w = scene_->world();
    ensure_components_registered(w);
    TagData td = {0};
    strncpy(td.tag_str, t.c_str(), sizeof(td.tag_str) - 1);
    td.tag_str[sizeof(td.tag_str) - 1] = '\0';
    ecs_set_id(w, (ecs_entity_t)e_, g_comp.tag, sizeof(TagData), &td);
}

bool GameObject::CompareTag(const std::string& t) const {
    return tag() == t;
}

int GameObject::layer() const {
    if (!scene_ || !e_) return 0;
    ecs_world_t* w = scene_->world();
    ensure_components_registered(w);
    LayerData* ld = (LayerData*)ecs_get_id(w, (ecs_entity_t)e_, g_comp.layer);
    if (!ld) return 0;
    return ld->layer;
}

void GameObject::layer(int l) {
    if (!scene_ || !e_) return;
    ecs_world_t* w = scene_->world();
    ensure_components_registered(w);
    LayerData ld = { l };
    ecs_set_id(w, (ecs_entity_t)e_, g_comp.layer, sizeof(LayerData), &ld);
}

void GameObject::SetParent(const GameObject& parent, bool keepWorld) {
    if (!scene_ || !e_) return;
    ecs_world_t* w = scene_->world();
    if (parent.scene() && parent.scene() != scene_) {
        // Cross-scene parenting not supported
        SDL_Log("[Scene] SetParent disallowed: cross-scene parenting child=%llu parent=%llu",
                (unsigned long long)e_, (unsigned long long)parent.e_);
        return;
    }
    if ((ecs_entity_t)e_ == (ecs_entity_t)parent.e_) {
        // disallow self-parenting
        SDL_Log("[Scene] SetParent disallowed: self-parenting entity=%llu", (unsigned long long)e_);
        return;
    }
    // Prevent cycles: ensure parent is not a descendant of this
    if (parent.e_) {
        ecs_entity_t cur = (ecs_entity_t)parent.e_;
        int depth = 0;
        while (cur && depth++ < 1024) {
            if (cur == (ecs_entity_t)e_) {
                // would create a cycle; abort
                SDL_Log("[Scene] SetParent would create cycle: child=%llu parent=%llu", (unsigned long long)e_, (unsigned long long)parent.e_);
                return;
            }
            ecs_entity_t p = ecs_get_target(w, cur, EcsChildOf, 0);
            if (!p) break; cur = p;
        }
    }
    // Compute world before change using helper
    auto compute_world = [&](ecs_entity_t ent){
        AmeWorldTransform2D wt = ameComputeWorldTransform(w, ent);
        return std::tuple<float,float,float>(wt.x, wt.y, wt.angle);
    };
    float cw_x=0, cw_y=0, cw_a=0;
    if (keepWorld) {
        std::tie(cw_x, cw_y, cw_a) = compute_world((ecs_entity_t)e_);
    }
    // Remove current parent and set new
    ecs_entity_t curp = ecs_get_target(w, (ecs_entity_t)e_, EcsChildOf, 0);
    if (curp) ecs_remove_pair(w, (ecs_entity_t)e_, EcsChildOf, curp);
    if (parent.e_) ecs_add_pair(w, (ecs_entity_t)e_, EcsChildOf, (ecs_entity_t)parent.e_);

    if (keepWorld) {
        // derive local = world relative to new parent
        float pw_x=0, pw_y=0, pw_a=0;
        if (parent.e_) {
            std::tie(pw_x, pw_y, pw_a) = compute_world((ecs_entity_t)parent.e_);
        }
        float la = cw_a - pw_a;
        float dx = cw_x - pw_x;
        float dy = cw_y - pw_y;
        float cs = cosf(-pw_a), sn = sinf(-pw_a);
        float lx = dx * cs - dy * sn;
        float ly = dx * sn + dy * cs;
        AmeTransform2D tr = { lx, ly, la };
        ecs_set_id(w, (ecs_entity_t)e_, g_comp.transform, sizeof(AmeTransform2D), &tr);
    }
}

GameObject GameObject::GetParent() const {
    if (!scene_ || !e_) return GameObject();
    ecs_world_t* w = scene_->world();
    ecs_entity_t p = ecs_get_target(w, (ecs_entity_t)e_, EcsChildOf, 0);
    if (!p) return GameObject();
    return GameObject(scene_, (Entity)p);
}

std::vector<GameObject> GameObject::GetChildren() const {
    std::vector<GameObject> out;
    if (!scene_ || !e_) return out;
    ecs_world_t* w = scene_->world();
    ecs_iter_t it = ecs_children(w, (ecs_entity_t)e_);
    while (ecs_children_next(&it)) {
        for (int i=0;i<it.count;i++) {
            out.emplace_back(scene_, (Entity)it.entities[i]);
        }
    }
    return out;
}


} // namespace unitylike
