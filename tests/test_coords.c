#include "ame/coords.h"
#include "ame/log.h"

#include <stdio.h>

static int fail(const char *m)
{
    fprintf(stderr, "FAIL coords: %s\n", m);
    return 1;
}

int main(void)
{
    if (ame_flip_y_index_top_to_bottom(0, 4) != 3) return fail("flip top");
    if (ame_flip_y_index_top_to_bottom(3, 4) != 0) return fail("flip bot");
    if (ame_linear_index_rowmajor_bottom_left(2, 1, 8) != 10) return fail("idx bl");
    if (ame_linear_index_rowmajor_top_to_bottom(0, 0, 4, 4) != 12) return fail("idx tl");
    float x, y;
    ame_tile_index_top_left_to_world_center(0, 0, 2, 10.0f, 10.0f, &x, &y);
    if (x < 4.9f || x > 5.1f) return fail("world x");
    if (y < 14.9f || y > 15.1f) return fail("world y"); /* y_bottom = 1, center 15 */
    int tx, ty;
    ame_world_center_to_tile_index_bottom_left(15.0f, 25.0f, 10.0f, 10.0f, &tx, &ty);
    if (tx != 1 || ty != 2) return fail("world to tile");
    LOGD("coords debug-only\n");
    printf("test_coords ok\n");
    return 0;
}
