// example_usage.c
#include "scene_loader.h"
#include <stdio.h>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <scene.yaml>\n", argv[0]);
        return 1;
    }
    
    scene_error_info_t error = {0};
    scene_t* scene = scene_load(argv[1], &error);
    
    if (!scene) {
        fprintf(stderr, "Failed to load scene: %s\n", error.message);
        if (error.path[0]) {
            fprintf(stderr, "  at: %s\n", error.path);
        }
        if (error.line > 0) {
            fprintf(stderr, "  line %d, column %d\n", error.line, error.column);
        }
        return 1;
    }
    
    printf("Loaded scene: %s v%s\n", scene->metadata.name, scene->metadata.version);
    printf("Entities: %zu\n", scene->entities_count);
    
    // Find and inspect an entity
    entity_t* player = scene_find_entity(scene, "Player");
    if (player) {
        printf("\nPlayer entity:\n");
        if (player->meta && player->meta->description) {
            printf("  Description: %s\n", player->meta->description);
        }
        printf("  Components: %zu\n", player->components_count);
        printf("  Tags: %zu\n", player->tags_count);
        
        component_t* transform = entity_find_component(player, "Transform");
        if (transform) {
            printf("  Has Transform component\n");
        }
        
        if (entity_has_tag(player, "Controllable")) {
            printf("  Is controllable\n");
        }
    }
    
    // Validate scene
    if (!scene_validate(scene, &error)) {
        fprintf(stderr, "Scene validation failed: %s\n", error.message);
    }
    
    scene_free(scene);
    return 0;
}
