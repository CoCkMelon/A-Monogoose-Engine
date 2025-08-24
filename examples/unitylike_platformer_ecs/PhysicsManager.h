#pragma once
#include "unitylike/Scene.h"
extern "C" {
#include "ame/physics.h"
#include "ame/tilemap_tmx.h"
}
#include <string>

using namespace unitylike;

// Singleton physics manager (Unity-like pattern)
class PhysicsManager : public MongooseBehaviour {
public:
    // Inspector fields
    float gravityX = 0.0f;
    float gravityY = -1000.0f;
    float fixedTimeStep = 1.0f / 60.0f;
    std::string tilemapPath = "";
    
private:
    static PhysicsManager* instance;
    AmePhysicsWorld* physicsWorld = nullptr;
    
public:
    // Singleton access
    static PhysicsManager* GetInstance() { return instance; }
    static AmePhysicsWorld* GetWorld() { 
        return instance ? instance->physicsWorld : nullptr; 
    }
    
    void Awake() override {
        // Set singleton instance
        if (instance != nullptr) {
            // Destroy duplicate
            gameObject().scene()->Destroy(gameObject());
            return;
        }
        instance = this;
    }
    
    void Start() override {
        // Create physics world
        physicsWorld = ame_physics_world_create(gravityX, gravityY, fixedTimeStep);
        
        // Load tilemap collisions if path provided
        if (!tilemapPath.empty()) {
            LoadTilemapCollisions();
        }
    }
    
    void FixedUpdate(float fixedDeltaTime) override {
        if (physicsWorld) {
            ame_physics_world_step(physicsWorld);
        }
    }
    
    void OnDestroy() override {
        if (physicsWorld) {
            ame_physics_world_destroy(physicsWorld);
            physicsWorld = nullptr;
        }
        if (instance == this) {
            instance = nullptr;
        }
    }
    
    // Create dynamic body
    b2Body* CreateDynamicBody(float x, float y, float width, float height, bool isSensor = false) {
        if (!physicsWorld) return nullptr;
        return ame_physics_create_body(physicsWorld, x, y, width, height, 
                                       AME_BODY_DYNAMIC, isSensor, nullptr);
    }
    
    // Create static body
    b2Body* CreateStaticBody(float x, float y, float width, float height, bool isSensor = false) {
        if (!physicsWorld) return nullptr;
        return ame_physics_create_body(physicsWorld, x, y, width, height, 
                                       AME_BODY_STATIC, isSensor, nullptr);
    }
    
    // Create kinematic body
    b2Body* CreateKinematicBody(float x, float y, float width, float height, bool isSensor = false) {
        if (!physicsWorld) return nullptr;
        return ame_physics_create_body(physicsWorld, x, y, width, height, 
                                       AME_BODY_KINEMATIC, isSensor, nullptr);
    }
    
private:
    void LoadTilemapCollisions() {
        AmeTilemapTmxLoadResult tmx{};
        if (ame_tilemap_load_tmx_for_gpu(tilemapPath.c_str(), &tmx)) {
            // Find collision layer
            int collisionLayer = tmx.collision_layer_index;
            if (collisionLayer < 0 && tmx.layer_count > 0) {
                collisionLayer = 0;  // Use first layer as fallback
            }
            
            if (collisionLayer >= 0) {
                const auto& layer = tmx.layers[collisionLayer];
                ame_physics_create_tilemap_collision(
                    physicsWorld,
                    (const int*)layer.map.layer0.data,
                    layer.map.width,
                    layer.map.height,
                    (float)layer.map.tile_width
                );
            }
            
            ame_tilemap_free_tmx_result(&tmx);
        }
    }
};

// Static instance definition
PhysicsManager* PhysicsManager::instance = nullptr;
