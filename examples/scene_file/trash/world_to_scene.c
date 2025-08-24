#include "world_to_scene.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static char* dupstr(const char *s){ return s?strdup(s):NULL; }

// Helpers to add to dynamic arrays
static void arr_push_ptr(void ***arr, size_t *count, size_t item_size, const void *item){
    void *nb = realloc(*arr, (*count + 1) * item_size);
    if (!nb) return; *arr = nb; memcpy((char*)(*arr) + (*count)*item_size, item, item_size); (*count)++;
}

static entity_t* scene_add_entity(scene_t *scene, const char *name){
    scene->entities = (entity_t*)realloc(scene->entities, (scene->entities_count+1)*sizeof(entity_t));
    entity_t *e = &scene->entities[scene->entities_count++];
    memset(e, 0, sizeof *e);
    e->name = dupstr(name);
    e->enabled = true;
    return e;
}

static void entity_add_tag(entity_t *e, const char *tag){
    e->tags = (char**)realloc(e->tags, (e->tags_count+1)*sizeof(char*));
    e->tags[e->tags_count++] = dupstr(tag);
}

static component_t* entity_add_component(entity_t *e, const char *type){
    e->components = (component_t*)realloc(e->components, (e->components_count+1)*sizeof(component_t));
    component_t *c = &e->components[e->components_count++];
    memset(c, 0, sizeof *c);
    c->type_name = dupstr(type);
    return c;
}

// Build a component_value_t from a component instance using Flecs reflection
static component_value_t build_value_from_type_ptr_d(const ecs_world_t *world, ecs_entity_t type, const void *ptr, int depth);
static component_value_t build_struct_from_cursor_d(const ecs_world_t *world, ecs_entity_t type, ecs_meta_cursor_t *cur, int depth);
static component_value_t build_array_from_cursor_d(const ecs_world_t *world, ecs_entity_t type, ecs_meta_cursor_t *cur, int depth);
static component_value_t build_value_from_type_ptr(const ecs_world_t *world, ecs_entity_t type, const void *ptr) {
    return build_value_from_type_ptr_d(world, type, ptr, 0);
}

static component_value_t build_primitive_value(const ecs_world_t *world, ecs_entity_t type, const void *ptr);
static component_value_t build_struct_from_cursor_d(const ecs_world_t *world, ecs_entity_t type, ecs_meta_cursor_t *cur, int depth) {
    component_value_t out = {0};
    out.type = COMPONENT_TYPE_OBJECT;
    // Collect members first into dynamic arrays
    size_t cap = 8; size_t count = 0;
    struct { char *key; component_value_t *val; } *items = malloc(cap * sizeof(*items));
    if (!items) { out.type = COMPONENT_TYPE_NULL; return out; }

    // Enter struct scope
    if (ecs_meta_push(cur) != 0) {
        free(items); out.type = COMPONENT_TYPE_NULL; return out;
    }

    while (ecs_meta_next(cur) == 0) {
        const char *mname = ecs_meta_get_member(cur);
        ecs_entity_t mtype = ecs_meta_get_type(cur);

        component_value_t mv = {0};
        const EcsType *mtk = ecs_get(world, mtype, EcsType);
        if (mtk) {
            switch (mtk->kind) {
                case EcsPrimitiveType: {
                    void *mptr = ecs_meta_get_ptr(cur);
                    mv = build_primitive_value(world, mtype, mptr);
                } break;
                case EcsStructType: {
                    // Recurse within the same cursor scope
                    mv = build_struct_from_cursor_d(world, mtype, cur, depth + 1);
                } break;
                case EcsArrayType: {
                    mv = build_array_from_cursor_d(world, mtype, cur, depth + 1);
                } break;
                default: {
                    // Unhandled kinds -> null
                    mv.type = COMPONENT_TYPE_NULL;
                } break;
            }
        } else {
            // Not a typed struct/array, try primitive reflect data
            void *mptr = ecs_meta_get_ptr(cur);
            const EcsPrimitive *mpr = ecs_get(world, mtype, EcsPrimitive);
            if (mpr) mv = build_primitive_value(world, mtype, mptr);
            else mv.type = COMPONENT_TYPE_NULL;
        }

        if (count == cap) {
            cap *= 2;
            void *nb = realloc(items, cap * sizeof(*items));
            if (!nb) { break; }
            items = nb;
        }
        items[count].key = dupstr(mname ? mname : "");
        items[count].val = malloc(sizeof(component_value_t));
        if (items[count].val) {
            *items[count].val = mv;
        }
        count++;
    }
    (void)ecs_meta_pop(cur);

    out.object_val.count = count;
    out.object_val.items = (typeof(out.object_val.items))malloc(count * sizeof(*out.object_val.items));
    if (!out.object_val.items) {
        for (size_t i = 0; i < count; i++) free(items[i].key), free(items[i].val);
        free(items);
        out.type = COMPONENT_TYPE_NULL;
        return out;
    }
    for (size_t i = 0; i < count; i++) {
        out.object_val.items[i].key = items[i].key;
        out.object_val.items[i].value = items[i].val;
    }
    free(items);
    return out;
}

