// scene_yaml_to_flecs_json.c
// Convert our YAML scene to Flecs Remote API JSON and load it into a world
// Build target is already wired in CMakeLists.txt as scene_yaml_to_flecs_json

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include <flecs.h>

#include "scene_loader.h"
#include "world_to_scene.h"

// --- Simple component type definitions & meta registration for this example ---
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

static void register_meta_types(ecs_world_t *world) {
    // Primitive alias/meta structs
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

    // Components with meta
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

    ecs_entity_t Health = ecs_component_init(world, &(ecs_component_desc_t){
        .entity = ecs_entity(world, { .name = "Health" }),
        .type = { .size = (int32_t)sizeof(HealthCmp), .alignment = (int32_t)_Alignof(HealthCmp) }
    });
    (void)ecs_struct(world, {
        .entity = Health,
        .members = {
            { .name = "current", .type = ecs_id(ecs_i32_t) },
            { .name = "max", .type = ecs_id(ecs_i32_t) },
        }
    });

    ecs_entity_t Velocity = ecs_component_init(world, &(ecs_component_desc_t){
        .entity = ecs_entity(world, { .name = "Velocity" }),
        .type = { .size = (int32_t)sizeof(VelocityCmp), .alignment = (int32_t)_Alignof(VelocityCmp) }
    });
    (void)ecs_struct(world, {
        .entity = Velocity,
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) },
            { .name = "z", .type = ecs_id(ecs_f32_t) },
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

static void preregister_tags_and_prefabs(ecs_world_t *world, const scene_t *scene) {
    // Create tags that appear in YAML
    const char* tags[] = { "Controllable", "Persistent", "EntityGroup" };
    for (size_t i = 0; i < sizeof(tags)/sizeof(tags[0]); i++) {
        ecs_entity(world, { .name = tags[i] });
    }
    // Create prefab entities if referenced
    for (size_t i = 0; i < scene->entities_count; i++) {
        const entity_t *e = &scene->entities[i];
        if (e->prefab && e->prefab[0]) {
            ecs_entity_t p = ecs_entity(world, { .name = e->prefab });
            ecs_add_id(world, p, EcsPrefab);
        }
    }
}

static void precreate_entities(ecs_world_t *world, const scene_t *scene) {
    for (size_t i = 0; i < scene->entities_count; i++) {
        const entity_t *e = &scene->entities[i];
        if (e->name && e->name[0]) {
            ecs_entity_desc_t ed = {0};
            ed.name = e->name;
            (void)ecs_entity_init(world, &ed);
        }
    }
}

// Simple string builder
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} strbuf_t;

static void sb_init(strbuf_t *sb) {
    sb->data = NULL; sb->len = 0; sb->cap = 0;
}

static void sb_reserve(strbuf_t *sb, size_t extra) {
    if (sb->len + extra + 1 > sb->cap) {
        size_t ncap = sb->cap ? sb->cap * 2 : 256;
        while (ncap < sb->len + extra + 1) ncap *= 2;
        char *n = (char*)realloc(sb->data, ncap);
        if (!n) { fprintf(stderr, "Out of memory in strbuf reserve\n"); exit(1);} 
        sb->data = n; sb->cap = ncap;
    }
}

static void sb_append_n(strbuf_t *sb, const char *s, size_t n) {
    sb_reserve(sb, n);
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

static void sb_append(strbuf_t *sb, const char *s) {
    sb_append_n(sb, s, strlen(s));
}

static void sb_append_ch(strbuf_t *sb, char c) {
    sb_reserve(sb, 1);
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
}

static void sb_append_escaped_str(strbuf_t *sb, const char *s) {
    sb_append_ch(sb, '"');
    for (const char *p = s ? s : ""; *p; p++) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
            case '"': sb_append(sb, "\\\""); break;
            case '\\': sb_append(sb, "\\\\"); break;
            case '\b': sb_append(sb, "\\b"); break;
            case '\f': sb_append(sb, "\\f"); break;
            case '\n': sb_append(sb, "\\n"); break;
            case '\r': sb_append(sb, "\\r"); break;
            case '\t': sb_append(sb, "\\t"); break;
            default:
                if (c < 0x20) {
                    char tmp[8];
                    snprintf(tmp, sizeof tmp, "\\u%04x", c);
                    sb_append(sb, tmp);
                } else {
                    sb_append_ch(sb, (char)c);
                }
        }
    }
    sb_append_ch(sb, '"');
}

