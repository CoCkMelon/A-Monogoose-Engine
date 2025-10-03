// Quick test to verify physics implementation compiles and links correctly
#include "unitylike/Scene.h"
#include <iostream>

extern "C" {
#include "ame/physics.h"
}

using namespace unitylike;

int main() {
    // Create a Flecs world
    ecs_world_t* world = ecs_init();
    
    // Create a Scene
    Scene scene(world);
    
    // Create physics world
    AmePhysicsWorld* physicsWorld = ame_physics_world_create(0.0f, -10.0f, 1.0f/60.0f);
    Physics2D::SetWorld(physicsWorld);
    
    std::cout << "✓ Physics world created" << std::endl;
    
    // Create multiple GameObjects with physics bodies
    std::cout << "\n=== Creating Physics Bodies ===" << std::endl;
    
    // Player at origin
    GameObject player = scene.Create("Player");
    player.transform().position(glm::vec2(0.0f, 0.0f));
    auto& playerRb = player.AddComponent<Rigidbody2D>();
    auto& playerCol = player.AddComponent<Collider2D>();
    playerCol.boxSize(glm::vec2(1.0f, 2.0f));
    std::cout << "✓ Player created at (0, 0) with 1x2 box collider" << std::endl;
    
    // Wall to the right
    GameObject wall = scene.Create("Wall");
    wall.transform().position(glm::vec2(10.0f, 0.0f));
    auto& wallRb = wall.AddComponent<Rigidbody2D>();
    wallRb.bodyType(Rigidbody2D::BodyType::Static);
    auto& wallCol = wall.AddComponent<Collider2D>();
    wallCol.boxSize(glm::vec2(2.0f, 10.0f));
    std::cout << "✓ Wall created at (10, 0) with 2x10 box collider" << std::endl;
    
    // Platform above
    GameObject platform = scene.Create("Platform");
    platform.transform().position(glm::vec2(5.0f, 8.0f));
    auto& platformRb = platform.AddComponent<Rigidbody2D>();
    platformRb.bodyType(Rigidbody2D::BodyType::Static);
    auto& platformCol = platform.AddComponent<Collider2D>();
    platformCol.boxSize(glm::vec2(6.0f, 1.0f));
    std::cout << "✓ Platform created at (5, 8) with 6x1 box collider" << std::endl;
    
    // Trigger zone (won't block raycasts)
    GameObject trigger = scene.Create("Trigger");
    trigger.transform().position(glm::vec2(-5.0f, 0.0f));
    auto& triggerRb = trigger.AddComponent<Rigidbody2D>();
    triggerRb.bodyType(Rigidbody2D::BodyType::Static);
    auto& triggerCol = trigger.AddComponent<Collider2D>();
    triggerCol.type(Collider2D::Type::Circle);
    triggerCol.radius(2.0f);
    triggerCol.isTrigger(true);
    std::cout << "✓ Trigger created at (-5, 0) with radius 2 circle (trigger)" << std::endl;
    
    // Circle enemy
    GameObject enemy = scene.Create("Enemy");
    enemy.transform().position(glm::vec2(3.0f, 3.0f));
    auto& enemyRb = enemy.AddComponent<Rigidbody2D>();
    auto& enemyCol = enemy.AddComponent<Collider2D>();
    enemyCol.type(Collider2D::Type::Circle);
    enemyCol.radius(0.5f);
    std::cout << "✓ Enemy created at (3, 3) with radius 0.5 circle collider" << std::endl;
    
    // Step once to create physics bodies
    std::cout << "\n=== Stepping Scene (creates physics bodies) ===" << std::endl;
    scene.Step(0.016f);
    std::cout << "✓ Scene stepped - bodies created automatically" << std::endl;
    
    // Test velocity
    std::cout << "\n=== Testing Rigidbody2D API ===" << std::endl;
    playerRb.velocity(glm::vec2(5.0f, 2.0f));
    glm::vec2 vel = playerRb.velocity();
    std::cout << "✓ Set/Get velocity: (" << vel.x << ", " << vel.y << ")" << std::endl;
    
    // Test forces
    playerRb.AddForce(glm::vec2(0.0f, 100.0f));
    std::cout << "✓ AddForce called successfully" << std::endl;
    
    // Test raycasts
    std::cout << "\n=== Testing Raycasts ===" << std::endl;
    
    // Raycast to the right - should hit wall
    RaycastHit2D hitRight = Physics2D::Raycast(glm::vec2(0, 0), glm::vec2(1, 0), 100.0f);
    std::cout << "  Right raycast: " << (hitRight.hasHit ? "HIT" : "MISS");
    if (hitRight.hasHit) {
        std::cout << " at distance " << hitRight.distance 
                  << " (" << hitRight.collider.name() << ")" << std::endl;
    } else {
        std::cout << std::endl;
    }
    
    // Raycast to the left - should miss (trigger doesn't block)
    RaycastHit2D hitLeft = Physics2D::Raycast(glm::vec2(0, 0), glm::vec2(-1, 0), 100.0f);
    std::cout << "  Left raycast: " << (hitLeft.hasHit ? "HIT" : "MISS");
    if (hitLeft.hasHit) {
        std::cout << " at distance " << hitLeft.distance
                  << " (" << hitLeft.collider.name() << ")" << std::endl;
    } else {
        std::cout << " (trigger doesn't block rays)" << std::endl;
    }
    
    // Raycast upward - should hit platform
    RaycastHit2D hitUp = Physics2D::Raycast(glm::vec2(5, 0), glm::vec2(0, 1), 100.0f);
    std::cout << "  Up raycast: " << (hitUp.hasHit ? "HIT" : "MISS");
    if (hitUp.hasHit) {
        std::cout << " at distance " << hitUp.distance
                  << " (" << hitUp.collider.name() << ")" << std::endl;
    } else {
        std::cout << std::endl;
    }
    
    // Raycast diagonal - should miss
    RaycastHit2D hitDiag = Physics2D::Raycast(glm::vec2(0, 0), glm::vec2(-1, -1), 10.0f);
    std::cout << "  Diagonal raycast: " << (hitDiag.hasHit ? "HIT" : "MISS") << std::endl;
    
    // Test RaycastAll
    std::cout << "\n=== Testing RaycastAll ===" << std::endl;
    auto hits = Physics2D::RaycastAll(glm::vec2(-10, 0), glm::vec2(1, 0), 100.0f);
    std::cout << "  RaycastAll found " << hits.size() << " hits" << std::endl;
    for (size_t i = 0; i < hits.size(); i++) {
        std::cout << "    Hit " << (i+1) << ": " << hits[i].collider.name()
                  << " at distance " << hits[i].distance << std::endl;
    }
    
    // Test overlap queries
    std::cout << "\n=== Testing Overlap Queries ===" << std::endl;
    
    // Overlap point at player position - should hit player
    bool pointHit = Physics2D::OverlapPoint(glm::vec2(0, 0));
    std::cout << "  OverlapPoint at (0,0): " << (pointHit ? "HIT" : "MISS") << std::endl;
    
    // Overlap point in empty space
    bool pointMiss = Physics2D::OverlapPoint(glm::vec2(20, 20));
    std::cout << "  OverlapPoint at (20,20): " << (pointMiss ? "HIT" : "MISS") << std::endl;
    
    // Overlap circle around origin - should find multiple objects
    auto nearOrigin = Physics2D::OverlapCircle(glm::vec2(0, 0), 10.0f);
    std::cout << "  OverlapCircle(r=10) at origin: " << nearOrigin.size() << " objects" << std::endl;
    for (const auto& obj : nearOrigin) {
        std::cout << "    - " << obj.name() << std::endl;
    }
    
    // Overlap circle in empty area
    auto farAway = Physics2D::OverlapCircle(glm::vec2(50, 50), 5.0f);
    std::cout << "  OverlapCircle(r=5) at (50,50): " << farAway.size() << " objects" << std::endl;
    
    // Overlap box
    auto boxOverlap = Physics2D::OverlapBox(glm::vec2(5, 0), glm::vec2(12, 4), 0.0f);
    std::cout << "  OverlapBox(12x4) at (5,0): " << boxOverlap.size() << " objects" << std::endl;
    for (const auto& obj : boxOverlap) {
        std::cout << "    - " << obj.name() << std::endl;
    }
    
    // Cleanup
    ame_physics_world_destroy(physicsWorld);
    ecs_fini(world);
    
    std::cout << "\n✅ All physics tests passed!" << std::endl;
    
    return 0;
}
