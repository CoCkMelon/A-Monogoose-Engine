/* test_prefab — name/prefix -> spawn callback registry (levels.txt MARKERS). */
#include <ame/prefab.h>

#include <string.h>

#include "utest.h"

static int g_calls;
static char g_last[64];

static ame_handle spawn_slime(void *user, const char *instance_name)
{
    (void)user;
    g_calls++;
    snprintf(g_last, sizeof g_last, "%s", instance_name);
    return (ame_handle){ .idx = 7, .gen = 1 };
}

static ame_handle spawn_col(void *user, const char *instance_name)
{
    (void)user;
    (void)instance_name;
    g_calls++;
    return (ame_handle){ .idx = 9, .gen = 2 };
}

static ame_handle spawn_k(void *user, const char *instance_name)
{
    (void)user;
    (void)instance_name;
    g_calls++;
    return (ame_handle){ .idx = 11, .gen = 3 };
}

static ame_handle spawn_exact(void *user, const char *instance_name)
{
    (void)user;
    (void)instance_name;
    g_calls++;
    return (ame_handle){ .idx = 42, .gen = 4 };
}

static ame_handle reject(void *user, const char *instance_name)
{
    (void)user;
    (void)instance_name;
    return AME_HANDLE_INVALID;
}

int main(void)
{
    printf("=== test_prefab (name -> spawn) ===\n");

    UT_CASE("prefix kinds resolve loaded marker names");
    {
        ame_prefab_reset();
        g_calls = 0;
        UT_ASSERT(ame_prefab_register_prefix("E_", spawn_slime, NULL) == 0);
        UT_ASSERT(ame_prefab_register_prefix("K_col_", spawn_col, NULL) == 0);
        UT_ASSERT(ame_prefab_register_prefix("K_", spawn_k, NULL) == 0);

        ame_handle h = ame_prefab_spawn("E_slime_01", "E_slime_01");
        UT_ASSERT(ame_handle_valid(h) && h.idx == 7);
        UT_ASSERT(strcmp(g_last, "E_slime_01") == 0);

        h = ame_prefab_spawn("K_col_wall", NULL);
        UT_ASSERTF(h.idx == 9, "longest prefix K_col_ must win, got idx %u",
                   (unsigned)h.idx);
        h = ame_prefab_spawn("K_other", NULL);
        UT_ASSERT(h.idx == 11);
        UT_ASSERT(g_calls == 3);
        UT_OK();
    }

    UT_CASE("exact beats prefix; unknown returns INVALID");
    {
        UT_ASSERT(ame_prefab_register("E_boss", spawn_exact, NULL) == 0);
        ame_handle h = ame_prefab_spawn("E_boss", "E_boss");
        UT_ASSERT(h.idx == 42);
        h = ame_prefab_spawn("E_slime_02", "E_slime_02");
        UT_ASSERT(h.idx == 7);
        UT_ASSERT(!ame_handle_valid(ame_prefab_spawn("Z_nothing", "x")));
        UT_ASSERT(ame_prefab_match("Z_nothing") == NULL);
        UT_ASSERT(strcmp(ame_prefab_match("K_col_floor"), "K_col_") == 0);
        UT_ASSERT(strcmp(ame_prefab_match("E_boss"), "E_boss") == 0);
        UT_OK();
    }

    UT_CASE("a rejecting builder yields INVALID");
    {
        UT_ASSERT(ame_prefab_register_prefix("T_", reject, NULL) == 0);
        UT_ASSERT(!ame_handle_valid(ame_prefab_spawn("T_door_keyred", NULL)));
        UT_OK();
    }

    UT_CASE("bad args and duplicates fail; reset clears");
    {
        UT_ASSERT(ame_prefab_register(NULL, spawn_slime, NULL) == -1);
        UT_ASSERT(ame_prefab_register("", spawn_slime, NULL) == -1);
        UT_ASSERT(ame_prefab_register("dup", NULL, NULL) == -1);
        UT_ASSERT(ame_prefab_register("dup", spawn_slime, NULL) == 0);
        UT_ASSERT(ame_prefab_register("dup", spawn_slime, NULL) == -1);
        /* same key as exact AND prefix is allowed (different tables) */
        UT_ASSERT(ame_prefab_register_prefix("dup", spawn_col, NULL) == 0);
        ame_prefab_reset();
        UT_ASSERT(ame_prefab_match("dup") == NULL);
        UT_ASSERT(!ame_handle_valid(ame_prefab_spawn("E_slime_01", NULL)));
        UT_OK();
    }

    UT_CASE("registry capacity is bounded");
    {
        ame_prefab_reset();
        static char keys[100][8]; /* keys are borrowed: must outlive reg */
        int ok = 0;
        for (int i = 0; i < 100; i++) {
            snprintf(keys[i], sizeof keys[i], "k%d", i);
            if (ame_prefab_register(keys[i], spawn_slime, NULL) == 0)
                ok++;
        }
        UT_ASSERTF(ok == 64, "capacity 64, registered %d", ok);
        ame_prefab_reset();
        UT_OK();
    }

    return ut_done("test_prefab");
}
