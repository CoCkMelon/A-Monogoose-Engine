#include "ame/prefab.h"
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL_mutex.h>

// Simple linear registry. Can be replaced with a hashmap later.
typedef struct PrefabEntry {
    char *name;
    AmePrefabBuilder builder;
    void *ud;
} PrefabEntry;

static PrefabEntry *g_prefabs = NULL;
static size_t g_prefab_count = 0;
static SDL_Mutex* g_prefab_mutex = NULL;

static SDL_Mutex* get_prefab_mutex(void) {
    if (!g_prefab_mutex) {
        g_prefab_mutex = SDL_CreateMutex();
    }
    return g_prefab_mutex;
}

int ame_prefab_register(const char* name, AmePrefabBuilder builder, void* user_data){
    if (!name || !builder) return 0;
    SDL_Mutex* m = get_prefab_mutex();
    SDL_LockMutex(m);
    // Check duplicate
    for (size_t i = 0; i < g_prefab_count; ++i){
        if (strcmp(g_prefabs[i].name, name) == 0) {
            SDL_UnlockMutex(m);
            return 0; // already exists
        }
    }
    PrefabEntry *nb = (PrefabEntry*)realloc(g_prefabs, (g_prefab_count+1)*sizeof(PrefabEntry));
    if (!nb) {
        SDL_UnlockMutex(m);
        return 0;
    }
    g_prefabs = nb;
    g_prefabs[g_prefab_count].name = strdup(name);
    g_prefabs[g_prefab_count].builder = builder;
    g_prefabs[g_prefab_count].ud = user_data;
    g_prefab_count++;
    SDL_UnlockMutex(m);
    return 1;
}

AmeEntity ame_prefab_instantiate(AmeScene* scene, const char* prefab_name, AmeEntity parent, const char* instance_name){
    if (!scene || !prefab_name) return 0;
    SDL_Mutex* m = get_prefab_mutex();
    SDL_LockMutex(m);
    for (size_t i = 0; i < g_prefab_count; ++i){
        if (strcmp(g_prefabs[i].name, prefab_name) == 0){
            AmePrefabBuilder builder = g_prefabs[i].builder;
            void* ud = g_prefabs[i].ud;
            SDL_UnlockMutex(m);
            return builder(scene, parent, instance_name, ud);
        }
    }
    SDL_UnlockMutex(m);
    return 0; // not found
}
