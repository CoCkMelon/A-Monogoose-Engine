/* ame-next — text layout + draw (text.txt). Layout is pure CPU (no GL);
 * draw submits glyph quads into the shared render batch. One .c owns state. */
#include <ame/text.h>
#include <ame/render.h>
#include <ame/math.h>
#include "font_atlas.h"
#include "font_atlas_dsdf.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_atlas_tex = -1;
static int g_dsdf_tex = -1;   /* -1: smooth face unavailable */
static int g_font_mode = AME_FONT_PIXEL;

/* face-aware vertical metrics (both baked at the same base size) */
static float cur_line_h(void) {
    return g_font_mode == AME_FONT_SMOOTH ? AME_DSDF_LINE_H
                                          : (float)AME_FONT_LINE_H;
}
static float cur_ascent(void) {
    return g_font_mode == AME_FONT_SMOOTH ? AME_DSDF_ASCENT
                                          : (float)AME_FONT_ASCENT;
}
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

static int dsdf_glyph_find(uint32_t cp) {
    int lo = 0, hi = ame_dsdf_glyph_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (ame_dsdf_glyphs[mid].cp == cp)
            return mid;
        if (ame_dsdf_glyphs[mid].cp < cp)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

int text_init_dsdf(void) {
    if (g_dsdf_tex >= 0)
        return g_dsdf_tex;
    /* atlas bytes are already RGBA-encoded DSDF samples */
    g_dsdf_tex = rp_load_texture(ame_dsdf_atlas, AME_DSDF_ATLAS_W,
                                 AME_DSDF_ATLAS_H, 4, false /*linear*/);
    if (g_dsdf_tex < 0)
        return -1;
    rp_set_dsdf_atlas(AME_DSDF_RANGE, AME_DSDF_ATLAS_W, AME_DSDF_ATLAS_H);
    return g_dsdf_tex;
}

void text_set_font(int face) {
    if (face == AME_FONT_SMOOTH && g_dsdf_tex < 0)
        return; /* not inited: stay pixel, never half-smooth */
    g_font_mode = face;
}

int text_font_mode(void) { return g_font_mode; }



/* test-only: mark the smooth face AVAILABLE without a GL upload, so
 * pure-CPU layout tests can exercise smooth-face metrics (draw is NOT
 * valid in that state - the texture id is a stub). */
int text_test_force_dsdf(void) {
    if (g_dsdf_tex < 0)
        g_dsdf_tex = 0;
    return g_dsdf_tex;
}

int text_init(bool nearest_sampling) {
    /* renderer-restart re-entry: BOTH faces went stale with the GL
     * context; re-registering the pixel atlas is the reset point (the
     * smooth face needs an explicit text_init_dsdf again) */
    g_dsdf_tex = -1;
    g_font_mode = AME_FONT_PIXEL;

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

/* Snap a coordinate to the pixel grid: floor(x + 0.5). THE text-grid
 * contract (mirrored in lean/Ame/Text.lean): layout stores el
 * positions ALREADY snapped, draw snaps its origin once, and every
 * consumer (caret, selection, hit-testing) must use el[]/w as-is -
 * one snapped pen array, no independent rounding anywhere. This is
 * what keeps the caret exactly on the ink grid and glyphs crisp. */
/* snap on DOUBLE: the layout pen accumulates in double so float error
 * can never flip a floor(x+0.5) boundary (the Lean model's exact Rat) */
static float pen_snap(double x) { return (float)floor(x + 0.5); }

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
    if (s[1] == '/' && (s[2] == 'c' || s[2] == 'C') && s[3] == '}') {
        *out_len = 4;
        return 3; /* close tag: restore default colour */
    }
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

static float tab_advance(float scale); /* 4 spaces of the active face */

/* advance of one visible codepoint at scale - through the ACTIVE face
 * (glyph indexes the pixel table; the smooth face resolves by codepoint) */
static float glyph_advance(int glyph, float scale) {
    if (glyph == AME_TXT_TAB)
        return tab_advance(scale);
    if (glyph < 0) {
        float w, h;
        fallback_box(&w, &h);
        return w * scale + 2.0f * scale;
    }
    if (g_font_mode == AME_FONT_SMOOTH) {
        int d = dsdf_glyph_find(ame_font_glyphs[glyph].cp);
        if (d >= 0)
            return ame_dsdf_glyphs[d].advance * scale;
    }
    return ame_font_glyphs[glyph].advance * scale;
}

/* TAB advance: 4 spaces of the ACTIVE face - a modelled constant, not
 * a fallback box (the caret/hit-test math owns it like any glyph) */
static float tab_advance(float scale) {
    return 4.0f * glyph_advance(glyph_find(' '), scale);
}

/* ------------------------------------------------------------------ */
/* layout core. `tags` false = the no-markup entry (editor): braces are
 * literal glyphs. Invariants (mirrored in lean/Ame/Text.lean):
 *  - the pen accumulates in DOUBLE: float error can never flip a snap
 *    boundary (the Lean model's exact Rat);
 *  - el[].x/.y are snapped whole px - the one grid ink and carets share;
 *  - el[].src_byte is the byte offset of the element's codepoint: the
 *    exact byte<->element bridge (consumed tags/CR have no element);
 *  - w = snapped pen AFTER THE LAST STORED ELEMENT (no phantom EOL
 *    caret when the glyph cap truncates);
 *  - a leading space drops only right after a WRAP - indentation after
 *    an explicit '\n' is content;
 *  - '\r' is consumed silently (CRLF paste = one break);
 *  - '\t' = invisible AME_TXT_TAB element, 4-space advance. */
static int text_layout_core(const char *utf8, float box_w, int align,
                            float scale, ame_text_layout *out, bool tags) {
    if (out) {
        out->count = 0;
        out->npause = 0;
        out->w = 0;
        out->h = 0;
        out->scale = scale;
        out->truncated = 0;
    }
    if (!utf8)
        return 0;

    uint32_t color = 0xFFFFFFu;
    int line_first[AME_TXT_MAX_LINES];
    int line_count = 0;
    int line_start_el = 0;
    double pen_x = 0, pen_y = 0;
    double max_line_w = 0;       /* max over lines of last-STORED pen end */
    double stored_line_end = -1; /* this line's pen after its last store */
    bool wrapped = false;        /* last break was a wrap (space dropping) */
    int total = 0;

    const unsigned char *base = (const unsigned char *)utf8;
    const char *s = utf8;
    line_first[line_count++] = 0;

    while (*s) {
        const unsigned char *el_start = (const unsigned char *)s;

        if (tags) { /* markup consumers only; the plain entry keeps '{' */
            uint32_t tag_col;
            float tag_sec;
            int tag_len;
            int tag = parse_tag(s, &tag_col, &tag_sec, &tag_len);
            if (tag) {
                s += tag_len;
                if (tag == 1) color = tag_col;
                if (tag == 3) color = 0xFFFFFFu;
                if (tag == 2 && out && out->npause < 32) {
                    out->pause[out->npause].at = (float)out->count;
                    out->pause[out->npause].seconds = tag_sec;
                    out->npause++;
                }
                continue;
            }
        }
        uint32_t cp = utf8_next(&s);

        if (cp == '\r')
            continue; /* CRLF pastes: the '\n' carries the break alone */

        if (cp == '\n') { /* explicit newline: finish line */
            if (line_start_el < (out ? out->count : total)) {
                if (stored_line_end > max_line_w)
                    max_line_w = stored_line_end;
                if (line_count < AME_TXT_MAX_LINES) {
                    line_first[line_count++] = out ? out->count : total;
                } else if (out) {
                    out->truncated |= 2; /* line bookkeeping capped */
                }
                line_start_el = out ? out->count : total;
            }
            pen_x = 0;
            stored_line_end = -1;
            wrapped = false; /* indentation after '\n' is content */
            pen_y += (double)cur_line_h() * scale;
            continue;
        }

        int glyph = cp == '\t' ? AME_TXT_TAB : glyph_find(cp);
        float adv = glyph_advance(glyph, scale);

        /* greedy word wrap */
        if (box_w > 0 && pen_x + adv > box_w && cp != ' ') {
            if (pen_x > 0) {
                if (stored_line_end > max_line_w)
                    max_line_w = stored_line_end;
                if (line_count < AME_TXT_MAX_LINES) {
                    line_first[line_count++] = out ? out->count : total;
                } else if (out) {
                    out->truncated |= 2;
                }
                pen_x = 0;
                stored_line_end = -1;
                wrapped = true; /* only NOW may a leading space drop */
                pen_y += (double)cur_line_h() * scale;
            }
        }
        if (cp == ' ' && pen_x == 0 && total > 0 && wrapped)
            continue; /* dropped ONLY right after a wrap */

        if (out && out->count < AME_TXT_MAX_GLYPHS) {
            ame_txt_el *e = &out->el[out->count];
            e->x = pen_snap(pen_x); /* snapped pen: the one grid */
            e->y = pen_snap(pen_y);
            e->color = color;
            e->glyph = glyph;
            e->src_byte = (uint32_t)(el_start - base);
            out->count++;
            stored_line_end = pen_x + (double)adv;
        } else if (out) {
            out->truncated |= 1; /* glyph cap: further pens don't exist */
        }
        total++;
        pen_x += (double)adv;
    }
    if (stored_line_end > max_line_w)
        max_line_w = stored_line_end;

    if (!out)
        return total;

    /* alignment: shift each line's elements */
    for (int li = 0; li < line_count; li++) {
        int first = line_first[li];
        int last = (li + 1 < line_count) ? line_first[li + 1] : out->count;
        if (last <= first)
            continue;
        double line_w = 0;
        for (int i = first; i < last; i++) {
            int g = out->el[i].glyph;
            float a = glyph_advance(g, scale);
            if ((double)out->el[i].x + a > line_w)
                line_w = (double)out->el[i].x + a;
        }
        double shift = 0;
        if (align == AME_TEXT_ALIGN_C)
            shift = (box_w > 0 ? (double)box_w : max_line_w) * 0.5 - line_w * 0.5;
        else if (align == AME_TEXT_ALIGN_R)
            shift = (box_w > 0 ? (double)box_w : max_line_w) - line_w;
        shift = pen_snap(shift); /* keep the grid under alignment */
        for (int i = first; i < last; i++)
            out->el[i].x += (float)shift;
    }

    out->w = pen_snap(max_line_w); /* EOL caret sits here exactly */
    out->h = (float)(pen_y + (double)cur_line_h() * scale);
    return out->count;
}

int text_layout(const char *utf8, float box_w, int align, float scale,
                ame_text_layout *out) {
    return text_layout_core(utf8, box_w, align, scale, out, true);
}

int text_layout_plain(const char *utf8, float box_w, int align, float scale,
                      ame_text_layout *out) {
    return text_layout_core(utf8, box_w, align, scale, out, false);
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
    if (e->glyph == AME_TXT_TAB)
        return; /* invisible: owns a pen cell, draws nothing */
    float col[4];
    col4(e->color, tint, col);
    if (e->glyph < 0) { /* fallback box */
        float w, h;
        fallback_box(&w, &h);
        w *= scale;
        h *= scale;
        rp_push_sprite(g_atlas_tex >= 0 ? g_atlas_tex : rp_white_texture(),
                       x + e->x, y + e->y + cur_ascent() * scale - h, w, h,
                       0.5f, 0.5f, 0.504f, 0.504f, col, layer);
        return;
    }
    const ame_font_glyph *g = &ame_font_glyphs[e->glyph];

    if (g_font_mode == AME_FONT_SMOOTH && g_dsdf_tex >= 0) {
        int di = dsdf_glyph_find(g->cp);
        if (di >= 0) {
            const ame_dsdf_glyph *d = &ame_dsdf_glyphs[di];
            /* cell INCLUDES the DSDF margin; xoff/yoff already carry it */
            float u0 = (float)d->ax / (float)AME_DSDF_ATLAS_W;
            float v0 = (float)d->ay / (float)AME_DSDF_ATLAS_H;
            float u1 = (float)(d->ax + d->aw) / (float)AME_DSDF_ATLAS_W;
            float v1 = (float)(d->ay + d->ah) / (float)AME_DSDF_ATLAS_H;
            float px = x + e->x + d->xoff * scale;
            float py = y + e->y + AME_DSDF_ASCENT * scale + d->yoff * scale;
            float qz = 0.001f * layer; /* match rp_push_sprite z */
            float q0[3] = { px, py, qz },
                  q1[3] = { px + (float)d->aw * scale, py, qz },
                  q2[3] = { px + (float)d->aw * scale,
                            py + (float)d->ah * scale, qz },
                  q3[3] = { px, py + (float)d->ah * scale, qz };
            rp_push_text_quad(g_dsdf_tex, q0, q1, q2, q3,
                              u0, v0, u1, v1, col, layer);
            return;
        }
        /* missing in the smooth table: fall through to the pixel glyph */
    }

    float u0 = (float)g->ax / (float)AME_FONT_ATLAS_WIDTH;
    float v0 = (float)g->ay / (float)AME_FONT_ATLAS_HEIGHT;
    float u1 = (float)(g->ax + g->aw) / (float)AME_FONT_ATLAS_WIDTH;
    float v1 = (float)(g->ay + g->ah) / (float)AME_FONT_ATLAS_HEIGHT;
    rp_push_sprite(g_atlas_tex,
                   x + e->x + g->xoff * scale,
                   y + e->y + cur_ascent() * scale + g->yoff * scale,
                   (float)g->aw * scale, (float)g->ah * scale,
                   u0, v0, u1, v1, col, layer);
}

void text_draw_screen(const ame_text_layout *l, float x, float y,
                      const float tint[4], float layer) {
    if (!l)
        return;
    /* x,y are WINDOW px: translate into world px by the view's top-left
     * (the ortho camera centers itself on pos, so world 0,0 is the screen
     * CENTER otherwise). */
    float ox, oy;
    rp_screen_origin(&ox, &oy);
    /* one snap for the whole call: glyph quads AND any caret/selection
     * the caller derives from the same el[]/origin stay on one grid */
    ox = pen_snap(ox + x);
    oy = pen_snap(oy + y);
    float sc = l->scale > 0.0f ? l->scale : 1.0f;
    for (int i = 0; i < l->count; i++)
        draw_glyph(&l->el[i], ox, oy, sc, tint, layer);
}

void text_draw_world(const ame_text_layout *l, const float pose[16],
                     const float tint[4], float layer) {
    if (!l || !pose)
        return;
    ame_m4 m;
    memcpy(m.m, pose, sizeof m.m);
    for (int i = 0; i < l->count; i++) {
        const ame_txt_el *e = &l->el[i];
        if (e->glyph == AME_TXT_TAB)
            continue; /* invisible: owns a pen cell, draws nothing */
        float col[4];
        col4(e->color, tint, col);
        float x0, y0, w, h, u0, v0, u1, v1;
        if (e->glyph < 0) {
            fallback_box(&w, &h);
            u0 = v0 = 0.5f;
            u1 = v1 = 0.504f;
        } else if (g_font_mode == AME_FONT_SMOOTH && g_dsdf_tex >= 0
                   && dsdf_glyph_find(ame_font_glyphs[e->glyph].cp) >= 0) {
            /* smooth face in WORLD space: pose-transformed DSDF cell
             * (margins included; bearings already carry them) */
            const ame_dsdf_glyph *d =
                &ame_dsdf_glyphs[dsdf_glyph_find(ame_font_glyphs[e->glyph].cp)];
            w = (float)d->aw;
            h = (float)d->ah;
            u0 = (float)d->ax / (float)AME_DSDF_ATLAS_W;
            v0 = (float)d->ay / (float)AME_DSDF_ATLAS_H;
            u1 = (float)(d->ax + d->aw) / (float)AME_DSDF_ATLAS_W;
            v1 = (float)(d->ay + d->ah) / (float)AME_DSDF_ATLAS_H;
            x0 = e->x + (float)d->xoff;
            y0 = e->y + AME_DSDF_ASCENT + (float)d->yoff;
            ame_v3 w0 = ame_m4_xform_point(m, ame_v3_(x0, y0, 0));
            ame_v3 w1 = ame_m4_xform_point(m, ame_v3_(x0 + w, y0, 0));
            ame_v3 w2 = ame_m4_xform_point(m, ame_v3_(x0 + w, y0 + h, 0));
            ame_v3 w3 = ame_m4_xform_point(m, ame_v3_(x0, y0 + h, 0));
            float t0[3] = { w0.x, w0.y, w0.z }, t1[3] = { w1.x, w1.y, w1.z },
                  t2[3] = { w2.x, w2.y, w2.z }, t3[3] = { w3.x, w3.y, w3.z };
            rp_push_text_quad(g_dsdf_tex, t0, t1, t2, t3,
                              u0, v0, u1, v1, col, layer);
            continue;
        } else {
            const ame_font_glyph *g = &ame_font_glyphs[e->glyph];
            w = (float)g->aw;
            h = (float)g->ah;
            u0 = (float)g->ax / (float)AME_FONT_ATLAS_WIDTH;
            v0 = (float)g->ay / (float)AME_FONT_ATLAS_HEIGHT;
            u1 = (float)(g->ax + g->aw) / (float)AME_FONT_ATLAS_WIDTH;
            v1 = (float)(g->ay + g->ah) / (float)AME_FONT_ATLAS_HEIGHT;
            x0 = e->x + (float)g->xoff;
            y0 = e->y + cur_ascent() + (float)g->yoff;
        }
        if (e->glyph < 0) {
            x0 = e->x;
            y0 = e->y + cur_ascent() - h;
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
