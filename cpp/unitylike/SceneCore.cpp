#include "Scene.h"
#include <flecs.h>
#include <cassert>
#include <unordered_map>
#include <vector>

namespace unitylike {

// Globals declared in header (remove duplicate definitions)
// CompIds g_comp{};
// ecs_entity_t g_comp_script_host = 0;

static std::vector<ecs_entity_t> g_script_entities;
static std::unordered_map<ecs_entity_t, ScriptHost> g_script_hosts;

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


} // namespace unitylike
