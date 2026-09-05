#include "ame/text.h"

#include "font5x7.inc"

#include <string.h>

static int glyph_index(int ch)
{
    if (ch < AME_TEXT_FIRST || ch > AME_TEXT_LAST) return 0; /* space */
    return ch - AME_TEXT_FIRST;
}

ame_font *ame_font_bake(ame_font *font,
                        unsigned char *rgba, int atlas_size,
                        int origin_x, int origin_y)
{
    if (!font) return font;
    font->atlas_size = atlas_size;
    font->origin_x = origin_x;
    font->origin_y = origin_y;
    font->cell = AME_TEXT_CELL;

    for (int gi = 0; gi < AME_TEXT_COUNT; gi++) {
        int col = gi % AME_TEXT_COLS;
        int row = gi / AME_TEXT_COLS;
        int ox = origin_x + col * AME_TEXT_CELL;
        int oy = origin_y + row * AME_TEXT_CELL;
        /* Transparent cell, white opaque ink. Adjacent glyph quads must not overlap. */
        for (int j = 0; j < AME_TEXT_CELL; j++) {
            for (int i = 0; i < AME_TEXT_CELL; i++) {
                int idx = ((oy + j) * atlas_size + (ox + i)) * 4;
                if (ox + i < 0 || oy + j < 0 || ox + i >= atlas_size || oy + j >= atlas_size)
                    continue;
                rgba[idx + 0] = 0;
                rgba[idx + 1] = 0;
                rgba[idx + 2] = 0;
                rgba[idx + 3] = 0;
            }
        }
        const unsigned char *cols = FONT5X7[gi];
        for (int cx = 0; cx < 5; cx++) {
            unsigned bits = cols[cx];
            for (int cy = 0; cy < 7; cy++) {
                if (bits & (1u << cy)) {
                    /* +1,+0 inset so 5x7 sits inside 8x8 with a 1px left/right pad */
                    ame_atlas_dot(rgba, atlas_size, atlas_size,
                                  ox + 1 + cx, oy + cy,
                                  255, 255, 255);
                }
            }
        }
    }
    /* 2x2 white block for untextured geometry, bottom-right of font region */
    int wx = origin_x + AME_TEXT_COLS * AME_TEXT_CELL - 2;
    int wy = origin_y + 6 * AME_TEXT_CELL - 2;
    if (wx < 0) wx = atlas_size - 4;
    if (wy < 0) wy = atlas_size - 4;
    ame_atlas_fill(rgba, atlas_size, atlas_size, wx, wy, 4, 4, 255, 255, 255);
    font->white = ame_uv_texel(wx + 2, wy + 2, atlas_size);
    return font;
}

ame_uv ame_font_glyph_uv(const ame_font *font, int ch)
{
    int gi = glyph_index(ch);
    int col = gi % AME_TEXT_COLS;
    int row = gi / AME_TEXT_COLS;
    int x = font->origin_x + col * font->cell;
    int y = font->origin_y + row * font->cell;
    /* Inset 0.5 texel so NEAREST stays inside the cell. */
    return ame_uv_rect(x, y, font->cell, font->cell, font->atlas_size, 0.5f);
}

float ame_font_width(const ame_font *font, const char *text, float pixel_size)
{
    (void)font;
    if (!text) return 0.0f;
    /* 6 pixels advance (5 ink + 1 gap) */
    int n = (int)strlen(text);
    return (float)n * 6.0f * pixel_size;
}

void ame_font_draw(ame_pipeline *batch, const ame_font *font,
                   float x, float y, float z,
                   float pixel_size, const char *text, ame_rgba color)
{
    if (!batch || !font || !text) return;
    float advance = 6.0f * pixel_size;
    float qw = 6.0f * pixel_size;
    float qh = 8.0f * pixel_size;
    float cx = x;
    for (const char *p = text; *p; p++) {
        int gi = glyph_index((unsigned char)*p);
        int gx = font->origin_x + (gi % AME_TEXT_COLS) * font->cell;
        int gy = font->origin_y + (gi / AME_TEXT_COLS) * font->cell;
        /* 6x8 UV matches 6px advance so neighbouring letters never overlap. */
        ame_uv uv = ame_uv_rect(gx, gy, 6, 8, font->atlas_size, 0.5f);
        ame_batch_xy_rect(batch, cx + qw * 0.5f, y + qh * 0.5f, z, qw, qh, uv, color);
        cx += advance;
    }
}
