#include "Scene.h"
#include "TransformHierarchy.h"
#include <flecs.h>
#include <cassert>
#include <unordered_map>
#include <vector>
#include <SDL3/SDL.h>

namespace unitylike {

// Globals declared in header (remove duplicate definitions)
// CompIds g_comp{};
// ecs_entity_t g_comp_script_host = 0;

static std::vector<ecs_entity_t> g_script_entities;
static std::unordered_map<ecs_entity_t, ScriptHost> g_script_hosts;
static bool g_systems_registered = false;
static float g_current_dt = 0.0f;
static float g_fixed_dt = 1.0f/60.0f;

// ECS system functions for script execution
static void ScriptAwakeSystem(ecs_iter_t* it) {
    // Run Awake for all scripts that haven't been awoken
    for (ecs_entity_t e : g_script_entities) {
        ScriptHost* shp = __get_script_host(e);
        if (!shp) continue;
        ScriptHost& sh = *shp;
        if (!sh.awoken) {
            for (auto* s : sh.scripts) if (s) s->Awake();
            sh.awoken = true;
        }
    }
}

static void ScriptStartSystem(ecs_iter_t* it) {
    // Run Start for all scripts that have been awoken but not started
    for (ecs_entity_t e : g_script_entities) {
        ScriptHost* shp = __get_script_host(e);
        if (!shp) continue;
        ScriptHost& sh = *shp;
        if (sh.awoken && !sh.started) {
            for (auto* s : sh.scripts) if (s) s->Start();
            sh.started = true;
        }
    }
}

static void ScriptUpdateSystem(ecs_iter_t* it) {
    // Run Update for all started scripts
    for (ecs_entity_t e : g_script_entities) {
        ScriptHost* shp = __get_script_host(e);
        if (!shp) continue;
        ScriptHost& sh = *shp;
        if (sh.started) {
            for (auto* s : sh.scripts) if (s) s->Update(g_current_dt);
        }
    }
}

static void ScriptLateUpdateSystem(ecs_iter_t* it) {
    // Run LateUpdate for all started scripts
    for (ecs_entity_t e : g_script_entities) {
        ScriptHost* shp = __get_script_host(e);
        if (!shp) continue;
        ScriptHost& sh = *shp;
        if (sh.started) {
            for (auto* s : sh.scripts) if (s) s->LateUpdate();
        }
    }
}

static void ScriptFixedUpdateSystem(ecs_iter_t* it) {
    // Run FixedUpdate for all started scripts
    for (ecs_entity_t e : g_script_entities) {
        ScriptHost* shp = __get_script_host(e);
        if (!shp) continue;
        ScriptHost& sh = *shp;
        if (sh.started) {
            for (auto* s : sh.scripts) if (s) s->FixedUpdate(g_fixed_dt);
        }
    }
}

// Register the script systems with appropriate ECS phases
static void register_script_systems(ecs_world_t* world) {
    if (g_systems_registered) return;
    
    // Awake system - runs early in the frame
    ecs_system_desc_t awake_desc = {0};
    awake_desc.entity = ecs_entity_init(world, &(ecs_entity_desc_t){ 
        .name = "UnitylikeScriptAwake", 
        .add = (ecs_id_t[]){ EcsOnLoad, 0 } 
    });
    awake_desc.callback = ScriptAwakeSystem;
    ecs_system_init(world, &awake_desc);
    
    // Start system - runs after Awake but before Update
    ecs_system_desc_t start_desc = {0};
    start_desc.entity = ecs_entity_init(world, &(ecs_entity_desc_t){ 
        .name = "UnitylikeScriptStart", 
        .add = (ecs_id_t[]){ EcsPreUpdate, 0 } 
    });
    start_desc.callback = ScriptStartSystem;
    ecs_system_init(world, &start_desc);
    
    // Update system - main update phase
    ecs_system_desc_t update_desc = {0};
    update_desc.entity = ecs_entity_init(world, &(ecs_entity_desc_t){ 
        .name = "UnitylikeScriptUpdate", 
        .add = (ecs_id_t[]){ EcsOnUpdate, 0 } 
    });
    update_desc.callback = ScriptUpdateSystem;
    ecs_system_init(world, &update_desc);
    
    // LateUpdate system - after main update
    ecs_system_desc_t late_update_desc = {0};
    late_update_desc.entity = ecs_entity_init(world, &(ecs_entity_desc_t){ 
        .name = "UnitylikeScriptLateUpdate", 
        .add = (ecs_id_t[]){ EcsPostUpdate, 0 } 
    });
    late_update_desc.callback = ScriptLateUpdateSystem;
    ecs_system_init(world, &late_update_desc);
    
    // FixedUpdate system - runs at fixed intervals
    // Note: For now we'll run this with regular update, but could be separated later
    ecs_system_desc_t fixed_update_desc = {0};
    fixed_update_desc.entity = ecs_entity_init(world, &(ecs_entity_desc_t){ 
        .name = "UnitylikeScriptFixedUpdate", 
        .add = (ecs_id_t[]){ EcsOnUpdate, 0 } 
    });
    fixed_update_desc.callback = ScriptFixedUpdateSystem;
    ecs_system_init(world, &fixed_update_desc);
    
    g_systems_registered = true;
    SDL_Log("[UnityLike] Script execution systems registered with ECS pipeline");
}

void __register_script_entity(ecs_world_t* /*w*/, std::uint64_t e) {
    ecs_entity_t ee = (ecs_entity_t)e;
    for (auto id : g_script_entities) { if (id == ee) return; }
    g_script_entities.push_back(ee);
}

ScriptHost* __get_script_host(std::uint64_t e){
    auto it = g_script_hosts.find((ecs_entity_t)e);
    if (it == g_script_hosts.end()) return nullptr;
    return &it->second;
}
void __ensure_script_host(std::uint64_t e){
    auto ee = (ecs_entity_t)e;
    if (!g_script_hosts.count(ee)) g_script_hosts.emplace(ee, ScriptHost{});
}
void __remove_script_host(std::uint64_t e){
    auto ee = (ecs_entity_t)e;
    g_script_hosts.erase(ee);
}

// Scene core
Scene::Scene(ecs_world_t* world) : world_(world) {
    assert(world_ != nullptr);
    ensure_components_registered(world_);
    register_script_systems(world_);
}

Scene::~Scene() {
    // Cleanup: delete all scripts from all hosts to avoid leaks
    for (auto& kv : g_script_hosts) {
        ScriptHost& sh = kv.second;
        for (auto* s : sh.scripts) {
            if (s) { s->OnDestroy(); delete s; }
        }
        sh.scripts.clear();
    }
    g_script_hosts.clear();
    g_script_entities.clear();
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
    ScriptHost* host = __get_script_host(go.id());
    if (host) {
        for (auto* s : host->scripts) { if (s) { s->OnDestroy(); delete s; } }
        host->scripts.clear();
        __remove_script_host(go.id());
    }
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

    // Pass 1: Awake all scripts on all entities before any Start
    for (ecs_entity_t e : g_script_entities) {
        ScriptHost* shp = __get_script_host(e);
        if (!shp) continue;
        ScriptHost& sh = *shp;
        if (!sh.awoken) {
            for (auto* s : sh.scripts) if (s) s->Awake();
            sh.awoken = true;
        }
    }

    // Pass 2: Start all scripts on all entities after Awake pass
    for (ecs_entity_t e : g_script_entities) {
        ScriptHost* shp = __get_script_host(e);
        if (!shp) continue;
        ScriptHost& sh = *shp;
        if (!sh.started) {
            for (auto* s : sh.scripts) if (s) s->Start();
            sh.started = true;
        }
    }

    // Pass 3: Regular Update and LateUpdate
    for (ecs_entity_t e : g_script_entities) {
        ScriptHost* shp = __get_script_host(e);
        if (!shp) continue;
        ScriptHost& sh = *shp;
        for (auto* s : sh.scripts) if (s) s->Update(dt);
        for (auto* s : sh.scripts) if (s) s->LateUpdate();
    }
}

void Scene::StepFixed(float fdt) {
    ensure_components_registered(world_);
    unitylike_set_fixed_dt(fdt);
    for (ecs_entity_t e : g_script_entities) {
        ScriptHost* shp = __get_script_host(e);
        if (!shp) continue;
        ScriptHost& sh = *shp;
        for (auto* s : sh.scripts) if (s) s->FixedUpdate(fdt);
    }
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