static component_value_t build_struct_from_cursor(const ecs_world_t *world, ecs_entity_t type, ecs_meta_cursor_t *cur) {
    return build_struct_from_cursor_d(world, type, cur, 0);
}

static component_value_t build_primitive_value(const ecs_world_t *world, ecs_entity_t type, const void *ptr) {
    component_value_t v = {0};
    const EcsPrimitive *pr = ecs_get(world, type, EcsPrimitive);
    if (!pr) { v.type = COMPONENT_TYPE_NULL; return v; }
    switch (pr->kind) {
        case EcsBool: v.type = COMPONENT_TYPE_BOOL; v.bool_val = *(const bool*)ptr; break;
        case EcsChar: v.type = COMPONENT_TYPE_INT; v.int_val = *(const char*)ptr; break;
        case EcsByte: v.type = COMPONENT_TYPE_INT; v.int_val = *(const unsigned char*)ptr; break;
        case EcsU8: v.type = COMPONENT_TYPE_INT; v.int_val = *(const uint8_t*)ptr; break;
        case EcsU16: v.type = COMPONENT_TYPE_INT; v.int_val = *(const uint16_t*)ptr; break;
        case EcsU32: v.type = COMPONENT_TYPE_INT; v.int_val = *(const uint32_t*)ptr; break;
        case EcsU64: v.type = COMPONENT_TYPE_INT; v.int_val = (int64_t)*(const uint64_t*)ptr; break;
        case EcsI8: v.type = COMPONENT_TYPE_INT; v.int_val = *(const int8_t*)ptr; break;
        case EcsI16: v.type = COMPONENT_TYPE_INT; v.int_val = *(const int16_t*)ptr; break;
        case EcsI32: v.type = COMPONENT_TYPE_INT; v.int_val = *(const int32_t*)ptr; break;
        case EcsI64: v.type = COMPONENT_TYPE_INT; v.int_val = *(const int64_t*)ptr; break;
        case EcsF32: v.type = COMPONENT_TYPE_FLOAT; v.float_val = *(const float*)ptr; break;
        case EcsF64: v.type = COMPONENT_TYPE_FLOAT; v.float_val = *(const double*)ptr; break;
        case EcsString: v.type = COMPONENT_TYPE_STRING; v.string_val = dupstr(*(char* const*)ptr); break;
        case EcsEntity: v.type = COMPONENT_TYPE_STRING; {
            ecs_entity_t e = *(const ecs_entity_t*)ptr; const char *n = ecs_get_name(world, e); v.string_val = dupstr(n?n:"");
        } break;
        case EcsId: v.type = COMPONENT_TYPE_STRING; v.string_val = dupstr("<id>"); break;
        default: v.type = COMPONENT_TYPE_NULL; break;
    }
    return v;
}

