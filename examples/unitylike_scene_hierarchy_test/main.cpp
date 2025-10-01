#include <cstdio>
#include <cmath>
#include <cassert>
#include <string>

#include <flecs.h>

#include "unitylike/SceneAsset.h"
#include "unitylike/Scene.h"              // ensure_components_registered via Scene ctor
#include "unitylike/TransformHierarchy.h" // ameComputeWorldTransform

static bool nearly_equal(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

int main() {
    // Create a Flecs world
    ecs_world_t* world = ecs_init();

    // Build scene with a simple hierarchy: Root -> Parent -> Child
    // Transforms:
    //   Root:   (0,0, 0 deg)
    //   Parent: local (10,0, 90 deg)
    //   Child:  local (5,0,  0 deg)
    // Expected world for Parent: (10,0), angle=pi/2
    // Expected world for Child: Parent rotation of (5,0) => (0,5) + (10,0) = (10,5), angle=pi/2
    unitylike::SceneAsset scene("HierarchyTest", "1.0.0");

    auto root = scene.create("Root").transform(0, 0, 0);
    auto parent = scene.create("Parent").transform(10, 0, (float)M_PI * 0.5f).setParent(root);
    auto child = scene.create("Child").transform(5, 0, 0.0f).setParent(parent);

    // Ensure façade component ids are registered
    // (Creates g_comp.transform ids etc. to match component names set by the scene builder)
    {
        unitylike::Scene facade(world); // ctor registers systems and components
        (void)facade;
    }

    // Instantiate into the world (after systems/components registered)
    bool ok = scene.instantiateToWorld(world);
    if (!ok) {
        std::printf("Instantiate failed\n");
        return 1;
    }

    // Lookup entities by name
    ecs_entity_t eRoot = ecs_lookup(world, "Root");
    ecs_entity_t eParent = ecs_lookup_path_w_sep(world, 0, "Root/Parent", "/", NULL, true);
    ecs_entity_t eChild = ecs_lookup_path_w_sep(world, 0, "Root/Parent/Child", "/", NULL, true);

    if (!eRoot || !eParent || !eChild) {
        std::printf("Entity lookup failed: Root=%llu Parent=%llu Child=%llu\n",
                    (unsigned long long)eRoot, (unsigned long long)eParent, (unsigned long long)eChild);
        return 2;
    }

    // Compute composed/world transforms
    auto wtRoot   = unitylike::ameComputeWorldTransform(world, eRoot);
    auto wtParent = unitylike::ameComputeWorldTransform(world, eParent);
    auto wtChild  = unitylike::ameComputeWorldTransform(world, eChild);

    // Validate expectations
    bool pass = true;
    // Root
    pass &= nearly_equal(wtRoot.x, 0.0f) && nearly_equal(wtRoot.y, 0.0f) && nearly_equal(wtRoot.angle, 0.0f);
    // Parent
    pass &= nearly_equal(wtParent.x, 10.0f) && nearly_equal(wtParent.y, 0.0f) && nearly_equal(wtParent.angle, (float)M_PI * 0.5f);
    // Child
    pass &= nearly_equal(wtChild.x, 10.0f) && nearly_equal(wtChild.y, 5.0f) && nearly_equal(wtChild.angle, (float)M_PI * 0.5f);

    if (!pass) {
        std::printf("Hierarchy test FAILED\n");
        std::printf("Root  : (%.3f, %.3f, ang=%.3f)\n", wtRoot.x, wtRoot.y, wtRoot.angle);
        std::printf("Parent: (%.3f, %.3f, ang=%.3f)\n", wtParent.x, wtParent.y, wtParent.angle);
        std::printf("Child : (%.3f, %.3f, ang=%.3f)\n", wtChild.x, wtChild.y, wtChild.angle);
        ecs_fini(world);
        return 3;
    }

    std::printf("Hierarchy test PASSED\n");

    // Also test prefab path minimally (if any prefab registered, this would create entities under Root)
    // For now, skip: users can register a C prefab and call scene.instantiatePrefab("Key", "Inst", root);

    ecs_fini(world);
    return 0;
}
