#include "ame/prefab.h"
#include <stdlib.h>
#include <string.h>

// Simple linear registry. Can be replaced with a hashmap later.
typedef struct PrefabEntry {
    char *name;
    AmePrefabBuilder builder;
    void *ud;
} PrefabEntry;

static PrefabEntry *g_prefabs = NULL;
static size_t g_prefab_count = 0;

int ame_prefab_register(const char* name, AmePrefabBuilder builder, void* user_data){
    if (!name || !builder) return 0;
    for (size_t i = 0; i < g_prefab_count; ++i){
        if (strcmp(g_prefabs[i].name, name) == 0) return 0; // already exists
    }
    PrefabEntry *nb = (PrefabEntry*)realloc(g_prefabs, (g_prefab_count+1)*sizeof(PrefabEntry));
    if (!nb) return 0;
    g_prefabs = nb;
    g_prefabs[g_prefab_count].name = strdup(name);
    g_prefabs[g_prefab_count].builder = builder;
    g_prefabs[g_prefab_count].ud = user_data;
    g_prefab_count++;
    return 1;
}

AmeEntity ame_prefab_instantiate(AmeScene* scene, const char* prefab_name, AmeEntity parent, const char* instance_name){
    if (!scene || !prefab_name) return 0;
    for (size_t i = 0; i < g_prefab_count; ++i){
        if (strcmp(g_prefabs[i].name, prefab_name) == 0){
            return g_prefabs[i].builder(scene, parent, instance_name, g_prefabs[i].ud);
        }
    }
    return 0; // not found
}
