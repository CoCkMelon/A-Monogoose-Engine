#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <flecs.h>

#include "scene_loader.h"

// Minimal meta registration to allow JSON to set components
static void register_meta_types(ecs_world_t *world) {
    typedef struct { float x, y, z; } Position3;
    typedef struct { float x, y, z, w; } Rotation4;
    typedef struct { float x, y, z; } Scale3;
    typedef struct { Position3 position; Rotation4 rotation; Scale3 scale; } TransformCmp;
    typedef struct { int current, max; } HealthCmp;
    typedef struct { float x, y, z; } VelocityCmp;
    typedef struct { const char* path; } MeshCmp;
    typedef struct { const char* type; } ParticleEmitterCmp;
    typedef struct { float fov, near, far; } CameraCmp;
    typedef struct { float r, g, b, a; } Color;
    typedef struct { Color color; float intensity; } DirectionalLightCmp;
    typedef struct { Color color; } AmbientLightCmp;
    typedef struct { Position3 position; } NavigationNodeCmp;

    ecs_entity_t t_Position3 = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "Position3" }),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) },
            { .name = "z", .type = ecs_id(ecs_f32_t) },
        }
    }); (void)t_Position3;

    ecs_entity_t t_Rotation4 = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "Rotation4" }),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) },
            { .name = "z", .type = ecs_id(ecs_f32_t) },
            { .name = "w", .type = ecs_id(ecs_f32_t) },
        }
    }); (void)t_Rotation4;

    ecs_entity_t t_Scale3 = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "Scale3" }),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) },
            { .name = "z", .type = ecs_id(ecs_f32_t) },
        }
    }); (void)t_Scale3;

    ecs_entity_t t_Color = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "Color" }),
        .members = {
            { .name = "r", .type = ecs_id(ecs_f32_t) },
            { .name = "g", .type = ecs_id(ecs_f32_t) },
            { .name = "b", .type = ecs_id(ecs_f32_t) },
            { .name = "a", .type = ecs_id(ecs_f32_t) },
        }
    }); (void)t_Color;

    ecs_entity_t Transform = ecs_component_init(world, &(ecs_component_desc_t){
        .entity = ecs_entity(world, { .name = "Transform" }),
        .type = { .size = (int32_t)sizeof(TransformCmp), .alignment = (int32_t)_Alignof(TransformCmp) }
    });
    (void)ecs_struct(world, {
        .entity = Transform,
        .members = {
            { .name = "position", .type = ecs_lookup(world, "Position3") },
            { .name = "rotation", .type = ecs_lookup(world, "Rotation4") },
            { .name = "scale", .type = ecs_lookup(world, "Scale3") },
        }
    });

    ecs_entity_t Mesh = ecs_component_init(world, &(ecs_component_desc_t){
        .entity = ecs_entity(world, { .name = "Mesh" }),
        .type = { .size = (int32_t)sizeof(MeshCmp), .alignment = (int32_t)_Alignof(MeshCmp) }
    });
    (void)ecs_struct(world, {
        .entity = Mesh,
        .members = { { .name = "path", .type = ecs_id(ecs_string_t) } }
    });

    ecs_entity_t ParticleEmitter = ecs_component_init(world, &(ecs_component_desc_t){
        .entity = ecs_entity(world, { .name = "ParticleEmitter" }),
        .type = { .size = (int32_t)sizeof(ParticleEmitterCmp), .alignment = (int32_t)_Alignof(ParticleEmitterCmp) }
    });
    (void)ecs_struct(world, {
        .entity = ParticleEmitter,
        .members = { { .name = "type", .type = ecs_id(ecs_string_t) } }
    });

    ecs_entity_t Camera = ecs_component_init(world, &(ecs_component_desc_t){
        .entity = ecs_entity(world, { .name = "Camera" }),
        .type = { .size = (int32_t)sizeof(CameraCmp), .alignment = (int32_t)_Alignof(CameraCmp) }
    });
    (void)ecs_struct(world, {
        .entity = Camera,
        .members = {
            { .name = "fov", .type = ecs_id(ecs_f32_t) },
            { .name = "near", .type = ecs_id(ecs_f32_t) },
            { .name = "far", .type = ecs_id(ecs_f32_t) },
        }
    });

    ecs_entity_t DirectionalLight = ecs_component_init(world, &(ecs_component_desc_t){
        .entity = ecs_entity(world, { .name = "DirectionalLight" }),
        .type = { .size = (int32_t)sizeof(DirectionalLightCmp), .alignment = (int32_t)_Alignof(DirectionalLightCmp) }
    });
    (void)ecs_struct(world, {
        .entity = DirectionalLight,
        .members = {
            { .name = "color", .type = ecs_lookup(world, "Color") },
            { .name = "intensity", .type = ecs_id(ecs_f32_t) },
        }
    });

    ecs_entity_t AmbientLight = ecs_component_init(world, &(ecs_component_desc_t){
        .entity = ecs_entity(world, { .name = "AmbientLight" }),
        .type = { .size = (int32_t)sizeof(AmbientLightCmp), .alignment = (int32_t)_Alignof(AmbientLightCmp) }
    });
    (void)ecs_struct(world, {
        .entity = AmbientLight,
        .members = { { .name = "color", .type = ecs_lookup(world, "Color") } }
    });

    ecs_entity_t NavigationNode = ecs_component_init(world, &(ecs_component_desc_t){
        .entity = ecs_entity(world, { .name = "NavigationNode" }),
        .type = { .size = (int32_t)sizeof(NavigationNodeCmp), .alignment = (int32_t)_Alignof(NavigationNodeCmp) }
    });
    (void)ecs_struct(world, {
        .entity = NavigationNode,
        .members = { { .name = "position", .type = ecs_lookup(world, "Position3") } }
    });
}

