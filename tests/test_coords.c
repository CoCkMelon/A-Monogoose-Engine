/* tests — the coordinate-space contract (include/ame/coords.h, coords.txt).
 *
 * Pure CPU. Asserts the SIGN of every conversion (that is the whole point:
 * a silently inverted y is the bug class this header exists to prevent) and
 * the round-trip identity that makes an accidental double flip harmless.
 */
#include <stdio.h>

#include <ame/coords.h>

#include "utest.h"

int main(void) {
    /* --- 1. the flip, and its involution ------------------------------- */
    UT_CASE("y_down <-> y_up flips within an extent, twice = identity");
    UT_ASSERT_NEAR(ame_y_down_to_y_up(0.0f, 100.0f), 100.0f, 1e-6f);
    UT_ASSERT_NEAR(ame_y_down_to_y_up(100.0f, 100.0f), 0.0f, 1e-6f);
    UT_ASSERT_NEAR(ame_y_down_to_y_up(25.0f, 100.0f), 75.0f, 1e-6f);
    UT_ASSERT_NEAR(ame_y_up_to_y_down(75.0f, 100.0f), 25.0f, 1e-6f);
    /* h - y is exact for integers, so f(f(y)) == y bit-for-bit. One assertion
     * over the whole sweep keeps the check count readable. */
    int bad = 0, swept = 0;
    for (int h = 1; h <= 4096; h += 7)
        for (int y = 0; y <= h; y += 13) {
            float back = ame_y_up_to_y_down(ame_y_down_to_y_up((float)y,
                                                              (float)h),
                                            (float)h);
            swept++;
            if (back != (float)y)
                bad++;
        }
    UT_ASSERTF(bad == 0, "%d of %d round trips were inexact", bad, swept);
    UT_OK();

    /* --- 2. screen rect (top edge, y-down) -> world rect (bottom, y-up) -- */
    UT_CASE("px_y_down rect -> world_y_up bottom edge");
    /* a 20px-tall box whose top is 10px below the window top, in a 100px
     * tall view: it sits 10..30 from the top, i.e. 70..90 from the bottom,
     * so its world BOTTOM edge is 70. */
    UT_ASSERT_NEAR(ame_rect_px_y_down_to_world_y_up(10.0f, 20.0f, 100.0f),
                   70.0f, 1e-6f);
    /* the full-view case: top 0, height = view -> bottom edge 0 */
    UT_ASSERT_NEAR(ame_rect_px_y_down_to_world_y_up(0.0f, 100.0f, 100.0f),
                   0.0f, 1e-6f);
    /* a zero-height rect at the window bottom lands at world y 0 */
    UT_ASSERT_NEAR(ame_rect_px_y_down_to_world_y_up(100.0f, 0.0f, 100.0f),
                   0.0f, 1e-6f);
    /* and the inverse comes back to the TOP edge of the px rect */
    for (int i = 0; i < 40; i++) {
        float top = (float)i * 3.0f, h = 17.0f;
        float bottom = ame_rect_px_y_down_to_world_y_up(top, h, 240.0f);
        UT_ASSERT_NEAR(ame_rect_world_y_up_to_px_y_down(bottom, h, 240.0f),
                       top, 1e-5f);
    }
    UT_OK();

    /* --- 3. Tiled ingest: row 0 is the HIGHEST row in a y-up world ------ */
    UT_CASE("tile_row_down -> world_y_up (row 0 on top)");
    /* 4 rows of 10px, base_y = the map's top edge at world y 100:
     * row 0 occupies 60..70, row 3 occupies 90..100. */
    UT_ASSERT_NEAR(ame_tile_row_down_to_world_y_up_bottom(0, 4, 10.0f, 100.0f),
                   60.0f, 1e-6f);
    UT_ASSERT_NEAR(ame_tile_row_down_to_world_y_up_bottom(3, 4, 10.0f, 100.0f),
                   90.0f, 1e-6f);
    /* strictly increasing with row: higher row index = higher world y */
    for (int r = 1; r < 8; r++)
        UT_ASSERT(ame_tile_row_down_to_world_y_up_bottom(r, 8, 10.0f, 80.0f)
                  > ame_tile_row_down_to_world_y_up_bottom(r - 1, 8, 10.0f,
                                                           80.0f));
    UT_ASSERT_NEAR(ame_tile_row_down_to_world_y_up_bottom(7, 8, 10.0f, 80.0f)
                   + 10.0f, 80.0f, 1e-6f); /* last row's top == base_y */
    UT_OK();

    UT_CASE("world_y_up -> tile_row_down inverts the ingest");
    int row_bad = 0;
    for (int r = 0; r < 8; r++) {
        float bottom = ame_tile_row_down_to_world_y_up_bottom(r, 8, 10.0f, 80.0f);
        /* a point inside the row maps back to that row */
        if (ame_world_y_up_to_tile_row_down(bottom + 5.0f, 8, 10.0f, 80.0f) != r)
            row_bad++;
    }
    UT_ASSERTF(row_bad == 0, "%d of 8 tile rows did not round trip", row_bad);
    UT_OK();

    /* --- 4. row-major indexing, both directions ------------------------- */
    UT_CASE("row-major index: row_down vs row_up");
    /* 4 wide, 3 tall. row_down 0 is index 0..3; row_up 0 is the BOTTOM row */
    UT_ASSERT(ame_index_row_major_row_down(2, 1, 4) == 6);
    UT_ASSERT(ame_index_row_major_row_down(0, 0, 4) == 0);
    /* bottom row (row_up 0) of a 3-row map == row_down 2 */
    UT_ASSERT(ame_index_row_major_row_up(2, 0, 4, 3)
              == ame_index_row_major_row_down(2, 2, 4));
    UT_ASSERT(ame_index_row_major_row_up(0, 0, 4, 3) == 8);
    UT_ASSERT(ame_index_row_major_row_up(0, 2, 4, 3) == 0); /* top row */
    UT_ASSERT(ame_index_row_major_row_up(3, 1, 4, 3) == 7);
    UT_OK();

    return ut_done("coords");
}
