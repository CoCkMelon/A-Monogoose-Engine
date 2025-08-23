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

static void emit_entity_ids_json(strbuf_t *sb, const entity_t *e) {
    // ids: aggregate tags and component type names. Prefab is emitted as tag as well.
    sb_append(sb, "\"ids\":[");
    bool first = true;
    // Components as ids
    for (size_t i = 0; i < e->components_count; i++) {
        if (!e->components[i].type_name) continue;
        if (!first) sb_append_ch(sb, ','); first = false;
        sb_append_escaped_str(sb, e->components[i].type_name);
    }
    // Tags
    for (size_t i = 0; i < e->tags_count; i++) {
        if (!e->tags[i]) continue;
        if (!first) sb_append_ch(sb, ','); first = false;
        sb_append_escaped_str(sb, e->tags[i]);
    }
    // Disabled flag -> add Disabled tag if not enabled
    if (!e->enabled) {
        if (!first) sb_append_ch(sb, ','); first = false;
        sb_append_escaped_str(sb, "Disabled");
    }
    sb_append_ch(sb, ']');
}

static char* scene_to_flecs_json(const scene_t *scene) {
    (void)emit_component_value_json; // values not included without reflection
    strbuf_t sb; sb_init(&sb);
    // World JSON format expected by ecs_world_from_json:
    // { "results": [ { "entities": [ { "name":..., "ids":[...] }, ... ] } ] }
    sb_append(&sb, "{\"results\":[{");
    sb_append(&sb, "\"entities\":[");
    for (size_t i = 0; i < scene->entities_count; i++) {
        const entity_t *e = &scene->entities[i];
        if (i) sb_append_ch(&sb, ',');
        sb_append_ch(&sb, '{');
        // ids only (names are not part of supported input fields)
        emit_entity_ids_json(&sb, e);
        // Note: we do not emit "values" since we don't have reflection for component types here.
        sb_append_ch(&sb, '}');
    }
    sb_append(&sb, "]}"); // close entities object
    sb_append(&sb, "]}"); // close results + root
    return sb.data; // caller takes ownership
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <scene.yaml> [--print-json]\n", argv[0]);
        return 1;
    }

    const char *scene_path = argv[1];
    bool print_json = false;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--print-json") == 0) print_json = true;
    }

    scene_error_info_t err = {0};
    scene_t *scene = scene_load(scene_path, &err);
    if (!scene) {
        fprintf(stderr, "Failed to load scene: %s\n", err.message);
        if (err.path[0]) fprintf(stderr, " at %s\n", err.path);
        if (err.line) fprintf(stderr, " line %d col %d\n", err.line, err.column);
        return 2;
    }

    char *json = scene_to_flecs_json(scene);
    if (!json) {
        fprintf(stderr, "Failed to convert scene to Flecs JSON\n");
        scene_free(scene);
        return 3;
    }

    if (print_json) {
        printf("%s\n", json);
    }

    // Initialize flecs world and load JSON
    ecs_world_t *world = ecs_init();
    if (!world) {
        fprintf(stderr, "Failed to init Flecs world\n");
        free(json);
        scene_free(scene);
        return 4;
    }

    const char *res = ecs_world_from_json(world, json, NULL);
    if (!res) {
        fprintf(stderr, "ecs_world_from_json failed\n");
        ecs_fini(world);
        free(json);
        scene_free(scene);
        return 5;
    }

    // Basic success message (component values require reflection to be set)
    printf("Loaded scene into Flecs world (entities requested: %zu).\n", scene->entities_count);

    ecs_fini(world);
    free(json);
    scene_free(scene);
    return 0;
}
