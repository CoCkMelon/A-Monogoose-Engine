#include "ame/scene_builder.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdalign.h>

#include <flecs.h>

// Internal data structures for the in-memory scene descriptor

typedef struct TagList {
    char **items;
    size_t count;
} TagList;

typedef struct Node {
    char *name;
    int enabled;
    TagList tags;
    int has_transform;
    AmeTransform2D transform;
    int has_camera;
    AmeCamera camera;

    // hierarchy
    uint64_t parent; // 0 if none; indices are 1-based for simplicity
} Node;

struct AmeScene {
    char *name;
    char *version;
    char *author;
    char *description;

    Node *nodes; // 1-based index: nodes[0] unused
    size_t count;
    size_t cap;
};

static uint64_t push_node(AmeScene *s, const char *name){
    if (!s) return 0;
    if (s->count + 1 >= s->cap){
        size_t ncap = s->cap ? s->cap * 2 : 16;
        Node *nb = (Node*)realloc(s->nodes, ncap * sizeof(Node));
        if (!nb) return 0;
        // zero new memory
        for (size_t i = s->cap; i < ncap; ++i) memset(&nb[i], 0, sizeof(Node));
        s->nodes = nb; s->cap = ncap;
    }
    size_t idx = ++s->count;
    Node *n = &s->nodes[idx];
    memset(n, 0, sizeof *n);
    n->name = name ? strdup(name) : NULL;
    n->enabled = 1;
    n->parent = 0;
    return (uint64_t)idx;
}

static void taglist_add(TagList *tl, const char *tag){
    if (!tag) return;
    char **nb = (char**)realloc(tl->items, (tl->count+1)*sizeof(char*));
    if (!nb) return; tl->items = nb; tl->items[tl->count++] = strdup(tag);
}

// Public API

AmeScene* ame_scene_create(const char* name, const char* version){
    AmeScene *s = (AmeScene*)calloc(1, sizeof(AmeScene));
    if (!s) return NULL;
    s->name = name ? strdup(name) : NULL;
    s->version = version ? strdup(version) : NULL;
    return s;
}

void ame_scene_destroy(AmeScene* scene){
    if (!scene) return;
    for (size_t i = 1; i <= scene->count; ++i){
        Node *n = &scene->nodes[i];
        free(n->name);
        for (size_t t = 0; t < n->tags.count; ++t) free(n->tags.items[t]);
        free(n->tags.items);
    }
    free(scene->nodes);
    free(scene->name);
    free(scene->version);
    free(scene->author);
    free(scene->description);
    free(scene);
}

AmeEntity ame_scene_add_entity(AmeScene* scene, const char* name){
    return push_node(scene, name);
}

void ame_scene_entity_set_enabled(AmeScene* scene, AmeEntity e, bool enabled){
    if (!scene || e==0 || e>scene->count) return;
    scene->nodes[e].enabled = enabled ? 1 : 0;
}

void ame_scene_entity_add_tag(AmeScene* scene, AmeEntity e, const char* tag){
    if (!scene || e==0 || e>scene->count) return;
    taglist_add(&scene->nodes[e].tags, tag);
}

void ame_scene_entity_set_transform(AmeScene* scene, AmeEntity e, AmeTransform2D tr){
    if (!scene || e==0 || e>scene->count) return;
    scene->nodes[e].has_transform = 1;
    scene->nodes[e].transform = tr;
}

void ame_scene_entity_set_camera(AmeScene* scene, AmeEntity e, const AmeCamera* cam){
    if (!scene || !cam || e==0 || e>scene->count) return;
    scene->nodes[e].has_camera = 1;
    scene->nodes[e].camera = *cam;
}

void ame_scene_set_parent(AmeScene* scene, AmeEntity child, AmeEntity parent){
    if (!scene || child==0 || child>scene->count) return;
    scene->nodes[child].parent = parent;
}

void ame_scene_set_author(AmeScene* scene, const char* author){
    if (!scene) return; if (scene->author) free(scene->author); scene->author = author?strdup(author):NULL;
}
void ame_scene_set_description(AmeScene* scene, const char* description){
    if (!scene) return; if (scene->description) free(scene->description); scene->description = description?strdup(description):NULL;
}

// Helpers to obtain component ids without exposing C++ façade. We register components by name directly in Flecs.
static ecs_entity_t ensure_component(ecs_world_t *w, const char *name, int32_t size, int32_t align){
    ecs_entity_desc_t ed = {0}; ed.name = name;
    ecs_component_desc_t cd = {0}; cd.entity = ecs_entity_init(w, &ed); cd.type.size = size; cd.type.alignment = align;
    return ecs_component_init(w, &cd);
}

bool ame_scene_instantiate_to_world(const AmeScene* scene, struct ecs_world_t* world){
    if (!scene || !world) return false;

    // Ensure components exist (AmeTransform2D, AmeCamera) by name
    ecs_entity_t c_tr = ensure_component(world, "AmeTransform2D", (int32_t)sizeof(AmeTransform2D), (int32_t)(_Alignof(AmeTransform2D)));
    ecs_entity_t c_cam = ensure_component(world, "AmeCamera", (int32_t)sizeof(AmeCamera), (int32_t)(_Alignof(AmeCamera)));

    // Pass 1: create entities and set names
    ecs_entity_t *emap = (ecs_entity_t*)calloc(scene->count + 1, sizeof(ecs_entity_t));
    if (!emap) return false;
    for (size_t i = 1; i <= scene->count; ++i){
        const Node *n = &scene->nodes[i];
        ecs_entity_desc_t ed = {0};
        ed.name = n->name;
        ecs_entity_t e = ecs_entity_init(world, &ed);
        emap[i] = e;
        if (!n->enabled) ecs_add_id(world, e, EcsDisabled);
        // tags as simple identifiers: add a Tag component by name as empty tags (optional)
        for (size_t t = 0; t < n->tags.count; ++t){
            // We create an empty tag entity and add it
            ecs_entity_t tag_e = ecs_lookup(world, n->tags.items[t]);
            if (!tag_e){ ecs_entity_desc_t ted = {0}; ted.name = n->tags.items[t]; tag_e = ecs_entity_init(world, &ted); }
            ecs_add_id(world, e, tag_e);
        }
    }

    // Pass 2: set components
    for (size_t i = 1; i <= scene->count; ++i){
        const Node *n = &scene->nodes[i]; ecs_entity_t e = emap[i];
        if (n->has_transform){ ecs_set_id(world, e, c_tr, sizeof(AmeTransform2D), &n->transform); }
        if (n->has_camera){ ecs_set_id(world, e, c_cam, sizeof(AmeCamera), &n->camera); }
    }

    // Pass 3: hierarchy relations
    for (size_t i = 1; i <= scene->count; ++i){
        const Node *n = &scene->nodes[i]; if (n->parent){ ecs_add_pair(world, emap[i], EcsChildOf, emap[n->parent]); }
    }

    free(emap);
    return true;
}
