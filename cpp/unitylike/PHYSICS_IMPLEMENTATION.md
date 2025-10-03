# Physics Implementation in cpp/unitylike

## Overview

This document describes the 2D physics implementation for the Unity-like C++ façade. The implementation uses Box2D as the underlying physics engine and provides a Unity-style API through the `unitylike` namespace.

## Components

### Core Files

1. **Rigidbody2D.cpp** - Rigidbody component implementation
   - Velocity and angular velocity control
   - Force and impulse application
   - Body type management (Dynamic, Kinematic, Static)
   - Mass, gravity scale, drag properties
   - Constraint system (freeze position/rotation)

2. **Collider2D.cpp** - Collider component implementation
   - Box and Circle collider shapes
   - Trigger support (isTrigger flag)
   - Dynamic shape configuration

3. **Physics2D.cpp** - Static utility class for physics queries (NEW)
   - `Raycast()` - Cast a ray and return first hit
   - `RaycastAll()` - Cast a ray and return all hits
   - `OverlapPoint()` - Check if a point overlaps any collider
   - `OverlapCircle()` - Find all colliders in a circular area
   - `OverlapBox()` - Find all colliders in a rectangular area

4. **PhysicsSync.cpp** - Transform/physics synchronization (NEW)
   - `SyncPhysicsToTransform` - Updates Transform from physics bodies (after physics step)
   - `SyncTransformToPhysics` - Updates physics bodies from Transform (for kinematic/static bodies)
   - Automatic registration with ECS pipeline

5. **PhysicsCallbacks.cpp** - Collision detection and callbacks (NEW)
   - `PhysicsContactListener` - Box2D contact listener implementation
   - Collision event tracking (Enter, Stay, Exit)
   - Trigger event tracking (Enter, Stay, Exit)
   - Contact point generation with normals and velocities

## Data Structures

### ContactPoint2D
```cpp
struct ContactPoint2D {
    glm::vec2 point;           // Contact point in world space
    glm::vec2 normal;          // Surface normal at contact point
    glm::vec2 relativeVelocity; // Relative velocity of colliding objects
    float separation;          // Penetration depth (negative = overlap)
};
```

### Collision2D
```cpp
struct Collision2D {
    GameObject gameObject;              // The other GameObject in the collision
    Rigidbody2D* rigidbody;            // Rigidbody of the other object (if any)
    Collider2D* collider;              // Collider of the other object
    glm::vec2 relativeVelocity;        // Relative velocity between objects
    std::vector<ContactPoint2D> contacts; // Contact points
};
```

### RaycastHit2D
```cpp
struct RaycastHit2D {
    bool hasHit;                  // Whether the ray hit anything
    glm::vec2 point;             // Hit point in world space
    glm::vec2 normal;            // Surface normal at hit point
    float distance;              // Distance from ray origin
    GameObject collider;         // GameObject that was hit
    Rigidbody2D* rigidbody;     // Rigidbody of hit object (if any)
};
```

## API Usage

### Rigidbody2D

```cpp
// Add a rigidbody to a GameObject
auto& rb = gameObject.AddComponent<Rigidbody2D>();

// Set velocity
rb.velocity(glm::vec2(5.0f, 0.0f));

// Apply forces
rb.AddForce(glm::vec2(0.0f, 10.0f));
rb.AddImpulse(glm::vec2(0.0f, 5.0f));

// Configure properties
rb.mass(2.0f);
rb.gravityScale(1.0f);
rb.drag(0.1f);
rb.bodyType(Rigidbody2D::BodyType::Dynamic);

// Freeze rotation
rb.constraints(Rigidbody2D::Constraints::FreezeRotation);
```

### Collider2D

```cpp
// Add a box collider
auto& collider = gameObject.AddComponent<Collider2D>();
collider.type(Collider2D::Type::Box);
collider.boxSize(glm::vec2(1.0f, 1.0f));

// Add a circle collider
collider.type(Collider2D::Type::Circle);
collider.radius(0.5f);

// Make it a trigger
collider.isTrigger(true);
```

