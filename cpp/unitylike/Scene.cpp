#include "Scene.h"

// Implementation of the Unity-like facade MVP.
// This file intentionally leans on the existing C core via Flecs and AME C headers.
// Scope: Scene, GameObject name/creation, Transform (position/rotation/scale minimal), Rigidbody2D velocity, and minimal script hosting.

#include <flecs.h>
#include <cassert>
#include <cstring>

extern "C" {
#include "ame/physics.h"
}

namespace unitylike {

// Internal helpers and state
struct CompIds {
    ecs_entity_t transform = 0;   // AmeTransform2D
    ecs_entity_t body = 0;        // AmePhysicsBody
};
static CompIds g_comp;

ecs_entity_t g_comp_script_host = 0;

// Track entities that own scripts to avoid complex query API differences
static std::vector<ecs_entity_t> g_script_entities;

void __register_script_entity(ecs_world_t* /*w*/, std::uint64_t e) {
    ecs_entity_t ee = (ecs_entity_t)e;
    // Avoid duplicates
    for (auto id : g_script_entities) { if (id == ee) return; }
    g_script_entities.push_back(ee);
}

static void ensure_components_registered(ecs_world_t* w) {
    if (g_comp.transform == 0) {
        ecs_component_desc_t cdp = (ecs_component_desc_t){0};
        ecs_entity_desc_t edp = {0}; edp.name = "AmeTransform2D";
        cdp.entity = ecs_entity_init(w, &edp);
        cdp.type.size = (int32_t)sizeof(AmeTransform2D);
        cdp.type.alignment = (int32_t)alignof(AmeTransform2D);
        g_comp.transform = ecs_component_init(w, &cdp);
    }
    if (g_comp.body == 0) {
        ecs_component_desc_t cdp = (ecs_component_desc_t){0};
        ecs_entity_desc_t edp = {0}; edp.name = "AmePhysicsBody";
        cdp.entity = ecs_entity_init(w, &edp);
        cdp.type.size = (int32_t)sizeof(AmePhysicsBody);
        cdp.type.alignment = (int32_t)alignof(AmePhysicsBody);
        g_comp.body = ecs_component_init(w, &cdp);
    }
    if (g_comp_script_host == 0) {
        ecs_component_desc_t cdp = (ecs_component_desc_t){0};
        ecs_entity_desc_t edp = {0}; edp.name = "UnitylikeScriptHost";
        cdp.entity = ecs_entity_init(w, &edp);
        cdp.type.size = (int32_t)sizeof(ScriptHost);
        cdp.type.alignment = (int32_t)alignof(ScriptHost);
        g_comp_script_host = ecs_component_init(w, &cdp);
    }
}

// Scene
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
    if (!name.empty()) {
        // Cache the name locally; Flecs already stores it, but the facade keeps a cache too
        go.name(name);
    }
    return go;
}

void Scene::Destroy(GameObject& go) {
    if (!go.id()) return;
    // Cleanup scripts if present
    ScriptHost* host = (ScriptHost*)ecs_get_id(world_, (ecs_entity_t)go.id(), g_comp_script_host);
    if (host) {
        for (auto* s : host->scripts) {
            if (s) { s->OnDestroy(); delete s; }
        }
        host->scripts.clear();
    }
    ecs_delete(world_, (ecs_entity_t)go.id());
}

void Scene::Step(float dt) {
    ensure_components_registered(world_);
    // Iterate tracked entities with ScriptHost and call lifecycle
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
    for (ecs_entity_t e : g_script_entities) {
        ScriptHost* shp = (ScriptHost*)ecs_get_id(world_, e, g_comp_script_host);
        if (!shp) continue;
        ScriptHost& sh = *shp;
        for (auto* s : sh.scripts) if (s) s->FixedUpdate(fdt);
    }
}

// GameObject
bool GameObject::activeSelf() const {
    // MVP: no disabled state; always true
    return e_ != 0;
}

void GameObject::SetActive(bool /*v*/) {
    // TODO: Map to Flecs enable/disable or an Active tag
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
    // Update Flecs entity name too
    ecs_set_name(scene_->world(), (ecs_entity_t)e_, n.c_str());
}

Transform& GameObject::transform() {
    // Store a lightweight handle; Transform methods will use owner_ to look up data
    static thread_local Transform t{ GameObject() }; // simple non-owning handle initialized with dummy
    t = Transform{*this};
    return t;
}

// Transform
glm::vec3 Transform::position() const {
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    AmeTransform2D* tr = (AmeTransform2D*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.transform);
    if (!tr) return glm::vec3(0.0f);
    return glm::vec3(tr->x, tr->y, 0.0f);
}

void Transform::position(const glm::vec3& p) {
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    AmeTransform2D tr = { (float)p.x, (float)p.y, 0.0f };
    // Preserve angle if component exists
    if (AmeTransform2D* cur = (AmeTransform2D*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.transform)) {
        tr.angle = cur->angle;
    }
    ecs_set_id(w, (ecs_entity_t)owner_.id(), g_comp.transform, sizeof(AmeTransform2D), &tr);
}

glm::quat Transform::rotation() const {
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    AmeTransform2D* tr = (AmeTransform2D*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.transform);
    float a = (tr ? tr->angle : 0.0f);
    // 2D rotation around Z
    return glm::quat(glm::vec3(0.0f, 0.0f, a));
}

void Transform::rotation(const glm::quat& q) {
    // Extract Z angle; assuming q represents 2D rotation
    float a = 2.0f * std::atan2(std::sqrt(q.z*q.z + q.w*q.w) - q.w, q.z); // crude; could use glm::yaw in full glm
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    AmeTransform2D tr = { 0.0f, 0.0f, a };
    if (AmeTransform2D* cur = (AmeTransform2D*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.transform)) {
        tr.x = cur->x; tr.y = cur->y;
    }
    ecs_set_id(w, (ecs_entity_t)owner_.id(), g_comp.transform, sizeof(AmeTransform2D), &tr);
}

glm::vec3 Transform::localScale() const {
    // MVP: scale not stored; return 1
    return glm::vec3(1.0f, 1.0f, 1.0f);
}

void Transform::localScale(const glm::vec3& /*s*/) {
    // MVP: no-op
}

// Rigidbody2D
glm::vec2 Rigidbody2D::velocity() const {
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    AmePhysicsBody* pb = (AmePhysicsBody*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.body);
    if (!pb || !pb->body) return glm::vec2(0.0f);
    float vx = 0.f, vy = 0.f;
    ame_physics_get_velocity(pb->body, &vx, &vy);
    return glm::vec2(vx, vy);
}

void Rigidbody2D::velocity(const glm::vec2& v) {
    ecs_world_t* w = owner_.scene()->world();
    ensure_components_registered(w);
    AmePhysicsBody* pb = (AmePhysicsBody*)ecs_get_id(w, (ecs_entity_t)owner_.id(), g_comp.body);
    if (!pb || !pb->body) return;
    ame_physics_set_velocity(pb->body, v.x, v.y);
}

bool Rigidbody2D::isKinematic() const {
    // MVP: not tracked; treat as dynamic unless marked differently. Requires physics API to query type.
    return false;
}

void Rigidbody2D::isKinematic(bool /*v*/) {
    // TODO: Expose in physics C API if needed; placeholder no-op for MVP
}

} // namespace unitylike
