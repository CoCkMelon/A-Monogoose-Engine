// scene_roundtrip_safe.cpp
// Safe C++ rewrite of the YAML -> Flecs JSON -> ECS world -> Flecs JSON -> YAML roundtrip
// - Uses RAII and std::string/std::vector for safe memory management
// - Reuses existing C scene model (scene_loader.h) and world extraction (world_to_scene.h)
// - Uses Flecs C API JSON loader/dumper
//
// Build suggestion (add to your CMake if desired):
// add_executable(scene_roundtrip_safe examples/scene_file/trash/scene_roundtrip_safe.cpp)
// target_include_directories(scene_roundtrip_safe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/examples/scene_file/trash)

#define FLECS_NO_CPP 1
#include <flecs.h>
#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <utility>

// Include the C headers, but work around the 'namespace' field name by redefining it
extern "C" {
#define namespace namespace_
#include "scene_loader.h"
#include "world_to_scene.h"
#undef namespace
}

namespace safe {

// Unique_ptr deleters for C resources
struct SceneDeleter {
    void operator()(scene_t* s) const noexcept { if (s) scene_free(s); }
};
using ScenePtr = std::unique_ptr<scene_t, SceneDeleter>;

struct WorldDeleter {
    void operator()(ecs_world_t* w) const noexcept { if (w) ecs_fini(w); }
};
using WorldPtr = std::unique_ptr<ecs_world_t, WorldDeleter>;

// Small helpers
static inline bool sv_empty(const char* s) { return !s || !*s; }

// JSON escaping into std::string
static std::string json_escape(std::string_view in) {
    std::string out;
    out.reserve(in.size() + 8);
    out.push_back('"');
    for (unsigned char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(c));
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
    return out;
}

// Emit component_value_t to JSON safely (mirrors C version semantics)
static void emit_component_value_json(std::string &dst, const component_value_t &v);

static void emit_object_json(std::string &dst, const component_value_t &v) {
    dst.push_back('{');
    for (size_t i = 0; i < v.object_val.count; i++) {
        if (i) dst.push_back(',');
        const auto &item = v.object_val.items[i];
        dst += json_escape(item.key ? item.key : "");
        dst.push_back(':');
        if (item.value) emit_component_value_json(dst, *item.value); else dst += "null";
    }
    dst.push_back('}');
}

static void emit_array_json(std::string &dst, const component_value_t &v) {
    dst.push_back('[');
    for (size_t i = 0; i < v.array_val.count; i++) {
        if (i) dst.push_back(',');
        emit_component_value_json(dst, v.array_val.values[i]);
    }
    dst.push_back(']');
}

static void emit_component_value_json(std::string &dst, const component_value_t &v) {
    switch (v.type) {
        case COMPONENT_TYPE_NULL: dst += "null"; break;
        case COMPONENT_TYPE_BOOL: dst += (v.bool_val ? "true" : "false"); break;
        case COMPONENT_TYPE_INT: {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(v.int_val));
            dst += buf;
            break;
        }
        case COMPONENT_TYPE_FLOAT: {
            char buf[64];
            // Use %g for compact yet readable
            std::snprintf(buf, sizeof buf, "%g", v.float_val);
            dst += buf;
            break;
        }
        case COMPONENT_TYPE_STRING:
            dst += json_escape(v.string_val ? v.string_val : "");
            break;
        case COMPONENT_TYPE_ARRAY:
            emit_array_json(dst, v);
            break;
        case COMPONENT_TYPE_OBJECT:
            emit_object_json(dst, v);
            break;
        default:
            dst += "null";
            break;
    }
}

// Find parent of an entity by scanning flat relations
static const char* find_parent_of(const scene_t *scene, const char *child) {
    if (!scene || !child) return nullptr;
    for (size_t i = 0; i < scene->hierarchy_relations_count; i++) {
        const auto &rel = scene->hierarchy_relations[i];
        if (rel.child && std::strcmp(rel.child, child) == 0) return rel.parent;
    }
    return nullptr;
}

static std::string build_full_path(const scene_t *scene, const char *name) {
    // collect parts upward
    std::vector<const char*> parts;
    const char *cur = name;
    while (cur && *cur) {
        parts.push_back(cur);
        cur = find_parent_of(scene, cur);
    }
    // join in reverse with '.'
    std::string out;
    size_t total = 0;
    for (size_t i = 0; i < parts.size(); i++) total += std::strlen(parts[i]) + (i ? 1 : 0);
    out.reserve(total);
    for (size_t i = 0; i < parts.size(); i++) {
        if (i) out.push_back('.');
        const char *p = parts[parts.size() - 1 - i];
        out.append(p ? p : "");
    }
    return out;
}

