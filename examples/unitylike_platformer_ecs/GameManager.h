#pragma once
#include "unitylike/Scene.h"
#include "PlayerBehaviour.h"
#include "CameraController.h"
#include "PhysicsManager.h"
#include <string>
#include <memory>

using namespace unitylike;

// Main game manager - Unity-like pattern for scene setup
class GameManager : public MongooseBehaviour {
public:
    // Configuration (Inspector fields in Unity)
    std::string tilemapPath = "examples/unitylike_pixel_platformer/Tiled/tilemap-example-a.tmx";
    std::string playerSpritePath = "examples/kenney_pixel-platformer/brackeys_platformer_assets/sprites/knight.png";
    
    // Initial settings
    float cameraZoom = 3.0f;
    int screenWidth = 1280;
    int screenHeight = 720;
    glm::vec2 playerStartPosition = {64.0f, 64.0f};
    
private:
    GameObject playerObject;
    GameObject cameraObject;
    GameObject physicsManagerObject;
    GameObject tilemapObject;
    
    PlayerBehaviour* playerBehaviour = nullptr;
    CameraController* cameraController = nullptr;
    PhysicsManager* physicsManager = nullptr;
    
    // TMX data (kept alive for tilemap rendering)
    std::unique_ptr<AmeTilemapTmxLoadResult> tmxData;
    
public:
    void Start() override {
        // Create physics manager first
        SetupPhysics();
        
        // Create tilemap
        SetupTilemap();
        
        // Create player
        SetupPlayer();
        
        // Create camera
        SetupCamera();
        
        // Link components
        LinkComponents();
    }
    
    void OnDestroy() override {
        // Clean up TMX data
        if (tmxData) {
            ame_tilemap_free_tmx_result(tmxData.get());
            tmxData.reset();
        }
    }
    
    void SetViewport(int width, int height) {
        screenWidth = width;
        screenHeight = height;
        if (cameraController) {
            cameraController->SetViewport(width, height);
        }
    }
    
private:
    void SetupPhysics() {
        physicsManagerObject = gameObject().scene()->Create("PhysicsManager");
        physicsManager = &physicsManagerObject.AddScript<PhysicsManager>();
        physicsManager->tilemapPath = tilemapPath;
        physicsManager->gravityY = -1000.0f;
        physicsManager->fixedTimeStep = 1.0f / 60.0f;
    }
    
    void SetupTilemap() {
        tmxData = std::make_unique<AmeTilemapTmxLoadResult>();
        
        if (ame_tilemap_load_tmx_for_gpu(tilemapPath.c_str(), tmxData.get())) {
            if (tmxData->layer_count > 0) {
                // Create tilemap entity for each layer (render order = layer index)
                const int maxLayers = tmxData->layer_count;
                for (int i = 0; i < maxLayers; ++i) {
                    auto layerObject = gameObject().scene()->Create("TilemapLayer" + std::to_string(i));
                    
                    // Set tilemap data for rendering
                    TilemapRefData tref{};
                    tref.map = &tmxData->layers[i].map;
                    tref.layer = i;
                    tref.atlas_tex = tmxData->layers[i].atlas_tex;
                    tref.gid_tex = tmxData->layers[i].gid_tex;
                    tref.atlas_w = tmxData->layers[i].atlas_w;
                    tref.atlas_h = tmxData->layers[i].atlas_h;
                    tref.tile_w = tmxData->layers[i].map.tile_width;
                    tref.tile_h = tmxData->layers[i].map.tile_height;
                    tref.firstgid = tmxData->layers[i].firstgid;
                    tref.columns = tmxData->layers[i].columns;
                    tref.map_w = tmxData->layers[i].map.width;
                    tref.map_h = tmxData->layers[i].map.height;
                    
                    // Write component directly
                    ecs_world_t* w = gameObject().scene()->world();
                    ensure_components_registered(w);
                    ecs_set_id(w, (ecs_entity_t)layerObject.id(), g_comp.tilemap, 
                              sizeof(TilemapRefData), &tref);
                    
                    if (i == 0) {
                        tilemapObject = layerObject;
                    }
                }
            }
        }
    }
    
    void SetupPlayer() {
        playerObject = gameObject().scene()->Create("Player");
        playerObject.transform().position({playerStartPosition.x, playerStartPosition.y, 0.0f});
        
        // Add player behaviour
        playerBehaviour = &playerObject.AddScript<PlayerBehaviour>();
        playerBehaviour->moveSpeed = 180.0f;
        playerBehaviour->jumpForce = 450.0f;
        
        // Load and set player texture
        LoadPlayerSprite();
    }
    
    void SetupCamera() {
        cameraObject = gameObject().scene()->Create("MainCamera");
        
        // Add camera controller
        cameraController = &cameraObject.AddScript<CameraController>();
        cameraController->zoom = cameraZoom;
        cameraController->smoothSpeed = 5.0f;
        cameraController->SetViewport(screenWidth, screenHeight);
    }
    
    void LinkComponents() {
        // Link player to physics
        if (playerBehaviour && physicsManager) {
            playerBehaviour->SetPhysicsWorld(PhysicsManager::GetWorld());
        }
        
        // Link camera to player
        if (cameraController && playerObject.id() != 0) {
            cameraController->SetTarget(&playerObject);
        }
    }
    
    void LoadPlayerSprite() {
        if (!playerBehaviour) return;
        
        // In a real implementation, this would load the texture and assign it
        // For now, we'll just set up the sprite renderer component
        auto* spriteRenderer = playerObject.TryGetComponent<SpriteRenderer>();
        if (spriteRenderer) {
            // Texture loading would happen here
            // spriteRenderer->texture(loadedTextureId);
            spriteRenderer->size({16.0f, 16.0f});
            spriteRenderer->color({1.0f, 1.0f, 1.0f, 1.0f});
        }
    }
};
