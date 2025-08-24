#pragma once
#include "unitylike/Scene.h"
#include "PlayerBehaviour.h"
#include "CameraController.h"
#include "PhysicsManager.h"
#include <string>
#include <memory>

extern "C" {
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <glad/gl.h>
}

using namespace unitylike;

// Main game manager - Unity-like pattern for scene setup
class GameManager : public MongooseBehaviour {
public:
    // Configuration (Inspector fields in Unity)
    std::string tilemapPath = "examples/kenney_pixel-platformer/Tiled/tilemap-example-a.tmx";
    std::string playerSpritePath = "examples/kenney_pixel-platformer/Tilemap/tilemap-characters_packed.png";
    
    // Initial settings
    float cameraZoom = 3.0f;
    int screenWidth = 1280;
    int screenHeight = 720;
    glm::vec2 playerStartPosition = {200.0f, 100.0f};
    
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
        SDL_Log("GameManager: Setting up physics manager");
        physicsManagerObject = gameObject().scene()->Create("PhysicsManager");
        physicsManager = &physicsManagerObject.AddScript<PhysicsManager>();
        physicsManager->tilemapPath = tilemapPath;
        physicsManager->gravityY = -1000.0f;
        physicsManager->fixedTimeStep = 1.0f / 60.0f;
        
        // Force Awake call to initialize physics world immediately
        if (physicsManager && !PhysicsManager::GetWorld()) {
            SDL_Log("GameManager: Manually triggering physics manager Awake");
            physicsManager->Awake();
        }
        
        SDL_Log("GameManager: Physics manager created, world: %p", PhysicsManager::GetWorld());
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
        SDL_Log("GameManager: Linking components");
        // Link player to physics
        if (playerBehaviour && physicsManager) {
            AmePhysicsWorld* world = PhysicsManager::GetWorld();
            SDL_Log("GameManager: Physics world available: %p", world);
            playerBehaviour->SetPhysicsWorld(world);
        } else {
            SDL_Log("GameManager: Missing components - player: %p, physics: %p", playerBehaviour, physicsManager);
        }
        
        // Link camera to player
        if (cameraController && playerObject.id() != 0) {
            cameraController->SetTarget(&playerObject);
        }
        
        // Check and adjust player spawn position for collision safety
        TestAndAdjustPlayerSpawnPosition();
    }
    
    void LoadPlayerSprite() {
        if (!playerBehaviour) {
            SDL_Log("GameManager: No playerBehaviour to load sprite for");
            return;
        }
        
        SDL_Log("GameManager: Loading player sprite from: %s", playerSpritePath.c_str());
        // Load the character atlas texture
        GLuint textureId = LoadTexture(playerSpritePath.c_str());
        SDL_Log("GameManager: Loaded texture ID: %u", textureId);
        if (textureId != 0) {
            // Set the texture on the player behaviour so it can configure the sprite renderer properly
            playerBehaviour->SetPlayerTexture(textureId);
            SDL_Log("GameManager: Player texture set successfully");
        } else {
            SDL_Log("GameManager: Failed to load player texture");
        }
    }
    
private:
    void TestAndAdjustPlayerSpawnPosition() {
        // Test if the player spawn position is safe (no collision)
        AmePhysicsWorld* world = PhysicsManager::GetWorld();
        if (!world) return;
        
        glm::vec3 currentPos = playerObject.transform().position();
        
        // Test current position with a small raycast downward
        AmeRaycastHit hit = ame_physics_raycast(world, 
                                                currentPos.x, currentPos.y + 8.0f,  // Start slightly above
                                                currentPos.x, currentPos.y - 8.0f); // End slightly below
        
        if (hit.hit) {
            // Player is spawning too close to collision, move higher
            float safeY = hit.point_y + 32.0f; // Move 32 pixels above the collision
            playerObject.transform().position({currentPos.x, safeY, currentPos.z});
            SDL_Log("Player spawn adjusted to avoid collision: (%.1f, %.1f) -> (%.1f, %.1f)", 
                   currentPos.x, currentPos.y, currentPos.x, safeY);
        } else {
            SDL_Log("Player spawn position is safe: (%.1f, %.1f)", currentPos.x, currentPos.y);
        }
    }
    
    GLuint LoadTexture(const char* path) {
        SDL_Log("GameManager: Attempting to load texture: %s", path);
        SDL_Surface* surface = IMG_Load(path);
        if (!surface) {
            SDL_Log("Failed to load texture %s: %s", path, SDL_GetError());
            return 0;
        }
        SDL_Log("GameManager: Surface loaded successfully: %dx%d", surface->w, surface->h);
        
        SDL_Surface* converted = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(surface);
        
        if (!converted) {
            SDL_Log("Failed to convert texture %s: %s", path, SDL_GetError());
            return 0;
        }
        
        GLuint textureId = 0;
        glGenTextures(1, &textureId);
        glBindTexture(GL_TEXTURE_2D, textureId);
        
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, converted->w, converted->h, 0, 
                     GL_RGBA, GL_UNSIGNED_BYTE, converted->pixels);
        
        SDL_DestroySurface(converted);
        
        return textureId;
    }
};