static component_value_t build_array_from_cursor_d(const ecs_world_t *world, ecs_entity_t type, ecs_meta_cursor_t *cur, int depth) {
    component_value_t out = {0};
    out.type = COMPONENT_TYPE_ARRAY;
    const EcsArray *ad = ecs_get(world, type, EcsArray);
    int32_t n = ad ? ad->count : 0;
    if (n < 0) n = 0;
    out.array_val.count = (size_t)n;
    out.array_val.values = n > 0 ? (component_value_t*)calloc((size_t)n, sizeof(component_value_t)) : NULL;
    if (ecs_meta_push(cur) != 0) {
        return out;
    }
    for (int32_t idx = 0; idx < n; idx++) {
        if (ecs_meta_elem(cur, idx) != 0) break;
        ecs_entity_t et = ecs_meta_get_type(cur);
        const EcsType *etk = ecs_get(world, et, EcsType);
        if (etk) {
            switch (etk->kind) {
                case EcsPrimitiveType: {
                    void *ep = ecs_meta_get_ptr(cur);
                    out.array_val.values[idx] = build_primitive_value(world, et, ep);
                } break;
                case EcsStructType: {
                    out.array_val.values[idx] = build_struct_from_cursor_d(world, et, cur, depth + 1);
                } break;
                case EcsArrayType: {
                    out.array_val.values[idx] = build_array_from_cursor_d(world, et, cur, depth + 1);
                } break;
                default: {
                    out.array_val.values[idx].type = COMPONENT_TYPE_NULL;
                } break;
            }
        } else {
            // Fallback to primitive
            const EcsPrimitive *pr = ecs_get(world, et, EcsPrimitive);
            if (pr) {
                void *ep = ecs_meta_get_ptr(cur);
                out.array_val.values[idx] = build_primitive_value(world, et, ep);
            } else {
                out.array_val.values[idx].type = COMPONENT_TYPE_NULL;
            }
        }
    }
    (void)ecs_meta_pop(cur);
    return out;
}

static component_value_t build_value_from_type_ptr_d(const ecs_world_t *world, ecs_entity_t type, const void *ptr, int depth) {
    component_value_t v = {0};

    if (depth > 64) {
        // Prevent runaway recursion on self-referential types
        v.type = COMPONENT_TYPE_STRING;
        v.string_val = dupstr("<max_depth>");
        return v;
    }

    const EcsType *tk = ecs_get(world, type, EcsType);
    if (!tk) {
        // Fallback: try primitive builtins like ecs_string_t, f32, i32
        const EcsPrimitive *pr = ecs_get(world, type, EcsPrimitive);
        if (pr) return build_primitive_value(world, type, ptr);
        v.type = COMPONENT_TYPE_NULL; return v;
    }
    switch (tk->kind) {
        case EcsPrimitiveType:
            return build_primitive_value(world, type, ptr);
        case EcsStructType: {
            const char *tname = ecs_get_name(world, type);
            (void)tname; // may be NULL
            ecs_meta_cursor_t cur = ecs_meta_cursor(world, type, (void*)ptr);
            return build_struct_from_cursor_d(world, type, &cur, depth + 1);
        }
        case EcsArrayType: {
            ecs_meta_cursor_t cur = ecs_meta_cursor(world, type, (void*)ptr);
            return build_array_from_cursor_d(world, type, &cur, depth + 1);
        }
        default:
            v.type = COMPONENT_TYPE_NULL; return v;
    }
}

