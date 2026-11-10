#include <ame/prefab.h>

#include <string.h>

enum { AME_PREFAB_MAX = 64 };

typedef struct {
    const char *key;
    ame_prefab_fn fn;
    void *user;
    int is_prefix;
} ame_prefab_entry;

static ame_prefab_entry g_reg[AME_PREFAB_MAX];
static int g_n;

void ame_prefab_reset(void)
{
    g_n = 0;
    memset(g_reg, 0, sizeof g_reg);
}

static int register_key(const char *key, ame_prefab_fn fn, void *user,
                        int is_prefix)
{
    if (!key || !key[0] || !fn || g_n >= AME_PREFAB_MAX) {
        ame_set_error(AME_ERR_INVALID_ARGUMENT, "prefab register");
        return -1;
    }
    for (int i = 0; i < g_n; i++)
        if (g_reg[i].key && g_reg[i].is_prefix == is_prefix
            && strcmp(g_reg[i].key, key) == 0) {
            ame_set_errorf(AME_ERR_INVALID_ARGUMENT, "prefab '%s' twice", key);
            return -1;
        }
    g_reg[g_n].key = key;
    g_reg[g_n].fn = fn;
    g_reg[g_n].user = user;
    g_reg[g_n].is_prefix = is_prefix;
    g_n++;
    return 0;
}

int ame_prefab_register(const char *name, ame_prefab_fn fn, void *user)
{
    return register_key(name, fn, user, 0);
}

int ame_prefab_register_prefix(const char *prefix, ame_prefab_fn fn,
                               void *user)
{
    return register_key(prefix, fn, user, 1);
}

static const ame_prefab_entry *find(const char *object_name)
{
    if (!object_name)
        return NULL;
    for (int i = 0; i < g_n; i++) /* exact beats every prefix */
        if (g_reg[i].key && !g_reg[i].is_prefix
            && strcmp(g_reg[i].key, object_name) == 0)
            return &g_reg[i];
    const ame_prefab_entry *best = NULL;
    size_t best_len = 0;
    for (int i = 0; i < g_n; i++) {
        if (!g_reg[i].key || !g_reg[i].is_prefix)
            continue;
        size_t kl = strlen(g_reg[i].key);
        if (kl > best_len && strncmp(object_name, g_reg[i].key, kl) == 0) {
            best = &g_reg[i];
            best_len = kl;
        }
    }
    return best;
}

const char *ame_prefab_match(const char *object_name)
{
    const ame_prefab_entry *e = find(object_name);
    return e ? e->key : NULL;
}

ame_handle ame_prefab_spawn(const char *object_name, const char *instance_name)
{
    const ame_prefab_entry *e = find(object_name);
    if (!e)
        return AME_HANDLE_INVALID;
    return e->fn(e->user, instance_name ? instance_name : object_name);
}
