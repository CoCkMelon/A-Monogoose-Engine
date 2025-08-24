#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <flecs.h>
#include <flecs/addons/script.h>
#include <flecs/addons/json.h>

// Minimal demo component with reflection so Flecs Script can set numeric fields
typedef struct Position { float x, y, z; } Position;

static void register_components(ecs_world_t *world) {
    ECS_COMPONENT(world, Position);
    // Register meta info for Position
    ecs_struct(world, {
        .entity = ecs_id(Position),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) },
            { .name = "z", .type = ecs_id(ecs_f32_t) },
        }
    });
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s [--script <file.ffs>] [--expr <script>] [--dump-json]\n\n"
        "Examples:\n"
        "  %s --expr \"Player\\n  Position { x: 1, y: 2, z: 3 }\" --dump-json\n"
        "  %s --script scenes/level01.ffs --dump-json\n",
        argv0, argv0, argv0);
}

int main(int argc, char **argv) {
    const char *script_file = NULL;
    const char *script_expr = NULL;
    bool dump_json = false;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--script") && i + 1 < argc) {
            script_file = argv[++i];
        } else if (!strcmp(argv[i], "--expr") && i + 1 < argc) {
            script_expr = argv[++i];
        } else if (!strcmp(argv[i], "--dump-json")) {
            dump_json = true;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 2;
        }
    }

    if (!script_file && !script_expr) {
        // Provide a small default inline scene if none specified
        script_expr = "// default demo scene\n"
                      "Level01\n"
                      "Player\n"
                      "  Position { x: 0, y: 1, z: 0 }\n"
                      "  ChildOf(Level01)\n"
                      "Enemy01\n"
                      "  Position { x: 10, y: 0, z: 5 }\n"
                      "  ChildOf(Level01)\n";
    }

    ecs_world_t *world = ecs_init();
    if (!world) {
        fprintf(stderr, "ecs_init failed\n");
        return 1;
    }

    // Register demo components + meta
    register_components(world);

    int rc = 0;
    if (script_file) {
        rc = ecs_script_run_file(world, script_file);
        if (rc != 0) {
            fprintf(stderr, "Failed to run script file: %s (rc=%d)\n", script_file, rc);
            ecs_fini(world);
            return 1;
        }
    }
    if (script_expr) {
        rc = ecs_script_run(world, "inline", script_expr, NULL);
        if (rc != 0) {
            fprintf(stderr, "Failed to run script expr (rc=%d)\n", rc);
            ecs_fini(world);
            return 1;
        }
    }

    if (dump_json) {
        ecs_world_to_json_desc_t jd = {0};
        char *json = ecs_world_to_json(world, &jd);
        if (json) {
            puts(json);
            ecs_os_free(json);
        }
    }

    // Simple verification: ensure Player exists
    ecs_entity_t player = ecs_lookup(world, "Player");
    if (player) {
        ecs_entity_t pos_id = ecs_lookup(world, "Position");
        const Position *p = pos_id ? ecs_get_id(world, player, pos_id) : NULL;
        if (p) {
            printf("Player Position: (%.2f, %.2f, %.2f)\n", p->x, p->y, p->z);
        } else {
            printf("Player exists (no Position)\n");
        }
    }

    ecs_fini(world);
    return 0;
}