// Convert component values (variant) to JSON (best effort). This is useful for emitting
// a richer JSON document. Flecs will ignore values for components that don't have
// reflection data registered, but the JSON remains a faithful representation.
static void emit_component_value_json(strbuf_t *sb, const component_value_t *v);

static void emit_object_json(strbuf_t *sb, const component_value_t *v) {
    sb_append_ch(sb, '{');
    for (size_t i = 0; i < v->object_val.count; i++) {
        if (i) sb_append_ch(sb, ',');
        sb_append_escaped_str(sb, v->object_val.items[i].key);
        sb_append_ch(sb, ':');
        emit_component_value_json(sb, v->object_val.items[i].value);
    }
    sb_append_ch(sb, '}');
}

static void emit_array_json(strbuf_t *sb, const component_value_t *v) {
    sb_append_ch(sb, '[');
    for (size_t i = 0; i < v->array_val.count; i++) {
        if (i) sb_append_ch(sb, ',');
        emit_component_value_json(sb, &v->array_val.values[i]);
    }
    sb_append_ch(sb, ']');
}

static void emit_component_value_json(strbuf_t *sb, const component_value_t *v) {
    switch (v->type) {
        case COMPONENT_TYPE_NULL:
            sb_append(sb, "null");
            break;
        case COMPONENT_TYPE_BOOL:
            sb_append(sb, v->bool_val ? "true" : "false");
            break;
        case COMPONENT_TYPE_INT: {
            char tmp[64]; snprintf(tmp, sizeof tmp, "%lld", (long long)v->int_val);
            sb_append(sb, tmp);
            break;
        }
        case COMPONENT_TYPE_FLOAT: {
            char tmp[64]; snprintf(tmp, sizeof tmp, "%g", v->float_val);
            sb_append(sb, tmp);
            break;
        }
        case COMPONENT_TYPE_STRING:
            sb_append_escaped_str(sb, v->string_val ? v->string_val : "");
            break;
        case COMPONENT_TYPE_ARRAY:
            emit_array_json(sb, v);
            break;
        case COMPONENT_TYPE_OBJECT:
            emit_object_json(sb, v);
            break;
        default:
            sb_append(sb, "null");
            break;
    }
}