### Physics2D Queries

```cpp
// Raycast
auto hit = Physics2D::Raycast(origin, direction, maxDistance);
if (hit.hasHit) {
    SDL_Log("Hit %s at distance %.2f", 
            hit.collider.name().c_str(), hit.distance);
}

// Raycast all
auto hits = Physics2D::RaycastAll(origin, direction, 100.0f, 16);
for (const auto& hit : hits) {
    // Process each hit
}

// Overlap tests
if (Physics2D::OverlapPoint(point)) {
    SDL_Log("Point is inside a collider");
}

auto objects = Physics2D::OverlapCircle(center, radius);
for (const auto& obj : objects) {
    SDL_Log("Found object: %s", obj.name().c_str());
}
```

### Collision Callbacks

In your `MongooseBehaviour` subclass:

```cpp
class PlayerController : public MongooseBehaviour {
public:
    void OnCollisionEnter2D(const Collision2D& collision) override {
        SDL_Log("Collided with %s", collision.gameObject.name().c_str());
        
        // Access contact points
        for (const auto& contact : collision.contacts) {
            SDL_Log("Contact at (%.2f, %.2f)", contact.point.x, contact.point.y);
            SDL_Log("Normal: (%.2f, %.2f)", contact.normal.x, contact.normal.y);
        }
    }
    
    void OnTriggerEnter2D(Collider2D* other) override {
        SDL_Log("Entered trigger: %s", other->owner_.name().c_str());
    }
};
```

## Integration with PhysicsManager

For proper physics integration, you should create a `PhysicsManager` script that manages the global physics world:

```cpp
class PhysicsManager : public MongooseBehaviour {
    AmePhysicsWorld* physicsWorld = nullptr;
    
public:
    void Awake() override {
        // Create physics world
        physicsWorld = ame_physics_world_create(0.0f, -1000.0f, 1.0f/60.0f);
        
        // Set it as the global physics world
        Physics2D::SetWorld(physicsWorld);
        
        // Initialize collision callbacks
        InitPhysicsCallbacks(physicsWorld, gameObject().scene());
    }
    
    void FixedUpdate(float fixedDeltaTime) override {
        // Step the physics simulation
        if (physicsWorld) {
            ame_physics_world_step(physicsWorld);
        }
    }
    
    void OnDestroy() override {
        if (physicsWorld) {
            ame_physics_world_destroy(physicsWorld);
            Physics2D::SetWorld(nullptr);
        }
    }
};
```

## System Execution Order

The physics systems run in the following order:

1. **PreUpdate Phase**
   - `SyncTransformToPhysics` - Updates kinematic/static physics bodies from Transform

2. **Update Phase**
   - User scripts (Update callbacks)
   - Collider synchronization (creates/updates fixtures)

3. **FixedUpdate Phase**  
   - User scripts (FixedUpdate callbacks)
   - `ame_physics_world_step()` - Physics simulation step (called by PhysicsManager)
   - Collision callbacks (via Box2D ContactListener)

4. **PostUpdate Phase**
   - `SyncPhysicsToTransform` - Updates Transform from dynamic physics bodies

## Notes

- Dynamic bodies are controlled by physics - their transforms are updated FROM the physics simulation
- Kinematic/Static bodies can be moved via Transform - changes are synced TO the physics simulation
- The Physics2D class maintains static pointers to the active world and scene
- Collision callbacks require script component registry integration (partially implemented)
- Body user data stores the ECS entity ID for GameObject lookup during raycasts and queries

## Future Enhancements

- Complete collision callback dispatch to all script components
- Add layer-based collision filtering
- Implement joint system (hinges, springs, etc.)
- Add continuous collision detection options
- Implement Physics2D.IgnoreCollision()
- Add debug rendering for colliders
