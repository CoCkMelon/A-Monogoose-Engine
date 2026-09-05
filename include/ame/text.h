#ifndef AME_TEXT_H
#define AME_TEXT_H

/*
 * Bitmap text. Glyphs are baked into an atlas as 8x8 cells (5x7 ink).
 * Drawing emits ONE textured quad per character — never per pixel.
 *
 * ASCII 32..126. Unknown codes draw a box. Layout is left-to-right,
 * baseline at y; no wrapping here (callers wrap if they need it).
 */

#include "ame/gfx.h"

enum {
    AME_TEXT_FIRST = 32,
    AME_TEXT_LAST  = 126,
    AME_TEXT_COUNT = 95,
    AME_TEXT_CELL  = 8,
    AME_TEXT_COLS  = 16
};

typedef struct ame_font {
    int atlas_size;
    int origin_x;
    int origin_y;
    int cell;
    ame_uv white; /* solid texel for untextured coloured geometry */
} ame_font;

/* Paint glyphs into rgba atlas. 16 columns of 8x8 starting at origin. */
ame_font *ame_font_bake(ame_font *font,
                        unsigned char *rgba, int atlas_size,
                        int origin_x, int origin_y);

ame_uv  ame_font_glyph_uv(const ame_font *font, int ch);
float   ame_font_width(const ame_font *font, const char *text, float pixel_size);

/* pixel_size is the world size of one atlas pixel. Glyph cell is 8 pixels. */
void ame_font_draw(ame_pipeline *batch, const ame_font *font,
                   float x, float y, float z,
                   float pixel_size, const char *text, ame_rgba color);

#endif