static void emit_entity_ids_and_values_json(strbuf_t *sb, const entity_t *e, const char* parent_name, bool include_values) {
    // ids: aggregate components, tags, and pairs. values: aligned entries (component values or null)
    sb_append(sb, "\"ids\":[");
    strbuf_t vsb; sb_init(&vsb); // will hold just the comma-separated elements for values
    bool first = true; bool firstv = true;

    // Components as ids + values
    for (size_t i = 0; i < e->components_count; i++) {
        if (!e->components[i].type_name) continue;
        if (!first) sb_append_ch(sb, ','); first = false;
        sb_append_escaped_str(sb, e->components[i].type_name);
        if (include_values) {
            if (!firstv) sb_append_ch(&vsb, ','); firstv = false;
            emit_component_value_json(&vsb, &e->components[i].data);
        }
    }
    // Tags
    for (size_t i = 0; i < e->tags_count; i++) {
        if (!e->tags[i]) continue;
        if (!first) sb_append_ch(sb, ','); first = false;
        sb_append_escaped_str(sb, e->tags[i]);
        if (include_values) {
            if (!firstv) sb_append_ch(&vsb, ','); firstv = false;
            sb_append(&vsb, "null");
        }
    }
    // Prefab -> IsA pair
    if (e->prefab && e->prefab[0]) {
        if (!first) sb_append_ch(sb, ','); first = false;
        // Pair as object with rel/obj
        sb_append(sb, "{\"rel\":");
        sb_append_escaped_str(sb, "IsA");
        sb_append(sb, ",\"obj\":");
        sb_append_escaped_str(sb, e->prefab);
        sb_append_ch(sb, '}');
        if (include_values) {
            if (!firstv) sb_append_ch(&vsb, ','); firstv = false;
            sb_append(&vsb, "null");
        }
    }
    // Hierarchy -> ChildOf pair
    if (parent_name && parent_name[0]) {
        if (!first) sb_append_ch(sb, ','); first = false;
        // Pair as object with rel/obj
        sb_append(sb, "{\"rel\":");
        sb_append_escaped_str(sb, "ChildOf");
        sb_append(sb, ",\"obj\":");
        sb_append_escaped_str(sb, parent_name);
        sb_append_ch(sb, '}');
        if (include_values) {
            if (!firstv) sb_append_ch(&vsb, ','); firstv = false;
            sb_append(&vsb, "null");
        }
    }
    // Disabled flag -> add Disabled tag if not enabled
    if (!e->enabled) {
        if (!first) sb_append_ch(sb, ','); first = false;
        sb_append_escaped_str(sb, "Disabled");
        if (include_values) {
            if (!firstv) sb_append_ch(&vsb, ','); firstv = false;
            sb_append(&vsb, "null");
        }
    }
    sb_append_ch(sb, ']');
    if (include_values) {
        sb_append(sb, ",\"values\":[");
        if (vsb.data && vsb.len > 0) {
            sb_append_n(sb, vsb.data, vsb.len);
        }
        sb_append_ch(sb, ']');
        free(vsb.data);
    }
}

// Find parent name for a given entity from flat relations
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
    // Build dotted path by walking up parents
    const char *parts[128];
    size_t count = 0;
    const char *cur = name;
    while (cur && cur[0] && count < 128) {
        parts[count++] = cur;
        cur = find_parent_of(scene, cur);
    }
    // Join in reverse
    size_t total = 0;
    for (size_t i = 0; i < count; i++) total += strlen(parts[i]) + (i ? 1 : 0);
    char *out = (char*)malloc(total + 1);
    if (!out) return NULL;
    size_t pos = 0;
    for (size_t i = 0; i < count; i++) {
        const char *p = parts[count - 1 - i];
        size_t len = strlen(p);
        if (i) out[pos++] = '.';
        memcpy(out + pos, p, len);
        pos += len;
    }
    out[pos] = '\0';
    return out;
}

static void emit_tags_array(strbuf_t *sb, const entity_t *e) {
    if (!e->tags_count) return;
    sb_append(sb, ",\"tags\":[");
    for (size_t i = 0; i < e->tags_count; i++) {
        if (i) sb_append_ch(sb, ',');
        sb_append_escaped_str(sb, e->tags[i]);
    }
    sb_append_ch(sb, ']');
}

static void emit_pairs_array(strbuf_t *sb, const char* parent, const entity_t *e) {
    bool have_any = (parent && parent[0]) || (e->prefab && e->prefab[0]);
    if (!have_any) return;
    sb_append(sb, ",\"pairs\":[");
    bool first = true;
    if (parent && parent[0]) {
        sb_append_escaped_str(sb, "ChildOf(");
        // We need to embed name within the same string; build it manually
        // Simpler: build the string fully
    }
}

