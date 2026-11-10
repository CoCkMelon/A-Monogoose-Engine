/* ame/debug.h - minimal live-variable debug overlay (trunk text stack).
 *
 * The full inspector is deliberately NOT here: this is the hand-registered
 * watch list agreed for v1 - a game pins floats by pointer and the overlay
 * prints "name: value" lines through the one trunk text path. No reflection,
 * no ECS, no per-frame allocation; the registry is a flat static table.
 *
 * C-callable, SDL/GL-free. Drawing submits glyph quads to the render batch
 * in SCREEN space (same contract as text_draw_screen: x right, y down).
 */
#ifndef AME_DEBUG_H
#define AME_DEBUG_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AME_DEBUG_WATCH_MAX 32
#define AME_DEBUG_NAME_MAX 24

/* Register (or replace, keyed by name) a watched float. The pointer must
 * outlive the draws; games typically point into a static snapshot struct.
 * Returns the slot index, or -1 when the table is full. */
int ame_debug_watch(const char *name, const float *value);

/* Same, but the value is formatted as an integer (counts, fps, ...). */
int ame_debug_watch_int(const char *name, const int *value);

int  ame_debug_watch_count(void);
const char *ame_debug_watch_name(int slot);
void ame_debug_watch_clear(void);

/* Render the list as "name: value" lines starting at (x, y) screen px,
 * y growing downward. scale multiplies glyph metrics; layer is the
 * render batch layer (draw above the HUD, e.g. 60). Returns line count.
 * Requires text_init() and an open render frame; otherwise a no-op. */
int ame_debug_watch_draw(float x, float y, float scale,
                         const float tint[4], float layer);

#ifdef __cplusplus
}
#endif

#endif /* AME_DEBUG_H */
