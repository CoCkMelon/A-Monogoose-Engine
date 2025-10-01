#include "SceneAsset.h"
#include <unordered_map>

namespace unitylike {

SceneAsset::SceneAsset(const std::string& name, const std::string& version) {
    scene_ = ame_scene_create(name.c_str(), version.c_str());
    own_ = true;
}

SceneAsset::SceneAsset(AmeScene* raw, bool own) : scene_(raw), own_(own) {}

SceneAsset SceneAsset::WrapNonOwning(AmeScene* raw) {
    return SceneAsset(raw, false);
}

SceneAsset::~SceneAsset() {
    if (scene_ && own_) ame_scene_destroy(scene_);
}

SceneAsset::SceneAsset(SceneAsset&& other) noexcept {
    scene_ = other.scene_; own_ = other.own_;
    other.scene_ = nullptr; other.own_ = true;
}
SceneAsset& SceneAsset::operator=(SceneAsset&& other) noexcept {
    if (this != &other) {
        if (scene_ && own_) ame_scene_destroy(scene_);
        scene_ = other.scene_; own_ = other.own_;
        other.scene_ = nullptr; other.own_ = true;
    }
    return *this;
}

SceneAsset::Entity SceneAsset::create(const std::string& name) {
    AmeEntity e = ame_scene_add_entity(scene_, name.c_str());
    return Entity(scene_, e);
}

SceneAsset::Entity SceneAsset::instantiatePrefab(const std::string& prefabName) {
    AmeEntity e = ame_prefab_instantiate(scene_, prefabName.c_str(), 0, nullptr);
    return Entity(scene_, e);
}
SceneAsset::Entity SceneAsset::instantiatePrefab(const std::string& prefabName, const std::string& instanceName) {
    AmeEntity e = ame_prefab_instantiate(scene_, prefabName.c_str(), 0, instanceName.empty()?nullptr:instanceName.c_str());
    return Entity(scene_, e);
}
SceneAsset::Entity SceneAsset::instantiatePrefab(const std::string& prefabName, const std::string& instanceName, const Entity& parent) {
    AmeEntity p = parent.valid() ? parent.id() : 0;
    AmeEntity e = ame_prefab_instantiate(scene_, prefabName.c_str(), p, instanceName.empty() ? nullptr : instanceName.c_str());
    return Entity(scene_, e);
}

void SceneAsset::setAuthor(const std::string& author) { ame_scene_set_author(scene_, author.c_str()); }
void SceneAsset::setDescription(const std::string& desc) { ame_scene_set_description(scene_, desc.c_str()); }

bool SceneAsset::instantiateToWorld(ecs_world_t* world) const { return ame_scene_instantiate_to_world(scene_, world); }

// Entity methods
SceneAsset::Entity& SceneAsset::Entity::enabled(bool v) { if (valid()) ame_scene_entity_set_enabled(sc_, id_, v); return *this; }
SceneAsset::Entity& SceneAsset::Entity::tag(const std::string& t) { if (valid()) ame_scene_entity_add_tag(sc_, id_, t.c_str()); return *this; }
SceneAsset::Entity& SceneAsset::Entity::transform(float x, float y, float angle_rad) {
    if (valid()) { AmeTransform2D tr; tr.x = x; tr.y = y; tr.angle = angle_rad; ame_scene_entity_set_transform(sc_, id_, tr); }
    return *this;
}
SceneAsset::Entity& SceneAsset::Entity::camera(const AmeCamera& c) { if (valid()) ame_scene_entity_set_camera(sc_, id_, &c); return *this; }
SceneAsset::Entity& SceneAsset::Entity::setParent(const Entity& parent) { if (valid() && parent.valid()) ame_scene_set_parent(sc_, id_, parent.id_); return *this; }

// Prefab registration adapter
namespace {
    struct Holder { PrefabBuilderFn fn; };

    static AmeEntity PrefabThunk(AmeScene* sc, AmeEntity parent, const char* instance_name, void* ud) {
        Holder* h = reinterpret_cast<Holder*>(ud);
        // Wrap provided scene without taking ownership
        SceneAsset asset = SceneAsset::WrapNonOwning(sc);
        SceneAsset::Entity parentEnt(sc, parent);
        std::string inst = instance_name ? std::string(instance_name) : std::string();
        SceneAsset::Entity created = h->fn(asset, parentEnt, inst);
        return created.valid() ? created.id() : 0;
    }
}

bool RegisterPrefab(const std::string& key, PrefabBuilderFn fn) {
    if (key.empty() || !fn) return false;
    Holder* h = new Holder{std::move(fn)}; // leaked intentionally for process lifetime
    int ok = ame_prefab_register(key.c_str(), PrefabThunk, (void*)h);
    if (!ok) { delete h; return false; }
    return true;
}

} // namespace unitylike
