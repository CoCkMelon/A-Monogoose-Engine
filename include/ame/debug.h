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

/* Measured size of the drawn block in screen px at `scale`: widest name
 * column + gap + widest value, and the shared line-stack height. False
 * when nothing is registered or the font is not ready (sizes zeroed).
 * CPU-only (text_layout); use it to ANCHOR the overlay instead of magic
 * offsets, e.g. right-aligned: x = view_w - margin - w. The size is the
 * TEXT block; a scrim (bg alpha > 0) reaches `pad` px beyond it on every
 * side, so anchored callers subtract pad too. */
bool ame_debug_watch_size(float scale, float *w, float *h);

/* Draw style. tint = name column; value_tint = value column (alpha < 0
 * copies tint); bg = scrim quad behind the measured block (alpha <= 0
 * disables it), inset/outset by pad px; layer is the render batch layer
 * (above the HUD, e.g. 60) - the scrim draws one layer below the text. */
typedef struct ame_debug_watch_style {
    float scale;
    float tint[4];
    float value_tint[4];
    float bg[4];
    float pad;
    float layer;
} ame_debug_watch_style;

/* Render the list as two aligned columns starting at (x, y) screen px,
 * y growing downward: names in tint, values left-aligned in a shared
 * column so digits line up vertically. Returns the line count drawn.
 * Requires text_init() and an open render frame; otherwise a no-op. */
int ame_debug_watch_draw(float x, float y, const ame_debug_watch_style *st);

#ifdef __cplusplus
}
#endif

#endif /* AME_DEBUG_H */
