#include "ame/physics.h"
#include "ame/ecs.h"
#include <box2d/box2d.h>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdio>

// Raycast callback for single hit
class RaycastCallback : public b2RayCastCallback {
public:
    RaycastCallback() : hit(false), fraction(1.0f), body(nullptr) {}
    
    float ReportFixture(b2Fixture* fixture, const b2Vec2& point, 
                       const b2Vec2& normal, float frac) override {
        // Keep the closest (smallest fraction)
        if (frac < fraction) {
            hit = true;
            hit_point = point;
            hit_normal = normal;
            fraction = frac;
            body = fixture->GetBody();
        }
        return frac; // continue, Box2D will pass others; we keep closest
    }
    
    bool hit;
    b2Vec2 hit_point;
    b2Vec2 hit_normal;
    float fraction;
    b2Body* body;
};

// Raycast callback for multiple hits
class RaycastAllCallback : public b2RayCastCallback {
public:
    RaycastAllCallback(AmeRaycastHit* buffer, size_t max) 
        : hits(buffer), max_hits(max), count(0) {}
    
    float ReportFixture(b2Fixture* fixture, const b2Vec2& point, 
                       const b2Vec2& normal, float fraction) override {
        if (count < max_hits) {
            hits[count].hit = true;
            hits[count].point_x = point.x;
            hits[count].point_y = point.y;
            hits[count].normal_x = normal.x;
            hits[count].normal_y = normal.y;
            hits[count].fraction = fraction;
            hits[count].body = fixture->GetBody();
            hits[count].user_data = reinterpret_cast<void*>(fixture->GetBody()->GetUserData().pointer);
            count++;
        }
        return 1.0f; // Continue to find all hits
    }
    
    AmeRaycastHit* hits;
    size_t max_hits;
    size_t count;
};

extern "C" {

AmePhysicsWorld* ame_physics_world_create(float gravity_x, float gravity_y, float timestep) {
    AmePhysicsWorld* world = (AmePhysicsWorld*)calloc(1, sizeof(AmePhysicsWorld));
    if (!world) return NULL;
    
    b2Vec2 gravity(gravity_x, gravity_y);
    world->world = new b2World(gravity);
    world->timestep = timestep;  // 1000 Hz timestep to match game tick rate
    world->velocity_iters = 6;
    world->position_iters = 2;
    
    return world;
}

void ame_physics_world_destroy(AmePhysicsWorld* world) {
    if (!world) return;
    if (world->world) {
        delete (b2World*)world->world;
    }
    free(world);
}

void ame_physics_world_step(AmePhysicsWorld* world) {
    if (!world || !world->world) return;
    ((b2World*)world->world)->Step(world->timestep, world->velocity_iters, world->position_iters);
}

b2Body* ame_physics_create_body(AmePhysicsWorld* world, float x, float y, 
                                float width, float height, AmeBodyType type,
                                bool is_sensor, void* user_data) {
    if (!world || !world->world) return NULL;
    
    b2BodyDef bodyDef;
    bodyDef.position.Set(x, y);
    
    switch (type) {
        case AME_BODY_STATIC:
            bodyDef.type = b2_staticBody;
            break;
        case AME_BODY_KINEMATIC:
            bodyDef.type = b2_kinematicBody;
            break;
        case AME_BODY_DYNAMIC:
            bodyDef.type = b2_dynamicBody;
            break;
    }
    
    bodyDef.userData.pointer = (uintptr_t)user_data;
    
    b2Body* body = ((b2World*)world->world)->CreateBody(&bodyDef);
    
    // Create box shape
    b2PolygonShape box;
    box.SetAsBox(width * 0.5f, height * 0.5f);
    
    b2FixtureDef fixtureDef;
    fixtureDef.shape = &box;
    fixtureDef.density = (type == AME_BODY_DYNAMIC) ? 1.0f : 0.0f;
    fixtureDef.friction = 0.3f;
    fixtureDef.isSensor = is_sensor;
    
    body->CreateFixture(&fixtureDef);
    
    return body;
}

void ame_physics_create_tilemap_collision(AmePhysicsWorld* world,
                                         const int* tiles, int width, int height,
                                         float tile_size) {
    if (!world || !world->world || !tiles) return;
    
    int collision_count = 0;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int tile = tiles[y * width + x];
            if (tile != 0) {
                // Create a static body for this tile
                // Use normal coordinate system first to test alignment
                float px = (x + 0.5f) * tile_size;
                float py = (y + 0.5f) * tile_size;
                ame_physics_create_body(world, px, py, tile_size, tile_size, 
                                       AME_BODY_STATIC, false, NULL);
                collision_count++;
                
                // Debug first few collisions
                if (collision_count <= 5) {
                    printf("Collision tile %d: grid(%d,%d) gid=%d -> world_pos(%.1f,%.1f) size=%.1f\n", 
                           collision_count, x, y, tile, px, py, tile_size);
                }
            }
        }
    }
    printf("Created %d collision bodies total\n", collision_count);
}

