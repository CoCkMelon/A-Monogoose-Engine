#pragma once
#include "unitylike/Scene.h"
extern "C" {
#include "ame/physics.h"
#include "ame/tilemap_tmx.h"
#include <SDL3/SDL.h>
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
    bool awakeWasCalled = false;
    
public:
    // Singleton access
    static PhysicsManager* GetInstance() { return instance; }
    static AmePhysicsWorld* GetWorld() { 
        return instance ? instance->physicsWorld : nullptr; 
    }
    
    void Awake() override {
        if (awakeWasCalled) {
            SDL_Log("PhysicsManager: Awake already called, skipping");
            return;
        }
        awakeWasCalled = true;
        
        SDL_Log("PhysicsManager: Awake called");
        // Set singleton instance
        if (instance != nullptr) {
            SDL_Log("PhysicsManager: Duplicate instance found, destroying");
            // Destroy duplicate
            gameObject().scene()->Destroy(gameObject());
            return;
        }
        instance = this;
        SDL_Log("PhysicsManager: Set as singleton instance");
        
        // Create physics world immediately
        if (!physicsWorld) {
            physicsWorld = ame_physics_world_create(gravityX, gravityY, fixedTimeStep);
            SDL_Log("PhysicsManager: Physics world created: %p", physicsWorld);
        }
    }
    
    void Start() override {
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
        // Use the exact same collision loading pattern as the working kenney_pixel-platformer
        // This implementation directly copies the working pattern from main.c lines ~1044-1052
        
        SDL_Log("PhysicsManager: Loading tilemap collisions from: %s", tilemapPath.c_str());
        
        AmeTilemapTmxLoadResult tmx{};
        if (!ame_tilemap_load_tmx_for_gpu(tilemapPath.c_str(), &tmx)) {
            SDL_Log("PhysicsManager: Failed to load TMX from: %s", tilemapPath.c_str());
            return;
        }
        
        SDL_Log("PhysicsManager: Loaded TMX with %d layers", tmx.layer_count);
        
        // Find the collision layer using the TMX result's collision_layer_index
        const AmeTilemap* coll_map = nullptr;
        int collision_layer_index = tmx.collision_layer_index;
        
        if (collision_layer_index >= 0 && collision_layer_index < tmx.layer_count) {
            coll_map = &tmx.layers[collision_layer_index].map;
            SDL_Log("PhysicsManager: Found collision layer at index %d", collision_layer_index);
        } else if (tmx.layer_count > 0) {
            coll_map = &tmx.layers[0].map;
            collision_layer_index = 0;
            SDL_Log("PhysicsManager: No collision layer found, using first layer (index 0)");
        }
        
        if (coll_map) {
            SDL_Log("PhysicsManager: Creating collision for layer %d: %dx%d tiles, tile size: %dx%d", 
                   collision_layer_index, coll_map->width, coll_map->height, 
                   coll_map->tile_width, coll_map->tile_height);
            
            // Use the EXACT same call pattern as the working example (line 1051)
            ame_physics_create_tilemap_collision(
                physicsWorld, 
                coll_map->layer0.data, 
                coll_map->width, 
                coll_map->height, 
                (float)coll_map->tile_width
            );
            
            SDL_Log("PhysicsManager: Tilemap collision created successfully");
        } else {
            SDL_Log("PhysicsManager: No suitable collision layer found");
        }
        
        // Clean up TMX data
        ame_tilemap_free_tmx_result(&tmx);
    }
};

// Static instance definition
PhysicsManager* PhysicsManager::instance = nullptr;
