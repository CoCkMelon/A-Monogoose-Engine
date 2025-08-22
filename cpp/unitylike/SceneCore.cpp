#include "Scene.h"
#include <flecs.h>
#include <cassert>
#include <vector>

namespace unitylike {

// Globals declared in header
CompIds g_comp{};
ecs_entity_t g_comp_script_host = 0;

static std::vector<ecs_entity_t> g_script_entities;

void __register_script_entity(ecs_world_t* /*w*/, std::uint64_t e) {
    ecs_entity_t ee = (ecs_entity_t)e;
    for (auto id : g_script_entities) { if (id == ee) return; }
    g_script_entities.push_back(ee);
}

// Scene core
Scene::Scene(ecs_world_t* world) : world_(world) {
    assert(world_ != nullptr);
    ensure_components_registered(world_);
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
    ScriptHost* host = (ScriptHost*)ecs_get_id(world_, (ecs_entity_t)go.id(), g_comp_script_host);
    if (host) {
        for (auto* s : host->scripts) { if (s) { s->OnDestroy(); delete s; } }
        host->scripts.clear();
    }
    ecs_delete(world_, (ecs_entity_t)go.id());
}

extern void unitylike_begin_update(float dt);
extern void unitylike_set_fixed_dt(float fdt);

void Scene::Step(float dt) {
    ensure_components_registered(world_);
    unitylike_begin_update(dt);
    for (ecs_entity_t e : g_script_entities) {
        ScriptHost* shp = (ScriptHost*)ecs_get_id(world_, e, g_comp_script_host);
        if (!shp) continue;
        ScriptHost& sh = *shp;
        if (!sh.awoken) { for (auto* s : sh.scripts) if (s) s->Awake(); sh.awoken = true; }
        if (!sh.started) { for (auto* s : sh.scripts) if (s) s->Start(); sh.started = true; }
        for (auto* s : sh.scripts) if (s) s->Update(dt);
        for (auto* s : sh.scripts) if (s) s->LateUpdate();
    }
}

void Scene::StepFixed(float fdt) {
    ensure_components_registered(world_);
    unitylike_set_fixed_dt(fdt);
    for (ecs_entity_t e : g_script_entities) {
        ScriptHost* shp = (ScriptHost*)ecs_get_id(world_, e, g_comp_script_host);
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

} // namespace unitylike