static void emit_tags_array(std::string &dst, const entity_t *e) {
    if (!e || e->tags_count == 0) return;
    dst += ",\"tags\":[";
    for (size_t i = 0; i < e->tags_count; i++) {
        if (i) dst.push_back(',');
        dst += json_escape(e->tags[i] ? e->tags[i] : "");
    }
    dst.push_back(']');
}

static std::string entity_to_flecs_entity_json(const scene_t *scene, const entity_t *e, bool names_as_paths) {
    std::string sb;
    sb.reserve(1024);
    sb.push_back('{');

    const char *parent = names_as_paths ? nullptr : find_parent_of(scene, e->name);
    bool wrote_any = false;

    if (parent && *parent) {
        sb += "\"parent\":";
        sb += json_escape(parent);
        wrote_any = true;
    }

    if (wrote_any) sb.push_back(',');
    sb += "\"name\":";
    if (names_as_paths) {
        std::string path = build_full_path(scene, e->name);
        sb += json_escape(path);
    } else {
        sb += json_escape(e->name ? e->name : "");
    }
    wrote_any = true;

    emit_tags_array(sb, e);

    if (!sv_empty(e->prefab)) {
        sb += ",\"pairs\":{";
        sb += json_escape("IsA");
        sb.push_back(':');
        sb += json_escape(e->prefab);
        sb.push_back('}');
    }

    if (e->components_count > 0) {
        sb += ",\"components\":{";
        for (size_t i = 0; i < e->components_count; i++) {
            if (i) sb.push_back(',');
            const component_t &c = e->components[i];
            sb += json_escape(c.type_name ? c.type_name : "");
            sb.push_back(':');
            emit_component_value_json(sb, c.data);
        }
        sb.push_back('}');
    }

    sb.push_back('}');
    return sb;
}

static void emit_constraints_pairs(std::string &dst, const scene_t *scene, bool &first_entity) {
    // Emit anonymous entities that carry joint relationships
    if (!scene) return;
    for (size_t i = 0; i < scene->constraints.joints_count; i++) {
        const auto &j = scene->constraints.joints[i];
        if (first_entity) first_entity = false; else dst.push_back(',');
        dst.push_back('{');
        // name: "#<i>"
        char namebuf[64];
        std::snprintf(namebuf, sizeof namebuf, "\"name\":\"#%zu\"", i + 1);
        dst += namebuf;
        // pairs
        dst += ",\"pairs\":{";
        dst += json_escape("JointType"); dst.push_back(':'); dst += json_escape(j.type ? j.type : "");
        if (j.entity_a) { dst.push_back(','); dst += json_escape("JointA"); dst.push_back(':'); dst += json_escape(j.entity_a); }
        if (j.entity_b) { dst.push_back(','); dst += json_escape("JointB"); dst.push_back(':'); dst += json_escape(j.entity_b); }
        dst.push_back('}');
        dst.push_back('}');
    }
}

static std::string build_world_json_from_scene(const scene_t *scene, bool names_as_paths) {
    std::string dst;
    dst.reserve(4096);
    dst += "{\"results\":[";
    bool first = true;
    if (scene) {
        for (size_t i = 0; i < scene->entities_count; i++) {
            if (!first) dst.push_back(',');
            first = false;
            dst += entity_to_flecs_entity_json(scene, &scene->entities[i], names_as_paths);
        }
        emit_constraints_pairs(dst, scene, first);
    }
    dst += "]}";
    return dst;
}

// Register example meta types (optional). For this safe C++ roundtrip we skip
// registering detailed meta reflection, as Flecs will still accept component
// values for known components and ignore values without reflection. If you want
// reflection for values, consider moving the C register_meta_types() into a
// separate C translation unit and link to it.
static void register_meta_types(ecs_world_t *world) {
    (void)world;
}

static void preregister_tags_and_prefabs(ecs_world_t *world, const scene_t *scene) {
    const char* tags[] = { "Controllable", "Persistent", "EntityGroup" };
    for (const char* t : tags) {
        ecs_entity_desc_t ed{}; ed.name = t; (void)ecs_entity_init(world, &ed);
    }
    if (!scene) return;
    for (size_t i = 0; i < scene->entities_count; i++) {
        const entity_t &e = scene->entities[i];
        if (e.prefab && e.prefab[0]) {
            ecs_entity_desc_t ed{}; ed.name = e.prefab; ecs_entity_t p = ecs_entity_init(world, &ed);
            ecs_add_id(world, p, EcsPrefab);
        }
    }
}

