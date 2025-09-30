/**
 * @file main.cpp
 * @brief Example demonstrating multithreaded script execution using Flecs ECS
 * 
 * This example shows how Unity-like MongooseBehaviour scripts are executed
 * in parallel across multiple threads using Flecs' built-in multithreading.
 */

#include "../../cpp/unitylike/Scene.h"
#include <flecs.h>
#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <string>

namespace unitylike {

// Test script that demonstrates thread-safe execution
class TestScript : public MongooseBehaviour {
public:
    static std::atomic<int> awake_count;
    static std::atomic<int> start_count;
    static std::atomic<int> update_count;
    static std::atomic<int> destroy_count;
    
    int id;
    float accumulator = 0.0f;
    
    TestScript(int script_id) : id(script_id) {}
    
    void Awake() override {
        awake_count.fetch_add(1);
        std::cout << "[Thread " << std::this_thread::get_id() << "] TestScript " << id << " Awake()\n";
    }
    
    void Start() override {
        start_count.fetch_add(1);
        std::cout << "[Thread " << std::this_thread::get_id() << "] TestScript " << id << " Start()\n";
    }
    
    void Update(float deltaTime) override {
        update_count.fetch_add(1);
        accumulator += deltaTime;
        
        // Only log first few updates to avoid spam
        if (update_count.load() <= 20) {
            std::cout << "[Thread " << std::this_thread::get_id() << "] TestScript " << id 
                     << " Update(" << deltaTime << ") - Total time: " << accumulator << "s\n";
        }
    }
    
    void LateUpdate() override {
        // Demonstrate LateUpdate execution
        if (update_count.load() <= 10) {
            std::cout << "[Thread " << std::this_thread::get_id() << "] TestScript " << id << " LateUpdate()\n";
        }
    }
    
    void FixedUpdate(float fixedDeltaTime) override {
        // Demonstrate FixedUpdate execution
        static std::atomic<int> fixed_count{0};
        if (fixed_count.fetch_add(1) <= 10) {
            std::cout << "[Thread " << std::this_thread::get_id() << "] TestScript " << id 
                     << " FixedUpdate(" << fixedDeltaTime << ")\n";
        }
    }
    
    void OnDestroy() override {
        destroy_count.fetch_add(1);
        std::cout << "[Thread " << std::this_thread::get_id() << "] TestScript " << id << " OnDestroy()\n";
    }
};

// Another test script to show multiple script types
class SecondScript : public MongooseBehaviour {
public:
    static std::atomic<int> instance_count;
    int my_id;
    
    SecondScript() : my_id(instance_count.fetch_add(1)) {}
    
    void Awake() override {
        std::cout << "[Thread " << std::this_thread::get_id() << "] SecondScript " << my_id << " Awake()\n";
    }
    
    void Update(float deltaTime) override {
        if (my_id < 5) {  // Only log first few
            std::cout << "[Thread " << std::this_thread::get_id() << "] SecondScript " << my_id << " Update()\n";
        }
    }
    
    void OnDestroy() override {
        std::cout << "[Thread " << std::this_thread::get_id() << "] SecondScript " << my_id << " OnDestroy()\n";
    }
};

// Initialize static counters
std::atomic<int> TestScript::awake_count{0};
std::atomic<int> TestScript::start_count{0};
std::atomic<int> TestScript::update_count{0};
std::atomic<int> TestScript::destroy_count{0};
std::atomic<int> SecondScript::instance_count{0};

} // namespace unitylike

void run_multithreaded_test() {
    using namespace unitylike;
    
    std::cout << "=== Multithreaded Script Execution Test ===\n";
    std::cout << "This test demonstrates Flecs-powered multithreaded script execution\n\n";
    
    // Create a Flecs world with multithreading enabled
    ecs_world_t* world = ecs_init();
    
    // Configure threading
    const int num_threads = std::thread::hardware_concurrency();
    ecs_set_threads(world, num_threads);
    std::cout << "Enabled " << num_threads << " worker threads\n";
    
    // Create Unity-like scene
    Scene scene(world);
    
    // Create multiple game objects with different script combinations
    const int num_entities = 50;
    std::vector<GameObject> entities;
    
    std::cout << "Creating " << num_entities << " entities with scripts...\n";
    
    for (int i = 0; i < num_entities; i++) {
        GameObject go = scene.Create("TestEntity_" + std::to_string(i));
        
        // Add TestScript to all entities
        go.AddScript<TestScript>(i);
        
        // Add SecondScript to some entities
        if (i % 3 == 0) {
            go.AddScript<SecondScript>();
        }
        
        entities.push_back(go);
    }
    
    std::cout << "Created entities with scripts\n\n";
    
    // Run multiple update cycles
    const int num_frames = 3;
    for (int frame = 0; frame < num_frames; frame++) {
        float deltaTime = 0.016f;  // ~60 FPS
        
        std::cout << "--- Frame " << (frame + 1) << " ---\n";
        
        auto start_time = std::chrono::high_resolution_clock::now();
        scene.Step(deltaTime);
        auto end_time = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        std::cout << "Frame took: " << duration.count() << " microseconds\n";
        
        // Small delay to see interleaving more clearly
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Also test fixed update
    std::cout << "\n--- Testing Fixed Update ---\n";
    scene.StepFixed(0.02f);  // 50 Hz fixed timestep
    
    // Print final statistics
    std::cout << "\n=== Final Statistics ===\n";
    std::cout << "TestScript Awake called:     " << TestScript::awake_count.load() << " times (expected: " << num_entities << ")\n";
    std::cout << "TestScript Start called:     " << TestScript::start_count.load() << " times (expected: " << num_entities << ")\n";
    std::cout << "TestScript Update called:    " << TestScript::update_count.load() << " times (expected: " << num_entities * num_frames << ")\n";
    std::cout << "SecondScript instances:      " << SecondScript::instance_count.load() << " (expected: " << (num_entities / 3 + 1) << ")\n";
    
    // Test script retrieval
    std::cout << "\n--- Testing Script Retrieval ---\n";
    if (!entities.empty()) {
        GameObject& first_entity = entities[0];
        TestScript* script = first_entity.GetScript<TestScript>();
        if (script) {
            std::cout << "Successfully retrieved TestScript from entity (ID: " << script->id << ")\n";
        } else {
            std::cout << "Failed to retrieve TestScript from entity\n";
        }
        
        SecondScript* second = first_entity.GetScript<SecondScript>();
        if (second) {
            std::cout << "Successfully retrieved SecondScript from entity (ID: " << second->my_id << ")\n";
        } else {
            std::cout << "Entity doesn't have SecondScript (as expected for entity 0)\n";
        }
    }
    
    // Cleanup - this should trigger OnDestroy for all scripts
    std::cout << "\n--- Cleanup ---\n";
    ecs_fini(world);
    
    std::cout << "TestScript OnDestroy called: " << TestScript::destroy_count.load() << " times (expected: " << num_entities << ")\n";
    
    // Verify results
    bool success = (TestScript::awake_count.load() == num_entities) &&
                   (TestScript::start_count.load() == num_entities) &&
                   (TestScript::update_count.load() == num_entities * num_frames) &&
                   (TestScript::destroy_count.load() == num_entities);
    
    if (success) {
        std::cout << "\n✅ Multithreaded script test PASSED!\n";
        std::cout << "All scripts executed correctly across " << num_threads << " threads.\n";
    } else {
        std::cout << "\n❌ Multithreaded script test FAILED!\n";
        std::cout << "Some script lifecycle methods were not called the expected number of times.\n";
    }
}

int main() {
    run_multithreaded_test();
    return 0;
}