static char* entity_to_flecs_entity_json(const scene_t *scene, const entity_t *e, bool names_as_paths) {
    strbuf_t sb; sb_init(&sb);
    sb_append_ch(&sb, '{');

    // parent must come first if present, unless names are emitted as full paths
    const char* parent = names_as_paths ? NULL : find_parent_of(scene, e->name);
    bool wrote_field = false;
    if (parent && parent[0]) {
        sb_append(&sb, "\"parent\":");
        sb_append_escaped_str(&sb, parent);
        wrote_field = true;
    }

    // name (required next)
    if (wrote_field) sb_append_ch(&sb, ',');
    sb_append(&sb, "\"name\":");
    if (names_as_paths) {
        char *path = build_full_path(scene, e->name);
        sb_append_escaped_str(&sb, path ? path : (e->name ? e->name : ""));
        if (path) free(path);
    } else {
        sb_append_escaped_str(&sb, e->name ? e->name : "");
    }
    wrote_field = true;

    // tags
    emit_tags_array(&sb, e);

    // pairs: object form, only include IsA here (parent is handled by 'parent')
    if (e->prefab && e->prefab[0]) {
        sb_append(&sb, ",\"pairs\":{");
        sb_append_escaped_str(&sb, "IsA");
        sb_append_ch(&sb, ':');
        sb_append_escaped_str(&sb, e->prefab);
        sb_append_ch(&sb, '}');
    }

    // components
    if (e->components_count > 0) {
        sb_append(&sb, ",\"components\":{");
        for (size_t i = 0; i < e->components_count; i++) {
            if (i) sb_append_ch(&sb, ',');
            sb_append_escaped_str(&sb, e->components[i].type_name);
            sb_append_ch(&sb, ':');
            emit_component_value_json(&sb, &e->components[i].data);
        }
        sb_append_ch(&sb, '}');
    }

    sb_append_ch(&sb, '}');
    return sb.data;
}

static void emit_constraints_pairs(strbuf_t *sb, const scene_t *scene, bool *first_entity) {
    // Emit dummy entities that carry joint relationships, or attach as pairs to entity_a
    // For simplicity, create anonymous joint entities with name "#<i>" and pairs: {"Joint": ["type"], "A": "pathA", "B": "pathB"}
    for (size_t i = 0; i < scene->constraints.joints_count; i++) {
        const joint_constraint_t *j = &scene->constraints.joints[i];
        if (*first_entity) *first_entity = false; else sb_append_ch(sb, ',');
        sb_append(sb, "{");
        // anonymous id to avoid name collision
        char num[64]; snprintf(num, sizeof num, "\"name\":\"#%zu\"", i+1);
        sb_append(sb, num);
        // pairs mapping to describe joint
        sb_append(sb, ",\"pairs\":{");
        sb_append_escaped_str(sb, "JointType"); sb_append_ch(sb, ':'); sb_append_escaped_str(sb, j->type ? j->type : "");
        if (j->entity_a) { sb_append_ch(sb, ','); sb_append_escaped_str(sb, "JointA"); sb_append_ch(sb, ':'); sb_append_escaped_str(sb, j->entity_a); }
        if (j->entity_b) { sb_append_ch(sb, ','); sb_append_escaped_str(sb, "JointB"); sb_append_ch(sb, ':'); sb_append_escaped_str(sb, j->entity_b); }
        sb_append_ch(sb, '}');
        sb_append_ch(sb, '}');
    }
}

