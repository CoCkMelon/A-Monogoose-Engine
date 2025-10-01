#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

extern "C" {
#include "ame/scene_builder.h"
#include "ame/prefab.h"
}

struct ecs_world_t;

namespace unitylike {

// A lightweight, code-first scene asset that mirrors the YAML scene model
// and can be instantiated into a Flecs world.
class SceneAsset {
public:
    SceneAsset(const std::string& name = "Scene", const std::string& version = "1.0.0");
    ~SceneAsset();

    SceneAsset(const SceneAsset&) = delete;
    SceneAsset& operator=(const SceneAsset&) = delete;
    SceneAsset(SceneAsset&&) noexcept;
    SceneAsset& operator=(SceneAsset&&) noexcept;

    // Non-owning wrapper around an existing AmeScene (will not destroy on dtor)
    static SceneAsset WrapNonOwning(AmeScene* raw);

    // Entity handle for fluent construction
    class Entity {
    public:
        Entity() = default;
        Entity(AmeScene* sc, AmeEntity id) : sc_(sc), id_(id) {}
        bool valid() const { return sc_ != nullptr && id_ != 0; }
        AmeEntity id() const { return id_; }

        Entity& enabled(bool v);
        Entity& tag(const std::string& t);
        Entity& transform(float x, float y, float angle_rad = 0.0f);
        Entity& camera(const AmeCamera& c);
        Entity& setParent(const Entity& parent);
    private:
        AmeScene* sc_ = nullptr;
        AmeEntity id_ = 0;
    };

    // Create an entity by name
    Entity create(const std::string& name);

    // Instantiate prefab by name. Returns the root entity created by the prefab.
    Entity instantiatePrefab(const std::string& prefabName);
    Entity instantiatePrefab(const std::string& prefabName, const std::string& instanceName);
    Entity instantiatePrefab(const std::string& prefabName, const std::string& instanceName, const Entity& parent);

    // Set top-level metadata
    void setAuthor(const std::string& author);
    void setDescription(const std::string& description);

    // Instantiate into a Flecs world. Returns true on success.
    bool instantiateToWorld(ecs_world_t* world) const;

    // Access raw pointer if needed for advanced operations
    AmeScene* raw() const { return scene_; }

private:
    // Private helper to build from raw with ownership flag
    SceneAsset(AmeScene* raw, bool own);

    AmeScene* scene_ = nullptr;
    bool own_ = true;
};

// C++ sugar around prefab registration that binds a lambda/functor.
// Builder receives the SceneAsset::Entity parent (may be invalid) and instance name, and returns the created root entity.
using PrefabBuilderFn = std::function<SceneAsset::Entity(SceneAsset&, SceneAsset::Entity parent, const std::string& instanceName)>;

// Register a prefab builder under a key. Returns true on success.
bool RegisterPrefab(const std::string& key, PrefabBuilderFn fn);

} // namespace unitylike