static const char* find_parent_of(const scene_t *scene, const char* child) {
    if (!child) return NULL;
    for (size_t i = 0; i < scene->hierarchy_relations_count; i++) {
        const parent_child_relation_t *rel = &scene->hierarchy_relations[i];
        if (rel->child && strcmp(rel->child, child) == 0) {
            return rel->parent;
        }
    }
    return NULL;
}

static char* build_full_path(const scene_t *scene, const char *name) {
    const char *parts[128]; size_t count = 0; const char *cur = name;
    while (cur && cur[0] && count < 128) { parts[count++] = cur; cur = find_parent_of(scene, cur); }
    size_t total = 0; for (size_t i = 0; i < count; i++) total += strlen(parts[i]) + (i?1:0);
    char *out = (char*)malloc(total+1); if (!out) return NULL; size_t pos=0;
    for (size_t i = 0; i < count; i++) { const char *p = parts[count-1-i]; size_t l=strlen(p); if (i) out[pos++]='.'; memcpy(out+pos,p,l); pos+=l; }
    out[pos]='\0'; return out;
}

static void sb_append(char **buf, size_t *len, size_t *cap, const char *s) {
    size_t n = strlen(s);
    if (*len + n + 1 > *cap) { size_t nc = *cap? *cap*2:256; while (nc < *len+n+1) nc*=2; char *nb = realloc(*buf, nc); if (!nb) exit(1); *buf=nb; *cap=nc; }
    memcpy(*buf+*len, s, n); *len += n; (*buf)[*len]='\0';
}

static char* build_world_json_from_scene_local(const scene_t *scene) {
    char *buf=NULL; size_t len=0, cap=0; sb_append(&buf,&len,&cap, "{\"results\":[");
    for (size_t i=0;i<scene->entities_count;i++) {
        if (i) sb_append(&buf,&len,&cap, ",");
        const entity_t *e = &scene->entities[i];
        char *path = build_full_path(scene, e->name);
        sb_append(&buf,&len,&cap, "{\"name\":");
        // naive escape: paths are simple; skip for brevity
        sb_append(&buf,&len,&cap, "\""); sb_append(&buf,&len,&cap, path?path:e->name); sb_append(&buf,&len,&cap, "\"");
        if (e->prefab && e->prefab[0]) {
            sb_append(&buf,&len,&cap, ",\"pairs\":{");
            sb_append(&buf,&len,&cap, "\"IsA\":\""); sb_append(&buf,&len,&cap, e->prefab); sb_append(&buf,&len,&cap, "\"}");
        }
        if (e->components_count) {
            sb_append(&buf,&len,&cap, ",\"components\":{");
            for (size_t ci=0; ci<e->components_count; ci++) {
                if (ci) sb_append(&buf,&len,&cap, ",");
                sb_append(&buf,&len,&cap, "\""); sb_append(&buf,&len,&cap, e->components[ci].type_name); sb_append(&buf,&len,&cap, "\"");
                sb_append(&buf,&len,&cap, ":null");
            }
            sb_append(&buf,&len,&cap, "}");
        }
        sb_append(&buf,&len,&cap, "}");
        if (path) free(path);
    }
    sb_append(&buf,&len,&cap, "]}");
    return buf;
}

static int run_roundtrip(const char *yaml_path) {
    scene_error_info_t err = {0};
    scene_t *scene = scene_load(yaml_path, &err);
    if (!scene) {
        fprintf(stderr, "Failed to load scene: %s\n", err.message);
        return 2;
    }

    char *wjson = build_world_json_from_scene_local(scene);

    ecs_world_t *world = ecs_init();
    // register minimal meta
    register_meta_types(world);

    const char *res = ecs_world_from_json(world, wjson, NULL);
    free(wjson);
    assert(res != NULL);

    // Basic assertions on a few entities
    ecs_entity_t player = ecs_lookup(world, "Player");
    assert(player != 0);

    ecs_entity_t player_model = ecs_lookup(world, "Player.PlayerModel");
    assert(player_model != 0);
    ecs_entity_t mesh = ecs_lookup(world, "Mesh");
    assert(mesh != 0);
    assert(ecs_has_id(world, player_model, mesh));

    ecs_entity_t enemy1 = ecs_lookup(world, "Enemies.Enemy_1");
    assert(enemy1 != 0);
    ecs_entity_t isa = ecs_lookup(world, "IsA");
    assert(isa != 0);
    ecs_entity_t prefab = ecs_lookup(world, "Spawner::EnemyPrefab");
    assert(prefab != 0);
    assert(ecs_has_pair(world, enemy1, isa, prefab));

    ecs_fini(world);
    scene_free(scene);
    return 0;
}

int main(int argc, char **argv) {
    const char *yaml = argc > 1 ? argv[1] : "examples/scene_file/main_level.amongoose.yaml";
    int rc = run_roundtrip(yaml);
    if (rc == 0) {
        puts("roundtrip ok");
    }
    return rc;
}