static void precreate_entities(ecs_world_t *world, const scene_t *scene) {
    if (!scene) return;
    for (size_t i = 0; i < scene->entities_count; i++) {
        const entity_t &e = scene->entities[i];
        if (e.name && e.name[0]) {
            ecs_entity_desc_t ed{}; ed.name = e.name; (void)ecs_entity_init(world, &ed);
        }
    }
}

} // namespace safe

int main(int argc, char **argv) {
    using namespace safe;

    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <scene.yaml> [--print-json] [--world-json] [--names-as-paths] [--dump-world-json] [--print-yaml]\n", argv[0]);
        return 1;
    }

    const char *scene_path = argv[1];
    bool print_json = false;
    bool world_json_mode = false;
    bool names_as_paths = false;
    bool dump_world_json = false;
    bool print_yaml = false;
    for (int i = 2; i < argc; i++) {
        if (std::strcmp(argv[i], "--print-json") == 0) print_json = true;
        else if (std::strcmp(argv[i], "--world-json") == 0) world_json_mode = true;
        else if (std::strcmp(argv[i], "--names-as-paths") == 0) names_as_paths = true;
        else if (std::strcmp(argv[i], "--dump-world-json") == 0) dump_world_json = true;
        else if (std::strcmp(argv[i], "--print-yaml") == 0) print_yaml = true;
    }

    scene_error_info_t err{};
    ScenePtr scene(scene_load(scene_path, &err));
    if (!scene) {
        std::fprintf(stderr, "Failed to load scene: %s\n", err.message);
        if (err.path[0]) std::fprintf(stderr, " at %s\n", err.path);
        if (err.line) std::fprintf(stderr, " line %d col %d\n", err.line, err.column);
        return 2;
    }

    WorldPtr world(ecs_init());
    if (!world) {
        std::fprintf(stderr, "Failed to init Flecs world\n");
        return 4;
    }

    register_meta_types(world.get());
    preregister_tags_and_prefabs(world.get(), scene.get());
    precreate_entities(world.get(), scene.get());

    if (world_json_mode) {
        std::string wjson = build_world_json_from_scene(scene.get(), names_as_paths);
        if (print_json) std::cout << wjson << std::endl;
        const char *res = ecs_world_from_json(world.get(), wjson.c_str(), nullptr);
        if (!res) {
            std::fprintf(stderr, "ecs_world_from_json failed\n");
            return 6;
        }
        std::printf("Loaded %zu entities into Flecs world via world_from_json.\n", scene->entities_count);
    } else {
        for (size_t i = 0; i < scene->entities_count; i++) {
            const entity_t &ent = scene->entities[i];
            ecs_entity_t e_id = ecs_lookup(world.get(), ent.name);
            if (!e_id) {
                std::fprintf(stderr, "Lookup failed for entity name: %s\n", ent.name);
                return 5;
            }
            std::string ejson = entity_to_flecs_entity_json(scene.get(), &ent, names_as_paths);
            if (print_json) std::cout << ejson << std::endl;
            const char *res = ecs_entity_from_json(world.get(), e_id, ejson.c_str(), nullptr);
            if (!res) {
                std::fprintf(stderr, "ecs_entity_from_json failed for %s\n", ent.name);
                return 6;
            }
        }
        std::printf("Loaded %zu entities into Flecs world via entity_from_json.\n", scene->entities_count);
    }

    if (dump_world_json) {
        ecs_world_to_json_desc_t d{};
        char *wj = ecs_world_to_json(world.get(), &d);
        if (wj) {
            std::puts(wj);
            ecs_os_free(wj);
        }
    }

    if (print_yaml) {
        // Original YAML
        if (char *yaml = scene_to_yaml(scene.get())) { std::puts(yaml); std::free(yaml); }
        // Reconstructed from world
        if (scene_t *from_world_raw = scene_from_world(world.get(), scene->metadata.name, scene->metadata.version)) {
            ScenePtr from_world(from_world_raw);
            if (char *yaml2 = scene_to_yaml(from_world.get())) {
                std::puts("--- yaml from world ---");
                std::puts(yaml2);
                std::free(yaml2);
            }
        }
    }

    return 0;
}

