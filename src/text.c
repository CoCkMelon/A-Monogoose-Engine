/* ame-next — text layout + draw (text.txt). Layout is pure CPU (no GL);
 * draw submits glyph quads into the shared render batch. One .c owns state. */
#include <ame/text.h>
#include <ame/render.h>
#include <ame/math.h>
#include "font_atlas.h"

#include <stdlib.h>
#include <string.h>

static int g_atlas_tex = -1;
/* metrics come straight from the BAKED constants so layout works with no
 * GL/init (text.txt: layout is pure CPU, headless-testable) */
#define g_scale_font_px ((float)AME_FONT_PX)
#define g_line_h        ((float)AME_FONT_LINE_H)
#define g_ascent        ((float)AME_FONT_ASCENT)

/* fallback box metrics when a codepoint is missing (visible, not a crash) */
static void fallback_box(float *w, float *h) {
    *w = g_scale_font_px * 0.62f;
    *h = g_scale_font_px * 0.62f;
}

static int glyph_find(uint32_t cp) {
    /* binary search over sorted baked table */
    int lo = 0, hi = ame_font_glyph_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if ((uint32_t)ame_font_glyphs[mid].cp == cp)
            return mid;
        if ((uint32_t)ame_font_glyphs[mid].cp < cp)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

int text_init(bool nearest_sampling) {
    /* expand A8 to RGBA (white, alpha = coverage) once, upload, free */
    size_t n = (size_t)AME_FONT_ATLAS_WIDTH * (size_t)AME_FONT_ATLAS_HEIGHT;
    uint8_t *rgba = malloc(n * 4);
    if (!rgba)
        return -1;
    for (size_t i = 0; i < n; i++) {
        rgba[i * 4] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = ame_font_atlas_a8[i];
    }
    g_atlas_tex = rp_load_texture(rgba, AME_FONT_ATLAS_WIDTH, AME_FONT_ATLAS_HEIGHT,
                                  4, nearest_sampling);
    free(rgba);
    return g_atlas_tex;
}

int text_font_px(void)    { return AME_FONT_PX; }
float text_line_h(void)   { return AME_FONT_LINE_H; }

/* --- utf-8 decode --------------------------------------------------------- */
static uint32_t utf8_next(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t c = *p++;
    if (c < 0x80) { /* ascii */ }
    else if ((c & 0xE0) == 0xC0 && (*p & 0xC0) == 0x80) {
        c = ((c & 0x1Fu) << 6) | (*p++ & 0x3Fu);
    } else if ((c & 0xF0) == 0xE0 && (*p & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80) {
        c = ((c & 0x0Fu) << 12) | ((p[0] & 0x3Fu) << 6) | (p[1] & 0x3Fu);
        p += 2;
    } else if ((c & 0xF8) == 0xF0 && (*p & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80
               && (p[2] & 0xC0) == 0x80) {
        c = ((c & 0x07u) << 18) | ((p[0] & 0x3Fu) << 12) | ((p[1] & 0x3Fu) << 6)
          | (p[2] & 0x3Fu);
        p += 3;
    } else {
        c = 0xFFFDu; /* invalid byte: replacement char */
    }
    *s = (const char *)p;
    return c;
}

/* --- tag parser: {c=RRGGBB} {c} {p=0.5} ------------------------------------ */
static int parse_hex_nibble(char c, bool *ok) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    *ok = false;
    return 0;
}

/* returns: 0 no tag, 1 color tag (out_color), 2 pause (out_sec), 3 close tag */
static int parse_tag(const char *s, uint32_t *out_color, float *out_sec,
                     int *out_len) {
    if (s[0] != '{')
        return 0;
    if (s[1] == 'c' || s[1] == 'C') {
        if (s[2] == '}' || s[2] == '}') { *out_len = 3; return 3; }
        if ((s[2] == '=' || s[2] == '=') && s[3] != 0) {
            bool ok = true;
            uint32_t col = 0;
            for (int i = 0; i < 6; i++)
                col = (col << 4) | (uint32_t)parse_hex_nibble(s[3 + i], &ok);
            if (ok && s[9] == '}') {
                *out_color = col;
                *out_len = 10;
                return 1;
            }
        }
        return 0;
    }
    if ((s[1] == 'p' || s[1] == 'P') && s[2] == '=' && s[3] != 0) {
        char *end;
        float v = strtof(s + 3, &end);
        if (end && *end == '}') {
            *out_sec = v;
            *out_len = (int)(end - s) + 1;
            return 2;
        }
    }
    return 0;
}

/* advance of one visible codepoint at scale */
static float glyph_advance(int glyph, float scale) {
    if (glyph < 0) {
        float w, h;
        fallback_box(&w, &h);
        return w * scale + 2.0f * scale;
    }
    return ame_font_glyphs[glyph].advance * scale;
}

int text_layout(const char *utf8, float box_w, int align, float scale,
                ame_text_layout *out) {
    if (out) {
        out->count = 0;
        out->npause = 0;
        out->w = 0;
        out->h = 0;
    }
    if (!utf8)
        return 0;

    uint32_t color = 0xFFFFFFu;
    int line_first[64];  /* first element index per line */
    int line_count = 0;
    int line_start_el = 0;
    float pen_x = 0, pen_y = 0;
    float max_line_w = 0;
    int total = 0;

    const char *s = utf8;
    line_first[line_count++] = 0;

    while (*s) {
        /* tags */
        uint32_t tag_col;
        float tag_sec;
        int tag_len;
        int tag = parse_tag(s, &tag_col, &tag_sec, &tag_len);
        if (tag) {
            s += tag_len;
            if (tag == 1) color = tag_col;
            if (tag == 3) color = 0xFFFFFFu;
            if (tag == 2 && out && out->npause < 32) {
                out->pause[out->npause].at = (float)(out ? out->count : total);
                out->pause[out->npause].seconds = tag_sec;
                out->npause++;
            }
            continue;
        }
        uint32_t cp = utf8_next(&s);

        if (cp == '\n') { /* explicit newline: finish line */
            if (line_start_el < (out ? out->count : total)) {
                if (pen_x > max_line_w) max_line_w = pen_x;
                line_first[line_count++] = out ? out->count : total;
                line_start_el = out ? out->count : total;
            }
            pen_x = 0;
            pen_y += g_line_h * scale;
            continue;
        }

        int glyph = glyph_find(cp);
        float adv = glyph_advance(glyph, scale);

        /* greedy word wrap */
        if (box_w > 0 && pen_x + adv > box_w && cp != ' ') {
            if (pen_x > 0) {
                if (pen_x > max_line_w) max_line_w = pen_x;
                if (line_count < 64)
                    line_first[line_count++] = out ? out->count : total;
                pen_x = 0;
                pen_y += g_line_h * scale;
            }
        }
        /* trailing spaces don't wrap-line again; drop leading space after wrap */
        if (cp == ' ' && pen_x == 0 && total > 0)
            continue;

        if (out && out->count < AME_TXT_MAX_GLYPHS) {
            ame_txt_el *e = &out->el[out->count];
            e->x = pen_x;
            e->y = pen_y;
            e->color = color;
            e->glyph = glyph;
            out->count++;
        }
        total++;
        pen_x += adv;
    }
    if (pen_x > max_line_w)
        max_line_w = pen_x;

    if (!out)
        return total;

    /* alignment: shift each line's elements */
    for (int li = 0; li < line_count; li++) {
        int first = line_first[li];
        int last = (li + 1 < line_count) ? line_first[li + 1] : out->count;
        if (last <= first)
            continue;
        float line_w = 0;
        for (int i = first; i < last; i++) {
            int g = out->el[i].glyph;
            float a = glyph_advance(g, scale);
            if (out->el[i].x + a > line_w)
                line_w = out->el[i].x + a;
        }
        float shift = 0;
        if (align == AME_TEXT_ALIGN_C) shift = (box_w > 0 ? box_w : max_line_w) * 0.5f - line_w * 0.5f;
        else if (align == AME_TEXT_ALIGN_R) shift = (box_w > 0 ? box_w : max_line_w) - line_w;
        for (int i = first; i < last; i++)
            out->el[i].x += shift;
    }

    out->w = max_line_w;
    out->h = pen_y + g_line_h * scale;
    return out->count;
}

void text_measure(const char *utf8, float box_w, int align, float scale,
                  float *w, float *h) {
    ame_text_layout tmp;
    int n = text_layout(utf8, box_w, align, scale, &tmp);
    (void)n;
    if (w) *w = tmp.w;
    if (h) *h = tmp.h;
}

/* --- draw ------------------------------------------------------------------ */

static void col4(uint32_t c, const float tint[4], float out[4]) {
    out[0] = ((c >> 16) & 0xFF) / 255.0f * tint[0];
    out[1] = ((c >> 8) & 0xFF) / 255.0f * tint[1];
    out[2] = (c & 0xFF) / 255.0f * tint[2];
    out[3] = tint[3];
}

static void draw_glyph(const ame_txt_el *e, float x, float y, float scale,
                       const float tint[4], float layer) {
    float col[4];
    col4(e->color, tint, col);
    if (e->glyph < 0) { /* fallback box */
        float w, h;
        fallback_box(&w, &h);
        w *= scale;
        h *= scale;
        rp_push_sprite(g_atlas_tex >= 0 ? g_atlas_tex : rp_white_texture(),
                       x + e->x, y + e->y + g_ascent * scale - h, w, h,
                       0.5f, 0.5f, 0.504f, 0.504f, col, layer);
        return;
    }
    const ame_font_glyph *g = &ame_font_glyphs[e->glyph];
    float u0 = (float)g->ax / (float)AME_FONT_ATLAS_WIDTH;
    float v0 = (float)g->ay / (float)AME_FONT_ATLAS_HEIGHT;
    float u1 = (float)(g->ax + g->aw) / (float)AME_FONT_ATLAS_WIDTH;
    float v1 = (float)(g->ay + g->ah) / (float)AME_FONT_ATLAS_HEIGHT;
    rp_push_sprite(g_atlas_tex,
                   x + e->x + g->xoff * scale,
                   y + e->y + g_ascent * scale + g->yoff * scale,
                   (float)g->aw * scale, (float)g->ah * scale,
                   u0, v0, u1, v1, col, layer);
}

void text_draw_screen(const ame_text_layout *l, float x, float y,
                      const float tint[4], float layer) {
    if (!l)
        return;
    for (int i = 0; i < l->count; i++)
        draw_glyph(&l->el[i], x, y, 1.0f, tint, layer);
}

void text_draw_world(const ame_text_layout *l, const float pose[16],
                     const float tint[4], float layer) {
    if (!l || !pose)
        return;
    ame_m4 m;
    memcpy(m.m, pose, sizeof m.m);
    for (int i = 0; i < l->count; i++) {
        const ame_txt_el *e = &l->el[i];
        float col[4];
        col4(e->color, tint, col);
        float x0, y0, w, h, u0, v0, u1, v1;
        if (e->glyph < 0) {
            fallback_box(&w, &h);
            u0 = v0 = 0.5f;
            u1 = v1 = 0.504f;
        } else {
            const ame_font_glyph *g = &ame_font_glyphs[e->glyph];
            w = (float)g->aw;
            h = (float)g->ah;
            u0 = (float)g->ax / (float)AME_FONT_ATLAS_WIDTH;
            v0 = (float)g->ay / (float)AME_FONT_ATLAS_HEIGHT;
            u1 = (float)(g->ax + g->aw) / (float)AME_FONT_ATLAS_WIDTH;
            v1 = (float)(g->ay + g->ah) / (float)AME_FONT_ATLAS_HEIGHT;
            x0 = e->x + (float)g->xoff;
            y0 = e->y + g_ascent + (float)g->yoff;
        }
        if (e->glyph < 0) {
            x0 = e->x;
            y0 = e->y + g_ascent - h;
        }
        /* glyph quad corners in layout space -> world pose */
        ame_v3 c0 = ame_m4_xform_point(m, ame_v3_(x0, y0, 0));
        ame_v3 c1 = ame_m4_xform_point(m, ame_v3_(x0 + w, y0, 0));
        ame_v3 c2 = ame_m4_xform_point(m, ame_v3_(x0 + w, y0 + h, 0));
        ame_v3 c3 = ame_m4_xform_point(m, ame_v3_(x0, y0 + h, 0));
        float q0[3] = { c0.x, c0.y, c0.z };
        float q1[3] = { c1.x, c1.y, c1.z };
        float q2[3] = { c2.x, c2.y, c2.z };
        float q3[3] = { c3.x, c3.y, c3.z };
        rp_push_quad(g_atlas_tex >= 0 ? g_atlas_tex : rp_white_texture(),
                     q0, q1, q2, q3, u0, v0, u1, v1, col, layer);
    }
}
