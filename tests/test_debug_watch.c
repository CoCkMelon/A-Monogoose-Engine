/* test_debug_watch.c - registry semantics of the v1 watch overlay.
 * Drawing itself is the headless screenshot smoke (biscuit HUD); layout is
 * CPU-only but needs the font, so keep this test GL/text-free. */
#include "ame/debug.h"
#include "utest.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    UT_CASE("register and read back");
    {
        static float fuel = 42.5f;
        static int fps = 60;
        ame_debug_watch_clear();
        int a = ame_debug_watch("fuel", &fuel);
        int b = ame_debug_watch_int("fps", &fps);
        UT_ASSERT(a >= 0 && b >= 0 && a != b);
        UT_ASSERT(ame_debug_watch_count() == 2);
        UT_ASSERT(ame_debug_watch_name(a) != NULL);
        UT_ASSERT(strcmp(ame_debug_watch_name(a), "fuel") == 0);
        UT_ASSERT(strcmp(ame_debug_watch_name(b), "fps") == 0);
        UT_OK();
    }

    UT_CASE("same name replaces the slot");
    {
        static float v1 = 1.0f, v2 = 2.0f;
        ame_debug_watch_clear();
        int a = ame_debug_watch("car_x", &v1);
        int b = ame_debug_watch("car_x", &v2);
        UT_ASSERT(a == b && ame_debug_watch_count() == 1);
        UT_OK();
    }

    UT_CASE("capacity is bounded and reported");
    {
        static float vals[AME_DEBUG_WATCH_MAX + 4];
        char name[16];
        ame_debug_watch_clear();
        int ok = 0;
        for (int k = 0; k < AME_DEBUG_WATCH_MAX + 4; k++) {
            snprintf(name, sizeof name, "w%d", k);
            if (ame_debug_watch(name, &vals[k]) >= 0)
                ok++;
        }
        UT_ASSERTF(ok == AME_DEBUG_WATCH_MAX, "kept %d != %d", ok,
                   AME_DEBUG_WATCH_MAX);
        UT_ASSERT(ame_debug_watch_count() == AME_DEBUG_WATCH_MAX);
        UT_ASSERT(ame_debug_watch_name(-1) == NULL);
        UT_ASSERT(ame_debug_watch_name(AME_DEBUG_WATCH_MAX) == NULL);
        UT_OK();
    }

    UT_CASE("bad input rejected");
    {
        static float v = 1.0f;
        ame_debug_watch_clear();
        UT_ASSERT(ame_debug_watch(NULL, &v) < 0);
        UT_ASSERT(ame_debug_watch("", &v) < 0);
        UT_ASSERT(ame_debug_watch("x", NULL) < 0);
        UT_ASSERT(ame_debug_watch_count() == 0);
        UT_OK();
    }

    return ut_done("test_debug_watch");
}
