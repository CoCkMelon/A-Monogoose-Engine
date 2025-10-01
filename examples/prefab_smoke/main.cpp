#include <cstdio>
#include <string>

#include <flecs.h>

#include "unitylike/SceneAsset.h"
#include "unitylike/Scene.h"
#include "unitylike/TransformHierarchy.h"

using namespace unitylike;

int main() {
    // Register a simple prefab in C++ that creates a Root entity with a child
    bool reg_ok = RegisterPrefab("Smoke/TwoNode", [](SceneAsset& asset, SceneAsset::Entity parent, const std::string& instanceName) {
        auto root = asset.create(instanceName.empty() ? "PrefabRoot" : instanceName)
            .transform(1.0f, 2.0f, 0.0f);
        if (parent.valid()) root.setParent(parent);
        auto child = asset.create("Child").transform(3.0f, 0.0f, 0.0f).setParent(root);
        (void)child;
        return root;
    });
    if (!reg_ok) {
        std::printf("RegisterPrefab failed\n");
        return 10;
    }

    // Build a SceneAsset and instantiate the prefab
    SceneAsset scene("PrefabSmoke", "1.0.0");
    auto holder = scene.create("Holder").transform(0,0,0);

    // Instantiate under Holder with an instance name
    auto root = scene.instantiatePrefab("Smoke/TwoNode", "InstanceA", holder);
    if (!root.valid()) {
        std::printf("Prefab instantiate returned invalid entity\n");
        return 11;
    }

    // Create world and finalize
    ecs_world_t* world = ecs_init();
    // Construct façade to register component IDs/systems
    Scene facade(world);

    bool ok = scene.instantiateToWorld(world);
    if (!ok) {
        std::printf("instantiateToWorld failed\n");
        return 12;
    }

    // Verify we can look up Holder/InstanceA/Child
    ecs_entity_t eHolder = ecs_lookup(world, "Holder");
    ecs_entity_t eInstance = ecs_lookup_path_w_sep(world, 0, "Holder/InstanceA", "/", NULL, true);
    ecs_entity_t eChild = ecs_lookup_path_w_sep(world, 0, "Holder/InstanceA/Child", "/", NULL, true);

    if (!eHolder || !eInstance || !eChild) {
        std::printf("Lookup failed: Holder=%llu InstanceA=%llu Child=%llu\n",
            (unsigned long long)eHolder, (unsigned long long)eInstance, (unsigned long long)eChild);
        return 13;
    }

    // Compute world transform of Child and check expected position
    auto wtChild = unitylike::ameComputeWorldTransform(world, eChild);
    // Holder(0,0) + InstanceA(1,2) + Child(3,0) => (4,2)
    if (std::fabs(wtChild.x - 4.0f) > 1e-4f || std::fabs(wtChild.y - 2.0f) > 1e-4f) {
        std::printf("Unexpected child world pos: (%.3f, %.3f)\n", wtChild.x, wtChild.y);
        return 14;
    }

    std::printf("Prefab smoke PASSED\n");
    ecs_fini(world);
    return 0;
}