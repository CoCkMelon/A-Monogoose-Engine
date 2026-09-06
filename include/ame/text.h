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
#define AME_TXT_MAX_LINES  64  /* alignment bookkeeping cap (line_first[]) */
#define AME_TXT_TAG_STACK  8

/* glyph = AME_TXT_TAB marks an invisible TAB element: it owns a snapped
 * pen cell and a fixed advance (4 spaces) but draws nothing. Caret and
 * hit-test math treat it exactly like a visible glyph. */
#define AME_TXT_TAB (-2)

typedef struct {
    float x, y;            /* pen-relative, baseline origin, layout px units */
    uint32_t color;        /* 0xRRGGBB (alpha from draw tint) */
    int    glyph;          /* glyph table index, -1 fallback box, AME_TXT_TAB */
    uint32_t src_byte;     /* BYTE offset of this element's codepoint in the
                            * input string - the exact byte<->element bridge
                            * (tags/tabs/CR skipped bytes simply have no
                            * element; consumers map bytes through these
                            * anchors instead of re-deriving columns) */
} ame_txt_el;

typedef struct {
    float    at;           /* reveal position (elements before are visible) */
    float    seconds;      /* pause length for the typewriter */
} ame_txt_pause;

typedef struct {
    ame_txt_el   el[AME_TXT_MAX_GLYPHS];
    int          count;
    float        scale;   /* draw scale captured at layout time */
    ame_txt_pause pause[32];
    int          npause;
    float        w, h;     /* measured box: w = snapped pen AFTER the last
                            * STORED element (the EOL caret basis - never a
                            * phantom pen for elements that don't exist) */
    int          truncated; /* bit 1: more than AME_TXT_MAX_GLYPHS codepoints
                             * bit 2: more than AME_TXT_MAX_LINES lines */
} ame_text_layout;

/* register the baked atlas with the renderer (creates one texture).
 * Returns the texture id (>=0) or -1. Call once at init. */
int  text_init(bool nearest_sampling);

/* --- font faces ----------------------------------------------------------- */
/* PIXEL: the baked bitmap atlas (crisp: nearest sampling + snapped pens -
 * the default and the path the grid contract was proven on).
 * SMOOTH: the baked DSDF atlas (DejaVu) - first-order densely sampled
 * distance fields (Acta Cybernetica 25, 2021), anti-aliased at any scale
 * in 2D and 3D. Layout metrics switch with the face (advances/line_h
 * come from the ACTIVE table); the snapped-pen GRID CONTRACT itself is
 * face-independent and holds for both. */
enum { AME_FONT_PIXEL = 0, AME_FONT_SMOOTH = 1 };

/* upload the baked DSDF atlas (RGBA8, linear sampling) + bind its
 * parameters to the renderer. Call once at init; optional - without it
 * the text module simply stays on the pixel face. */
int  text_init_dsdf(void);

/* select the active face. Falls back to PIXEL if text_init_dsdf has
 * not run (or failed) - never a missing-glyph surprise. */
void text_set_font(int face);
int  text_font_mode(void);

/* layout/measure are pure CPU — no GL needed (headless-testable).
 * box_w <= 0 disables wrapping. scale multiplies glyph metrics.
 * Returns glyph count; out may be NULL to just measure.
 *
 * GRID CONTRACT (mirrored in lean/Ame/Text.lean): el[].x/.y and w are
 * SNAPPED to whole pixels (floor(+0.5)) - the pen grid. Draw snaps
 * its origin once and consumes them as-is. Carets, selection rects
 * and hit-tests must read el[]/w DIRECTLY (never re-round against
 * fractional advances): caret_i.x = origin + el[i].x, EOL caret =
 * origin + w. Then caret and ink share one grid BY CONSTRUCTION. */
int  text_layout(const char *utf8, float box_w, int align, float scale,
                 ame_text_layout *out);
/* no-markup layout: '{'/'}' are LITERAL glyphs, no tags are consumed -
 * the entry point for plain-text consumers (the editor). Element
 * src_byte anchors are 1:1 with visible codepoints. */
int  text_layout_plain(const char *utf8, float box_w, int align, float scale,
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
