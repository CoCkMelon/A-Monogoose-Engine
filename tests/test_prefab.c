#include "ame/prefab.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *m)
{
    fprintf(stderr, "FAIL prefab: %s\n", m);
    return 1;
}

static char g_name[32];

static ame_handle spawn_enemy(void *user, const char *instance)
{
    int *n = (int *)user;
    (*n)++;
    if (instance) {
        strncpy(g_name, instance, 31);
        g_name[31] = 0;
    }
    return ame_handle_make(3, 2);
}

int main(void)
{
    int n = 0;
    ame_prefab_registry_reset();
    if (!ame_prefab_register("Enemy", spawn_enemy, &n)) return fail("reg");
    if (ame_prefab_register("Enemy", spawn_enemy, &n)) return fail("dup");
    ame_handle h = ame_prefab_instantiate("Enemy", "bob");
    if (ame_handle_index(h) != 3 || ame_handle_generation(h) != 2)
        return fail("handle");
    if (n != 1) return fail("count");
    if (strcmp(g_name, "bob") != 0) return fail("name");
    if (ame_prefab_instantiate("Nope", NULL) != AME_HANDLE_INVALID)
        return fail("missing");
    printf("test_prefab ok\n");
    return 0;
}
