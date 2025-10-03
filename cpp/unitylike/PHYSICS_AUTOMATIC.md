# Automatic Physics Body Management

## Overview

The Unity-like physics system automatically creates and manages Box2D physics bodies for you. You no longer need to manually call `ame_physics_create_body()` or manage body lifecycle.

## How It Works

### Automatic Creation

When you add a `Rigidbody2D` component to a GameObject:

```cpp
GameObject player = scene.Create("Player");
auto& rb = player.AddComponent<Rigidbody2D>();
```

The ECS system automatically:
1. Creates a Box2D `b2Body` in the PreUpdate phase
2. Uses the GameObject's Transform position as the initial position
3. Uses the Collider2D size if available, otherwise defaults to 1x1
4. Stores the entity ID in the body's user data (for raycasts)
5. Links the body to the AmePhysicsBody component

### Automatic Synchronization

The physics sync systems handle bidirectional transform/physics synchronization:

**PreUpdate Phase:**
- `SyncTransformToPhysics` - Updates kinematic/static bodies from Transform changes

**PostUpdate Phase:**
- `SyncPhysicsToTransform` - Updates Transform from dynamic body positions

This means you can:
- Set `transform.position()` on kinematic bodies and it will move the physics body
- Physics will automatically update `transform.position()` for dynamic bodies

## Usage Examples

### Basic Dynamic Body

```cpp
// Create a player with physics
GameObject player = scene.Create("Player");
player.transform().position(glm::vec2(100, 200));

// Add Rigidbody2D - body is created automatically!
auto& rb = player.AddComponent<Rigidbody2D>();

// Add collider for shape
auto& collider = player.AddComponent<Collider2D>();
collider.boxSize(glm::vec2(16, 32));

// Use physics immediately - no manual setup needed!
rb.velocity(glm::vec2(5, 0));
rb.AddForce(glm::vec2(0, 100));
```

### Kinematic Body (Player Controller)

```cpp
GameObject player = scene.Create("Player");
auto& rb = player.AddComponent<Rigidbody2D>();
rb.bodyType(Rigidbody2D::BodyType::Kinematic);

// Move via transform - syncs automatically to physics
player.transform().position(glm::vec2(x, y));
```

### Static Body (Platform)

```cpp
GameObject platform = scene.Create("Platform");
auto& rb = platform.AddComponent<Rigidbody2D>();
rb.bodyType(Rigidbody2D::BodyType::Static);

auto& collider = platform.AddComponent<Collider2D>();
collider.boxSize(glm::vec2(100, 20));
```

## What You DON'T Need Anymore

### ❌ Manual Body Creation
```cpp
// OLD WAY - DON'T DO THIS
b2Body* body = ame_physics_create_body(
    physicsWorld, x, y, w, h, 
    AME_BODY_DYNAMIC, false, nullptr
);
```

### ❌ Manual Transform Sync
```cpp
// OLD WAY - DON'T DO THIS
float px, py;
ame_physics_get_position(body, &px, &py);
transform.position(glm::vec2(px, py));
```

### ❌ Manual Velocity Access
```cpp
// OLD WAY - DON'T DO THIS
float vx, vy;
ame_physics_get_velocity(body, &vx, &vy);
ame_physics_set_velocity(body, newVx, newVy);
```

### ❌ Manual Body Cleanup
```cpp
// OLD WAY - DON'T DO THIS
void OnDestroy() {
    if (body && physicsWorld) {
        ame_physics_destroy_body(physicsWorld, body);
    }
}
```

## What You DO Need

### ✅ Set Physics World

Before creating any physics objects, set the global physics world:

```cpp
// In your PhysicsManager or game initialization
AmePhysicsWorld* physicsWorld = ame_physics_world_create(0.0f, -1000.0f, 1.0f/60.0f);
Physics2D::SetWorld(physicsWorld);
InitPhysicsCallbacks(physicsWorld, &scene);
```

### ✅ Step Physics

Call the physics step in your FixedUpdate:

```cpp
void FixedUpdate(float fixedDeltaTime) override {
    AmePhysicsWorld* world = Physics2D::GetWorld();
    if (world) {
        ame_physics_world_step(world);
    }
}
```

### ✅ Use Unity-like API

Just use the Rigidbody2D component methods:

```cpp
// Velocity
rb.velocity(glm::vec2(5, 0));
glm::vec2 vel = rb.velocity();

// Forces
rb.AddForce(glm::vec2(0, jumpForce));
rb.AddImpulse(glm::vec2(pushX, pushY));

// Properties  
rb.mass(2.0f);
rb.gravityScale(1.5f);
rb.drag(0.1f);
```

## System Execution Order

```
PreUpdate:
  └─ CreateMissingPhysicsBodies  // Auto-creates bodies
  └─ SyncTransformToPhysics      // Kinematic/static sync

Update:
  └─ Your scripts (Update)
  └─ Collider system              // Updates fixtures

FixedUpdate:
  └─ Your scripts (FixedUpdate)
  └─ ame_physics_world_step()     // Physics simulation
  └─ Collision callbacks          // OnCollisionEnter2D, etc.

PostUpdate:
  └─ SyncPhysicsToTransform       // Dynamic body sync
```

## Performance Notes

- Body creation happens automatically but only once per entity
- The creation system runs in PreUpdate but early-exits for entities that already have bodies
- No overhead in hot paths (velocity, forces, etc.) - those access the body directly
- Transform sync only happens for entities with both Transform and AmePhysicsBody components

## Migration Guide

### From Manual Box2D Code

**Before:**
```cpp
class PlayerBehaviour : public MongooseBehaviour {
    b2Body* physicsBody = nullptr;
    AmePhysicsWorld* physicsWorld = nullptr;
    
    void SetPhysicsWorld(AmePhysicsWorld* world) {
        physicsWorld = world;
    }
    
    void Start() override {
        auto pos = transform().position();
        physicsBody = ame_physics_create_body(
            physicsWorld, pos.x, pos.y, 16.0f, 16.0f,
            AME_BODY_DYNAMIC, false, nullptr
        );
    }
    
    void Update(float dt) override {
        float px, py;
        ame_physics_get_position(physicsBody, &px, &py);
        transform().position(glm::vec2(px, py));
    }
    
    void FixedUpdate(float dt) override {
        float vx, vy;
        ame_physics_get_velocity(physicsBody, &vx, &vy);
        vx = moveSpeed * input;
        ame_physics_set_velocity(physicsBody, vx, vy);
    }
    
    void OnDestroy() override {
        if (physicsBody && physicsWorld) {
            ame_physics_destroy_body(physicsWorld, physicsBody);
        }
    }
};
```

**After:**
```cpp
class PlayerBehaviour : public MongooseBehaviour {
    Rigidbody2D* rb = nullptr;
    
    void Awake() override {
        rb = gameObject().TryGetComponent<Rigidbody2D>();
        if (!rb) {
            rb = &gameObject().AddComponent<Rigidbody2D>();
        }
    }
    
    void FixedUpdate(float dt) override {
        glm::vec2 vel = rb->velocity();
        vel.x = moveSpeed * input;
        rb->velocity(vel);
    }
    
    // Transform sync is automatic - no Update() needed
    // Body cleanup is automatic - no OnDestroy() needed
};
```

Much simpler and more Unity-like!
