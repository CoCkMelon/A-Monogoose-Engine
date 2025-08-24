#ifndef AME_COORDS_H
#define AME_COORDS_H

#ifdef __cplusplus
extern "C" {
#endif

// Canonical project coordinate system
// - World space uses pixels
// - Origin (0,0) is at bottom-left of the world/map
// - X increases to the right, Y increases upward (Y-up)
// - Object/tile positions are typically at their centers (center-based)
//
// Many external sources use different conventions:
// - TMX (Tiled) layer data indexes rows from the TOP (Y-down)
// - Window/screen pixels are typically top-left origin (Y-down)
// - GPU tilemap shader may operate in top-left camera space for efficiency
//
// This header provides small, explicit helpers to convert between those spaces
// and the canonical world Y-up bottom-left center-based coordinate system.

#include <stdint.h>

// Flip a row index from top-left origin to bottom-left origin
// y_top:  row index counted from top (0 = top row)
// h:      number of rows
// returns y_bottom (0 = bottom row)
static inline int ame_flip_y_index_top_to_bottom(int y_top, int h) {
    return (h - 1) - y_top;
}

// Compute linear row-major index for x,y (y counted from bottom)
static inline int ame_linear_index_rowmajor_bottom_left(int x, int y_bottom, int w) {
    return y_bottom * w + x;
}

// Compute linear row-major index for (x, y_top) where y is counted from the TOP.
// This converts to bottom-left indexing internally and returns the linear index.
static inline int ame_linear_index_rowmajor_top_to_bottom(int x, int y_top, int w, int h) {
    int y_bottom = ame_flip_y_index_top_to_bottom(y_top, h);
    return ame_linear_index_rowmajor_bottom_left(x, y_bottom, w);
}

// Convert tile index (x, y) where y is counted from BOTTOM (0 = bottom row)
// into world-space center position in pixels (canonical Y-up bottom-left center).
static inline void ame_tile_index_bottom_left_to_world_center(int x, int y_bottom,
                                                             float tile_w, float tile_h,
                                                             float* out_x, float* out_y) {
    if (out_x) *out_x = ((float)x + 0.5f) * tile_w;
    if (out_y) *out_y = ((float)y_bottom + 0.5f) * tile_h;
}

// Convert tile index (x, y) where y is counted from TOP (TMX style)
// into world-space center position in pixels (canonical Y-up bottom-left center).
static inline void ame_tile_index_top_left_to_world_center(int x, int y_top,
                                                           int map_h_tiles,
                                                           float tile_w, float tile_h,
                                                           float* out_x, float* out_y) {
    int y_bottom = ame_flip_y_index_top_to_bottom(y_top, map_h_tiles);
    ame_tile_index_bottom_left_to_world_center(x, y_bottom, tile_w, tile_h, out_x, out_y);
}

// Convert world pixel center to bottom-left tile indices (integer floor).
static inline void ame_world_center_to_tile_index_bottom_left(float world_x, float world_y,
                                                              float tile_w, float tile_h,
                                                              int* out_x, int* out_y_bottom) {
    if (out_x) *out_x = (int)(world_x / tile_w);
    if (out_y_bottom) *out_y_bottom = (int)(world_y / tile_h);
}

#ifdef __cplusplus
}
#endif

#endif // AME_COORDS_H

