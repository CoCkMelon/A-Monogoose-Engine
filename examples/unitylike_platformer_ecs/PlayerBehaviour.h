#pragma once
#include "unitylike/Scene.h"
#include "input_local.h"
#include <glad/gl.h>
extern "C" {
#include "ame/physics.h"
#include <SDL3/SDL.h>
}
#include <cmath>

using namespace unitylike;

// Unity-like player controller using component pattern
class PlayerBehaviour : public MongooseBehaviour {
public:
    // Inspector fields (Unity-like serialized fields)
    float moveSpeed = 180.0f;
    float jumpForce = 450.0f;
    float gravityScale = 1.0f;
    
    // Sprite animation settings
    int idleFrame = 0;
    int walkFrame1 = 1;
    int walkFrame2 = 2;
    int jumpFrame = 3;
    float animationSpeed = 10.0f;
    
private:
    // Components (cached references)
    SpriteRenderer* spriteRenderer = nullptr;
    Transform* cachedTransform = nullptr;
    
    // Physics
    b2Body* physicsBody = nullptr;
    AmePhysicsWorld* physicsWorld = nullptr;
    
    // Animation state
    float animationTime = 0.0f;
    bool facingRight = true;
    
    // Input state
    float horizontalInput = 0.0f;
    bool jumpPressed = false;
    bool isGrounded = false;
    
public:
    void Awake() override {
        // Cache component references (Unity pattern)
        spriteRenderer = gameObject().TryGetComponent<SpriteRenderer>();
        if (!spriteRenderer) {
            spriteRenderer = &gameObject().AddComponent<SpriteRenderer>();
        }
        cachedTransform = &gameObject().transform();
    }
    
    void Start() override {
        // Initialize sprite renderer with default values (will be overridden by GameManager)
        if (spriteRenderer) {
            spriteRenderer->size({16.0f, 16.0f});
            spriteRenderer->color({0.8f, 0.8f, 1.0f, 1.0f});  // Light blue fallback
            spriteRenderer->sortingLayer(1);
            spriteRenderer->orderInLayer(10);
        }
        
        // Create physics body if physics world is available
        if (physicsWorld && !physicsBody) {
            SDL_Log("PlayerBehaviour: Creating physics body in Start()");
            CreatePhysicsBody();
        }
    }
    
    void Update(float deltaTime) override {
        // Update animation
        UpdateAnimation(deltaTime);
        
        // Sync transform with physics
        if (physicsBody) {
            float px, py;
            ame_physics_get_position(physicsBody, &px, &py);
            cachedTransform->position({px, py, 0.0f});
        }
    }
    
    void FixedUpdate(float fixedDeltaTime) override {
        ProcessInput();
        // Apply physics-based movement
        if (physicsBody) {
            float vx, vy;
            ame_physics_get_velocity(physicsBody, &vx, &vy);
            
            // Horizontal movement
            vx = moveSpeed * horizontalInput;
            
            // Jump
            if (jumpPressed && CheckGrounded(vy)) {
                vy = jumpForce;
            }
            
            ame_physics_set_velocity(physicsBody, vx, vy);
        }
    }
    
    void OnDestroy() override {
        // Cleanup physics body
        if (physicsBody && physicsWorld) {
            ame_physics_destroy_body(physicsWorld, physicsBody);
            physicsBody = nullptr;
        }
    }
    
    // Set the physics world reference
    void SetPhysicsWorld(AmePhysicsWorld* world) {
        physicsWorld = world;
        SDL_Log("PlayerBehaviour: SetPhysicsWorld called with world: %p", world);
        // Physics body will be created in Start() where GameObject is guaranteed to be valid
    }
    
