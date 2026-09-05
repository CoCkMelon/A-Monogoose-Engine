#include "ame/prefab.h"

#include <string.h>

enum { AME_PREFAB_MAX = 32 };

typedef struct Prefab {
    const char *name;
    ame_prefab_fn fn;
    void *user;
} Prefab;

static Prefab g_reg[AME_PREFAB_MAX];
static int g_n;

void ame_prefab_registry_reset(void)
{
    g_n = 0;
    memset(g_reg, 0, sizeof(g_reg));
}

int ame_prefab_register(const char *name, ame_prefab_fn fn, void *user)
{
    if (!name || !name[0] || !fn || g_n >= AME_PREFAB_MAX) return 0;
    for (int i = 0; i < g_n; i++)
        if (g_reg[i].name && strcmp(g_reg[i].name, name) == 0)
            return 0;
    g_reg[g_n].name = name;
    g_reg[g_n].fn = fn;
    g_reg[g_n].user = user;
    g_n++;
    return 1;
}

ame_handle ame_prefab_instantiate(const char *name, const char *instance_name)
{
    if (!name) return AME_HANDLE_INVALID;
    for (int i = 0; i < g_n; i++) {
        if (g_reg[i].name && strcmp(g_reg[i].name, name) == 0)
            return g_reg[i].fn(g_reg[i].user, instance_name);
    }
    return AME_HANDLE_INVALID;
}