void ame_physics_destroy_body(AmePhysicsWorld* world, b2Body* body) {
    if (!world || !world->world || !body) return;
    ((b2World*)world->world)->DestroyBody(body);
}

void ame_physics_get_position(b2Body* body, float* x, float* y) {
    if (!body) return;
    b2Vec2 pos = body->GetPosition();
    if (x) *x = pos.x;
    if (y) *y = pos.y;
}

void ame_physics_set_position(b2Body* body, float x, float y) {
    if (!body) return;
    body->SetTransform(b2Vec2(x, y), body->GetAngle());
}

void ame_physics_get_velocity(b2Body* body, float* vx, float* vy) {
    if (!body) return;
    b2Vec2 vel = body->GetLinearVelocity();
    if (vx) *vx = vel.x;
    if (vy) *vy = vel.y;
}

void ame_physics_set_velocity(b2Body* body, float vx, float vy) {
    if (!body) return;
    body->SetLinearVelocity(b2Vec2(vx, vy));
}

void ame_physics_set_angle(b2Body* body, float angle) {
    if (!body) return;
    b2Vec2 pos = body->GetPosition();
    body->SetTransform(pos, angle);
}

AmeRaycastHit ame_physics_raycast(AmePhysicsWorld* world, 
                                  float start_x, float start_y,
                                  float end_x, float end_y) {
    AmeRaycastHit result = {0};
    if (!world || !world->world) return result;
    
    b2Vec2 p1(start_x, start_y);
    b2Vec2 p2(end_x, end_y);
    
    RaycastCallback callback;
    ((b2World*)world->world)->RayCast(&callback, p1, p2);
    
    if (callback.hit) {
        result.hit = true;
        result.point_x = callback.hit_point.x;
        result.point_y = callback.hit_point.y;
        result.normal_x = callback.hit_normal.x;
        result.normal_y = callback.hit_normal.y;
        result.fraction = callback.fraction;
        result.body = callback.body;
        result.user_data = (void*)((callback.body) ? callback.body->GetUserData().pointer : 0);
    }
    
    return result;
}

AmeRaycastMultiHit ame_physics_raycast_all(AmePhysicsWorld* world, 
                                           float start_x, float start_y,
                                           float end_x, float end_y,
                                           size_t max_hits) {
    AmeRaycastMultiHit result = {0};
    if (!world || !world->world || max_hits == 0) return result;
    
    result.hits = (AmeRaycastHit*)calloc(max_hits, sizeof(AmeRaycastHit));
    result.capacity = max_hits;
    
    b2Vec2 p1(start_x, start_y);
    b2Vec2 p2(end_x, end_y);
    
    RaycastAllCallback callback(result.hits, max_hits);
    ((b2World*)world->world)->RayCast(&callback, p1, p2);
    
    result.count = callback.count;
    
    return result;
}

void ame_physics_raycast_free(AmeRaycastMultiHit* multi_hit) {
    if (multi_hit && multi_hit->hits) {
        free(multi_hit->hits);
        multi_hit->hits = NULL;
        multi_hit->count = 0;
        multi_hit->capacity = 0;
    }
}

AmeEcsId ame_physics_register_body_component(AmeEcsWorld* w) {
    return ame_ecs_component_register(w, "AmePhysicsBody", sizeof(AmePhysicsBody), alignof(AmePhysicsBody));
}

AmeEcsId ame_physics_register_transform_component(AmeEcsWorld* w) {
    return ame_ecs_component_register(w, "AmeTransform2D", sizeof(AmeTransform2D), alignof(AmeTransform2D));
}

void ame_physics_sync_transforms(AmePhysicsWorld* physics, 
                                 AmePhysicsBody* bodies, 
                                 AmeTransform2D* transforms, 
                                 size_t count) {
    (void)physics;
    if (!bodies || !transforms) return;
    
    for (size_t i = 0; i < count; i++) {
        if (bodies[i].body) {
            b2Vec2 pos = bodies[i].body->GetPosition();
            float angle = bodies[i].body->GetAngle();
            transforms[i].x = pos.x;
            transforms[i].y = pos.y;
            transforms[i].angle = angle;
        }
    }
}

} // extern "C"