    // Set the player texture (called by GameManager)
    void SetPlayerTexture(GLuint textureId) {
        if (spriteRenderer && textureId != 0) {
            spriteRenderer->texture(textureId);
            // Set sprite size to match character tile dimensions (24x24)
            spriteRenderer->size({24.0f, 24.0f});
            spriteRenderer->color({1.0f, 1.0f, 1.0f, 1.0f});
            spriteRenderer->sortingLayer(2);  // Above tilemap
            spriteRenderer->orderInLayer(0);
            
            // Initialize UV coordinates for first frame (idle - first character)
            // Character atlas is 9 columns x 3 rows, each tile 24x24
            float frameWidth = 1.0f / 9.0f;
            float frameHeight = 1.0f / 3.0f;
            spriteRenderer->uv(0.0f, 0.0f, frameWidth, frameHeight);
        }
    }
    
private:
    void CreatePhysicsBody() {
        if (!physicsWorld) {
            SDL_Log("PlayerBehaviour: Cannot create physics body - no physics world");
            return;
        }
        
        if (physicsBody) {
            SDL_Log("PlayerBehaviour: Physics body already exists, skipping creation");
            return;
        }
        
        // Ensure we have a valid transform
        if (!cachedTransform) {
            SDL_Log("PlayerBehaviour: Cached transform is null, refreshing");
            cachedTransform = &gameObject().transform();
            if (!cachedTransform) {
                SDL_Log("PlayerBehaviour: Still cannot get transform, aborting physics body creation");
                return;
            }
        }
        
        auto pos = cachedTransform->position();
        SDL_Log("PlayerBehaviour: Creating physics body at position (%.1f, %.1f)", pos.x, pos.y);
        physicsBody = ame_physics_create_body(
            physicsWorld, 
            pos.x, pos.y, 
            16.0f, 16.0f,  // Player size
            AME_BODY_DYNAMIC, 
            false,  // Not a sensor
            nullptr
        );
        SDL_Log("PlayerBehaviour: Physics body created: %p", physicsBody);
    }
    
    void ProcessInput() {
        // Get horizontal input
        horizontalInput = 0.0f;
        int inputDir = input_move_dir();
        horizontalInput = (float)inputDir;
        
        // Get jump input (edge detection handled by input_local)
        jumpPressed = input_jump_edge();
        
        // Debug output (only when input is detected)
        if (inputDir != 0 || jumpPressed) {
            SDL_Log("PlayerBehaviour: Input - dir: %d (%.1f), jump: %d, physicsBody: %p", 
                   inputDir, horizontalInput, jumpPressed, physicsBody);
        }
        
        // Update facing direction
        if (horizontalInput > 0.01f) facingRight = true;
        else if (horizontalInput < -0.01f) facingRight = false;
    }
    
    bool CheckGrounded(float vy) {
        // Simple ground check based on vertical velocity
        // In a full implementation, you'd use raycasts or collision detection
        return std::abs(vy) < 1.0f;
    }
    
    void UpdateAnimation(float deltaTime) {
        if (!spriteRenderer) return;
        
        animationTime += deltaTime;
        
        // Determine animation frame
        int frame = idleFrame;
        
        if (physicsBody) {
            float vx, vy;
            ame_physics_get_velocity(physicsBody, &vx, &vy);
            
            if (std::abs(vy) > 1.0f) {
                // Jumping/falling
                frame = jumpFrame;
            } else if (std::abs(vx) > 1.0f) {
                // Walking
                int animFrame = static_cast<int>(animationTime * animationSpeed) & 1;
                frame = animFrame ? walkFrame2 : walkFrame1;
            }
        }
        
        // Apply frame to sprite UV
        ApplySpriteFrame(frame);
        
        // Flip sprite based on facing direction
        if (spriteRenderer) {
            auto uv = spriteRenderer->uv();
            if (!facingRight) {
                // Flip horizontally
                spriteRenderer->uv(uv.z, uv.y, uv.x, uv.w);
            }
        }
    }
    
    void ApplySpriteFrame(int frame) {
        // Character atlas is 9 columns x 3 rows, each character is 24x24 pixels
        const int framesPerRow = 9;
        const int totalRows = 3;
        const float frameWidth = 1.0f / framesPerRow;
        const float frameHeight = 1.0f / totalRows;
        
        // Map our animation frames to atlas positions
        // Frame 0 (idle): First character (column 0, row 0)
        // Frame 1-2 (walk): Use different characters for walk animation
        // Frame 3 (jump): Another character for jump
        int atlasFrame = 0;
        switch (frame) {
            case 0: atlasFrame = 0; break;  // Idle - first character
            case 1: atlasFrame = 1; break;  // Walk frame 1 - second character  
            case 2: atlasFrame = 2; break;  // Walk frame 2 - third character
            case 3: atlasFrame = 3; break;  // Jump - fourth character
            default: atlasFrame = 0; break;
        }
        
        int col = atlasFrame % framesPerRow;
        int row = atlasFrame / framesPerRow;
        
        float u0 = col * frameWidth;
        float v0 = row * frameHeight;
        float u1 = u0 + frameWidth;
        float v1 = v0 + frameHeight;
        
        if (spriteRenderer) {
            spriteRenderer->uv(u0, v0, u1, v1);
        }
    }
};