static char* build_world_json_from_scene(const scene_t *scene, bool names_as_paths) {
    strbuf_t sb; sb_init(&sb);
    sb_append(&sb, "{\"results\":[");
    bool first = true;
    for (size_t i = 0; i < scene->entities_count; i++) {
        if (!first) sb_append_ch(&sb, ','); first = false;
        char *ejson = entity_to_flecs_entity_json(scene, &scene->entities[i], names_as_paths);
        sb_append(&sb, ejson);
        free(ejson);
    }
    // emit constraints as additional anonymous entities carrying pairs
    emit_constraints_pairs(&sb, scene, &first);
    sb_append(&sb, "]}");
    return sb.data;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <scene.yaml> [--print-json]\n", argv[0]);
        return 1;
    }

    const char *scene_path = argv[1];
    bool print_json = false;
    bool world_json_mode = false;
    bool names_as_paths = false;
    bool dump_world_json = false;
    bool print_yaml = false;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--print-json") == 0) print_json = true;
        else if (strcmp(argv[i], "--world-json") == 0) world_json_mode = true;
        else if (strcmp(argv[i], "--names-as-paths") == 0) names_as_paths = true;
        else if (strcmp(argv[i], "--dump-world-json") == 0) dump_world_json = true;
        else if (strcmp(argv[i], "--print-yaml") == 0) print_yaml = true;
    }

    scene_error_info_t err = {0};
    scene_t *scene = scene_load(scene_path, &err);
    if (!scene) {
        fprintf(stderr, "Failed to load scene: %s\n", err.message);
        if (err.path[0]) fprintf(stderr, " at %s\n", err.path);
        if (err.line) fprintf(stderr, " line %d col %d\n", err.line, err.column);
        return 2;
    }

    // Initialize flecs world and load JSON
    ecs_world_t *world = ecs_init();
    // Register reflection for known components
    register_meta_types(world);
    // Pre-register tags and prefab entities
    preregister_tags_and_prefabs(world, scene);
    // Pre-create all scene entities so pairs reference valid targets
    precreate_entities(world, scene);
    if (!world) {
        fprintf(stderr, "Failed to init Flecs world\n");
        scene_free(scene);
        return 4;
    }

    if (world_json_mode) {
        char *wjson = build_world_json_from_scene(scene, names_as_paths);
        if (print_json) {
            puts(wjson);
        }
        const char *res = ecs_world_from_json(world, wjson, NULL);
        free(wjson);
        if (!res) {
            fprintf(stderr, "ecs_world_from_json failed\n");
            ecs_fini(world);
            scene_free(scene);
            return 6;
        }
        printf("Loaded %zu entities into Flecs world via world_from_json.\n", scene->entities_count);
    } else {
        // For each entity, emit its JSON and apply to the precreated entity
        for (size_t i = 0; i < scene->entities_count; i++) {
            const entity_t *ent = &scene->entities[i];
            ecs_entity_t e_id = ecs_lookup(world, ent->name);
            if (!e_id) {
                fprintf(stderr, "Lookup failed for entity name: %s\n", ent->name);
                ecs_fini(world);
                scene_free(scene);
                return 5;
            }
            char *ejson = entity_to_flecs_entity_json(scene, ent, names_as_paths);
            if (print_json) {
                printf("%s\n", ejson);
            }
            const char *res = ecs_entity_from_json(world, e_id, ejson, NULL);
            free(ejson);
            if (!res) {
                fprintf(stderr, "ecs_entity_from_json failed for %s\n", ent->name);
                ecs_fini(world);
                scene_free(scene);
                return 6;
            }
        }
        printf("Loaded %zu entities into Flecs world via entity_from_json.\n", scene->entities_count);
    }

    // Optionally dump world -> Flecs JSON after load
    if (dump_world_json) {
        ecs_world_to_json_desc_t d = {0};
        // Keep defaults; world_to_json returns a full snapshot
        char *wj = ecs_world_to_json(world, &d);
        if (wj) {
            puts(wj);
            ecs_os_free(wj);
        }
    }

    // Optionally print YAML from the original scene model
    if (print_yaml) {
        char *yaml = scene_to_yaml(scene);
        if (yaml) { puts(yaml); free(yaml); }
    }

    // Also demonstrate reconstructing YAML from the ECS world
    if (print_yaml) {
        scene_t *from_world = scene_from_world(world, scene->metadata.name, scene->metadata.version);
        if (from_world) {
            char *yaml2 = scene_to_yaml(from_world);
            if (yaml2) { puts("--- yaml from world ---"); puts(yaml2); free(yaml2); }
            scene_free(from_world);
        }
    }

    ecs_fini(world);
    scene_free(scene);
    return 0;
}