scene_t* scene_from_world(ecs_world_t *world, const char *scene_name, const char *version){
    if (!world) return NULL;
    scene_t *scene = (scene_t*)calloc(1, sizeof(scene_t));
    if (!scene) return NULL;
    scene->metadata.name = dupstr(scene_name?scene_name:"WorldScene");
    scene->metadata.version = dupstr(version?version:"0.0.0");

    // Iterate all entities with any id, and skip builtin/module stuff by simple heuristics
    ecs_query_t *q = ecs_query(world, { .terms = { { .id = EcsAny } } });
    ecs_iter_t it = ecs_query_iter(world, q);

    ecs_entity_t name_id = ecs_lookup(world, "flecs.core.Identifier");
    ecs_entity_t prefab_tag = EcsPrefab;

    while (ecs_query_next(&it)) {
        for (int i = 0; i < it.count; i++) {
            ecs_entity_t e = it.entities[i];
            const char *name = ecs_get_name(world, e);
            if (!name || name[0] == '#') continue; // skip anon
            // Skip flecs-internal namespaces
            if (!strncmp(name, "flecs.", 6)) continue;

            // Make entity entry
            entity_t *se = scene_add_entity(scene, name);

            // Tags: will be read via JSON serialization, skip direct type inspection here

            // Populate components by introspecting component ids on the entity
            // Iterate over ids in the table by using entity_to_json to list component names, then fetch their data
            ecs_entity_to_json_desc_t jd = ECS_ENTITY_TO_JSON_INIT;
            jd.serialize_full_paths = true;
            jd.serialize_values = false; // we’ll fetch pointers directly for accuracy
            char *ejson = ecs_entity_to_json(world, e, &jd);
            if (ejson) {
                // Components
                const char *comp = strstr(ejson, "\"components\":{");
                if (comp) {
                    comp += strlen("\"components\":{");
                    const char *end = strchr(comp, '}');
                    if (end) {
                        const char *p = comp;
                        while (p < end) {
                            while (p < end && (*p==' '||*p=='\n'||*p=='\r'||*p=='\t'||*p==',')) p++;
                            if (p>=end || *p!='"') break;
                            const char *kstart = ++p;
                            while (p < end && *p!='"') p++;
                            if (p>=end) break;
                            char *type_name = strndup(kstart, (size_t)(p-kstart));
                            p++; // closing quote
                            // Include any component that has reflection metadata, but skip flecs internals
                            if (strncmp(type_name, "flecs.", 6) != 0 && strcmp(type_name, "Component") != 0) {
                                ecs_entity_t comp_id = ecs_lookup(world, type_name);
                                if (comp_id) {
                                    const EcsType *tk = ecs_get(world, comp_id, EcsType);
                                    const EcsPrimitive *pr = ecs_get(world, comp_id, EcsPrimitive);
                                    if (tk || pr) {
                                        const void *cptr = ecs_get_id(world, e, comp_id);
                                        if (cptr) {
                                            component_t *c = entity_add_component(se, type_name);
                                            c->data = build_value_from_type_ptr(world, comp_id, cptr);
                                        }
                                    }
                                }
                            }
                            free(type_name);
                            // advance to next
                            const char *comma = strchr(p, ',');
                            if (!comma || comma> end) break; else p = comma+1;
                        }
                    }
                }
                // Tags
                const char *tags = strstr(ejson, "\"tags\":[");
                if (tags) {
                    tags += strlen("\"tags\":[");
                    const char *te = strchr(tags, ']');
                    if (te) {
                        const char *p = tags;
                        while (p < te) {
                            while (p < te && (*p==' '||*p=='\n'||*p=='\r'||*p=='\t'||*p==',')) p++;
                            if (p>=te || *p!='"') break;
                            const char *k = ++p; while (p < te && *p!='"') p++; if (p>=te) break; char *tag = strndup(k, (size_t)(p-k)); p++;
                            if (strcmp(tag, "flecs.core.Prefab")!=0) entity_add_tag(se, tag);
                            free(tag);
                        }
                    }
                }
                // Prefab via pairs
                const char *pairs = strstr(ejson, "\"pairs\":{");
                if (pairs) {
                    const char *isa = strstr(pairs, "\"IsA\"");
                    if (isa) {
                        const char *col = strchr(isa, ':');
                        if (col) {
                            const char *p = col+1; while (*p==' '||*p=='"') p++;
                            const char *s = p; while (*p && *p!='"' && *p!='}' && *p!=',') p++;
                            char *pref = strndup(s, (size_t)(p-s));
                            if (pref && pref[0]) se->prefab = pref; else free(pref);
                        }
                    }
                }
                ecs_os_free(ejson);
            }

            // Parent-child relations
            ecs_entity_t parent = ecs_get_parent(world, e);
            if (parent) {
                const char *pname = ecs_get_name(world, parent);
                if (pname) {
                    scene->hierarchy_relations = (parent_child_relation_t*)realloc(scene->hierarchy_relations, (scene->hierarchy_relations_count+1)*sizeof(parent_child_relation_t));
                    parent_child_relation_t *rel = &scene->hierarchy_relations[scene->hierarchy_relations_count++];
                    rel->parent = dupstr(pname);
                    rel->child = dupstr(name);
                    rel->order = 0;
                }
            }
        }
    }
    ecs_query_fini(q);

    return scene;
}
