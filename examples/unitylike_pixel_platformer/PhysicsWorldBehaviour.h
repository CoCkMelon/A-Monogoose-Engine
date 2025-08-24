#pragma once
#include "unitylike/Scene.h"
extern "C" {
#include "ame/physics.h"
#include "ame/tilemap_tmx.h"
}
#include <string>

using namespace unitylike;

// Global accessors for the physics world (simple MVP singleton)
inline AmePhysicsWorld*& __physics_world_singleton() { static AmePhysicsWorld* g = nullptr; return g; }
inline AmePhysicsWorld* physics_world_get() { return __physics_world_singleton(); }

class PhysicsWorldBehaviour : public MongooseBehaviour {
public:
    // Config
    std::string tmxPath = "examples/unitylike_pixel_platformer/Tiled/tilemap-example-a.tmx";
    float gravityX = 0.0f;
    float gravityY = -1000.0f;
    float fixedTimeStep = 1.0f/60.0f;

    void Start() override {
        // Create world
        __physics_world_singleton() = ame_physics_world_create(gravityX, gravityY, fixedTimeStep);
        // Load TMX for colliders
        AmeTilemapTmxLoadResult tmx{};
        if (ame_tilemap_load_tmx_for_gpu(tmxPath.c_str(), &tmx)) {
            int coll = (tmx.collision_layer_index >= 0 ? tmx.collision_layer_index : (tmx.layer_count>0?0:-1));
            if (coll >= 0) {
                const auto& L = tmx.layers[coll];
                ame_physics_create_tilemap_collision(physics_world_get(), (const int*)L.map.layer0.data, L.map.width, L.map.height, (float)L.map.tile_width);
            }
        }
        ame_tilemap_free_tmx_result(&tmx);
    }
    void FixedUpdate(float /*fdt*/) override {
        if (physics_world_get()) ame_physics_world_step(physics_world_get());
    }
    void OnDestroy() override {
        if (physics_world_get()) { ame_physics_world_destroy(physics_world_get()); __physics_world_singleton() = nullptr; }
    }
};

