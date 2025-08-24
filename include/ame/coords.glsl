// ame/coords.glsl - Shared GLSL helpers for canonical coordinates
// Canonical: world pixels, origin at bottom-left, Y-up, center-based positions.

#ifndef AME_COORDS_GLSL
#define AME_COORDS_GLSL

// Flip a row index from top-left origin to bottom-left origin
int ame_flip_y_index_top_to_bottom_gl(int y_top, int h) {
    return (h - 1) - y_top;
}

// Convert world pixel coordinate to bottom-left tile index (integer floor)
ivec2 ame_world_px_to_tile_index_bottom_left(vec2 world_px, ivec2 tile_size) {
    return ivec2(floor(world_px / vec2(tile_size)));
}

// Compute in-tile pixel coordinate (0..tile_size-1) from world pixel position
ivec2 ame_world_px_to_in_tile_px(vec2 world_px, ivec2 tile_size) {
    vec2 tile_frac = fract(world_px / vec2(tile_size));
    return ivec2(tile_frac * vec2(tile_size));
}

#endif // AME_COORDS_GLSL

