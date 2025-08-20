#ifndef AME_PHYSICS_H
#define AME_PHYSICS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Forward declarations for Box2D C++ types (opaque to C)
typedef struct b2World b2World;
typedef struct b2Body b2Body;

// Forward decl from ECS wrapper
typedef struct AmeEcsWorld AmeEcsWorld;
typedef uint64_t AmeEcsId;

// Physics world wrapper
typedef struct AmePhysicsWorld {
    b2World* world;
    float timestep;        // Fixed timestep for simulation (e.g., 1/60.0f)
    int velocity_iters;    // Velocity iterations for solver
    int position_iters;    // Position iterations for solver
} AmePhysicsWorld;

// Body types
typedef enum AmeBodyType {
    AME_BODY_STATIC = 0,
    AME_BODY_KINEMATIC = 1,
    AME_BODY_DYNAMIC = 2
} AmeBodyType;

// Physics body component for ECS entities
typedef struct AmePhysicsBody {
    b2Body* body;
    float width;           // For box shapes
    float height;          // For box shapes
    bool is_sensor;        // Whether this body is a sensor (no collision response)
} AmePhysicsBody;

// Transform component for synchronization with physics
typedef struct AmeTransform2D {
    float x, y;           // Position
    float angle;          // Rotation in radians
} AmeTransform2D;

// Ray cast result
typedef struct AmeRaycastHit {
    bool hit;             // Whether the ray hit something
    float point_x, point_y;  // Hit point in world space
    float normal_x, normal_y; // Surface normal at hit point
    float fraction;       // Distance along ray (0 to 1)
    b2Body* body;         // Body that was hit
    void* user_data;      // Optional user data from the body
} AmeRaycastHit;

// Initialize physics world with gravity
AmePhysicsWorld* ame_physics_world_create(float gravity_x, float gravity_y);

// Destroy physics world and all bodies
void ame_physics_world_destroy(AmePhysicsWorld* world);

// Step the physics simulation
void ame_physics_world_step(AmePhysicsWorld* world);

// Create a physics body
b2Body* ame_physics_create_body(AmePhysicsWorld* world, float x, float y, 
                                float width, float height, AmeBodyType type,
                                bool is_sensor, void* user_data);

// Create a tile-based static collision world from tilemap data
// Each non-zero tile becomes a static box collider
void ame_physics_create_tilemap_collision(AmePhysicsWorld* world,
                                         const int* tiles, int width, int height,
                                         float tile_size);

// Destroy a physics body
void ame_physics_destroy_body(AmePhysicsWorld* world, b2Body* body);

// Get/set body position
void ame_physics_get_position(b2Body* body, float* x, float* y);
void ame_physics_set_position(b2Body* body, float x, float y);

// Set body rotation angle (radians)
void ame_physics_set_angle(b2Body* body, float angle);

// Get/set body velocity
void ame_physics_get_velocity(b2Body* body, float* vx, float* vy);
void ame_physics_set_velocity(b2Body* body, float vx, float vy);

// Perform a raycast
AmeRaycastHit ame_physics_raycast(AmePhysicsWorld* world, 
                                  float start_x, float start_y,
                                  float end_x, float end_y);

// Perform multiple raycasts and collect all hits
typedef struct AmeRaycastMultiHit {
    AmeRaycastHit* hits;
    size_t count;
    size_t capacity;
} AmeRaycastMultiHit;

AmeRaycastMultiHit ame_physics_raycast_all(AmePhysicsWorld* world, 
                                           float start_x, float start_y,
                                           float end_x, float end_y,
                                           size_t max_hits);
void ame_physics_raycast_free(AmeRaycastMultiHit* multi_hit);

// Register physics components with ECS
AmeEcsId ame_physics_register_body_component(AmeEcsWorld* w);
AmeEcsId ame_physics_register_transform_component(AmeEcsWorld* w);

// Sync ECS transforms with physics bodies
void ame_physics_sync_transforms(AmePhysicsWorld* physics, 
                                 AmePhysicsBody* bodies, 
                                 AmeTransform2D* transforms, 
                                 size_t count);

#ifdef __cplusplus
}
#endif

#endif // AME_PHYSICS_H
