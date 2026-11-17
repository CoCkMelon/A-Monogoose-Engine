/* ame-next — the project's coordinate spaces, named (coords.txt).
 *
 * FOUR SPACES. Every engine function that takes or returns a coordinate says
 * which one in its NAME; this header is the only place conversions live.
 *
 *   px_y_down      window/screen pixels, origin = window TOP-LEFT, +y DOWN.
 *                  1 unit = 1 window px. Input (in_mouse_pos), text_atlas
 *                  layout, and every screen-space UI call use this.
 *   world_y_up     2D WORLD space. +x right, +y UP, origin arbitrary.
 *                  1 unit = 1 world px at zoom 1. Gameplay, physics, tile
 *                  placement, sprite/rect placement. Gravity is -y.
 *   tile_row_down  Tiled .tmj layer rows as authored: row 0 = TOP row,
 *                  index = row * width + col (right-down).
 *   mesh_y_up      3D/model space: right-handed, +y up, camera looks down -z.
 *                  OBJ vertices, meshes, the perspective camera.
 *
 * RULES (why this header exists)
 *   1. Content ingest converts ONCE at load (tile_row_down -> world_y_up).
 *   2. Screen <-> world conversion happens in the camera/render layer only
 *      (camera_screen_y_down_to_world_y_up, rp_push_sprite_screen) and never
 *      in gameplay code.
 *   3. Game code picks a space and STAYS in it: gameplay math is world_y_up,
 *      HUD/editor/UI math is px_y_down. No flipping inside a frame loop.
 *   4. A function that converts names BOTH spaces it bridges.
 *
 * The flips below are exact for integer inputs (h - y is a mirror about the
 * view centre), which is what makes a double conversion harmless: the
 * round-trip test asserts f(f(y)) == y bit-for-bit for integer extents.
 */
#ifndef AME_COORDS_H
#define AME_COORDS_H

#ifdef __cplusplus
extern "C" {
#endif

/* --- the one flip: within a space of vertical extent `extent` ------------
 * `extent` is the y-extent the two spaces share (view height in px for
 * px_y_down <-> world_y_up, map height in tiles for row indices). */
static inline float ame_y_down_to_y_up(float y_down, float extent) {
    return extent - y_down;
}

static inline float ame_y_up_to_y_down(float y_up, float extent) {
    return extent - y_up;
}

/* an ORIGIN (top edge) in px_y_down -> the BOTTOM edge of the same rect in
 * world_y_up. This is the conversion a screen-space rect needs before it is
 * pushed as a world sprite, whose anchor is the lower-left corner. */
static inline float ame_rect_px_y_down_to_world_y_up(float y_top_px,
                                                     float h,
                                                     float view_h_px) {
    return (view_h_px - y_top_px) - h;
}

/* inverse: world_y_up lower-left + h -> px_y_down TOP edge */
static inline float ame_rect_world_y_up_to_px_y_down(float y_bottom_world,
                                                     float h,
                                                     float view_h_px) {
    return (view_h_px - (y_bottom_world + h));
}

/* --- Tiled rows -> world (ingest, once at load) -------------------------
 * Row 0 is the TOP row as authored; in a y-up world it is the HIGHEST. Row
 * `row` occupies world y [bottom, bottom + tile_h). */
static inline float ame_tile_row_down_to_world_y_up_bottom(int row, int rows,
                                                           float tile_h,
                                                           float base_y) {
    return base_y - (float)(rows - row) * tile_h;
}

/* inverse: world y of a point -> the y-DOWN row index containing it.
 * Rows count DOWN from base_y, so the row index is (rows - 1) minus how many
 * whole tile rows lie between the point and the map's top edge. */
static inline int ame_world_y_up_to_tile_row_down(float world_y, int rows,
                                                  float tile_h, float base_y) {
    int from_top = (int)((base_y - world_y) / tile_h);
    return (rows - 1) - from_top;
}

/* --- row-major index math, both row directions ------------------------- */
static inline int ame_index_row_major_row_down(int x, int row_down, int w) {
    return row_down * w + x; /* identical to Tiled's own indexing */
}

static inline int ame_index_row_major_row_up(int x, int row_up, int w, int h) {
    return ame_index_row_major_row_down(x, (h - 1) - row_up, w);
}

#ifdef __cplusplus
}
#endif

#endif /* AME_COORDS_H */
