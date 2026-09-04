/* ame-next — text: glyph-atlas layout + draw (text.txt).
 *
 * ONE layout engine feeds 2D and 3D: layout is pure CPU (utf-8 -> runs ->
 * wrap/align -> positioned glyphs), then glyphs are submitted as quads to
 * the shared batch — screen-space, world planar, or world billboard.
 *
 * Inline tags (case-insensitive): {c=RRGGBB} color ... {c} restore,
 * {p=seconds} typewriter pause marker (surfaced in the layout).
 * Missing glyphs render as a visible fallback box, never a crash.
 */
#ifndef AME_TEXT_H
#define AME_TEXT_H

#include <ame/ame.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AME_TEXT_ALIGN_L 0
#define AME_TEXT_ALIGN_C 1
#define AME_TEXT_ALIGN_R 2

#define AME_TXT_MAX_GLYPHS 512
#define AME_TXT_TAG_STACK  8

typedef struct {
    float x, y;            /* pen-relative, baseline origin, layout px units */
    uint32_t color;        /* 0xRRGGBB (alpha from draw tint) */
    int    glyph;          /* index into baked glyph table, -1 = fallback box */
} ame_txt_el;

typedef struct {
    float    at;           /* reveal position (elements before are visible) */
    float    seconds;      /* pause length for the typewriter */
} ame_txt_pause;

typedef struct {
    ame_txt_el   el[AME_TXT_MAX_GLYPHS];
    int          count;
    ame_txt_pause pause[32];
    int          npause;
    float        w, h;     /* measured box */
} ame_text_layout;

/* register the baked atlas with the renderer (creates one texture).
 * Returns the texture id (>=0) or -1. Call once at init. */
int  text_init(bool nearest_sampling);

/* layout/measure are pure CPU — no GL needed (headless-testable).
 * box_w <= 0 disables wrapping. scale multiplies glyph metrics.
 * Returns glyph count; out may be NULL to just measure. */
int  text_layout(const char *utf8, float box_w, int align, float scale,
                 ame_text_layout *out);
void text_measure(const char *utf8, float box_w, int align, float scale,
                  float *w, float *h);

/* drawing submits glyph quads to the render batch (render.txt single pass).
 * tint is rgba floats multiplied with per-run colors. */
void text_draw_screen(const ame_text_layout *l, float x, float y,
                      const float tint[4], float layer);
/* world pose: 4x4 column-major transform applied to each glyph quad
 * (planar mode); billboard helpers live in the game/camera layer */
void text_draw_world(const ame_text_layout *l, const float pose[16],
                     const float tint[4], float layer);

/* baked atlas info (for tests and draw math) */
int   text_font_px(void);
float text_line_h(void);

#ifdef __cplusplus
}
#endif

#endif /* AME_TEXT_H */
