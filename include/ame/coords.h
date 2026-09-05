#ifndef AME_COORDS_H
#define AME_COORDS_H

/*
 * Canonical project coordinate system (mongoose coords.h).
 * - World: origin bottom-left, X right, Y up (center-based positions).
 * - TMX/Tiled layer rows are top-down; window pixels are top-left Y-down.
 * Helpers convert those spaces into world Y-up.
 *
 * Biscuit/Memory use world units (not pixels); the index math is the same.
 */

static inline int ame_flip_y_index_top_to_bottom(int y_top, int h)
{
    return (h - 1) - y_top;
}

static inline int ame_linear_index_rowmajor_bottom_left(int x, int y_bottom, int w)
{
    return y_bottom * w + x;
}

static inline int ame_linear_index_rowmajor_top_to_bottom(int x, int y_top, int w, int h)
{
    int y_bottom = ame_flip_y_index_top_to_bottom(y_top, h);
    return ame_linear_index_rowmajor_bottom_left(x, y_bottom, w);
}

static inline void ame_tile_index_bottom_left_to_world_center(int x, int y_bottom,
                                                             float tile_w, float tile_h,
                                                             float *out_x, float *out_y)
{
    if (out_x) *out_x = ((float)x + 0.5f) * tile_w;
    if (out_y) *out_y = ((float)y_bottom + 0.5f) * tile_h;
}

static inline void ame_tile_index_top_left_to_world_center(int x, int y_top,
                                                           int map_h_tiles,
                                                           float tile_w, float tile_h,
                                                           float *out_x, float *out_y)
{
    int y_bottom = ame_flip_y_index_top_to_bottom(y_top, map_h_tiles);
    ame_tile_index_bottom_left_to_world_center(x, y_bottom, tile_w, tile_h, out_x, out_y);
}

static inline void ame_world_center_to_tile_index_bottom_left(float world_x, float world_y,
                                                              float tile_w, float tile_h,
                                                              int *out_x, int *out_y_bottom)
{
    if (out_x) *out_x = (int)(world_x / tile_w);
    if (out_y_bottom) *out_y_bottom = (int)(world_y / tile_h);
}

#endif
