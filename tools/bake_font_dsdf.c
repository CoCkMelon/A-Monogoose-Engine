/* tools — bake a DSDF (densely sampled distance field) font atlas.
 *
 * Technique per "Geometric Distance Fields of Plane Curves", Acta
 * Cybernetica 25 (2021) 187-203: the atlas stores FIRST-ORDER samples
 * ("A1"): every texel carries (d, grad d). The fragment shader
 * reconstructs the signed distance with per-corner Taylor terms,
 * which is EXACT for straight edges (glyph stems) and higher-order
 * than plain (zeroth-order) SDF bilinear sampling - sharper corners
 * at the same atlas resolution, robust in perspective (3D) too.
 *
 * Pipeline (deterministic, no external deps beyond stb_truetype):
 *   1. rasterize each glyph's coverage at 4x the base pixel size
 *   2. exact signed EDT on the 4x bitmap (Felzenszwalb 1D transform,
 *      rows then columns, seeds inside/outside separately)
 *   3. downsample 4x4 -> one atlas texel: average distance (in
 *      atlas-texel units) + central-difference gradient (unit where
 *      the field is defined, 0 in the saturated band)
 *   4. pack cells (ink + RANGE margin) on shelves into a fixed-width
 *      atlas, growing the height as needed
 *   5. emit font_atlas_dsdf.c/h: RGBA8 atlas (R = distance, GB =
 *      gradient) + glyph table with the SAME advance/bearing metrics
 *      as the pixel atlas baked by bake_font.c (layout is shared).
 *
 * Encoding (atlas texel):
 *   R = 255 * (0.5 + clamp(d / RANGE, -1, 1) * 0.5)   d>0 inside ink
 *   G = 255 * (grad.x * 0.5 + 0.5)   atlas-texel units, |grad| <= 1
 *   B = 255 * (grad.y * 0.5 + 0.5)
 *   A = 255
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#define BASE_PX 32
#define SS 4              /* supersampling factor for the EDT source */
#define RANGE 8.0f        /* distance band in atlas texels */
#define MARGIN 8          /* cell padding in texels (>= RANGE) */
#define ATLAS_W 1024
#define ATLAS_H_MAX 2048

static const struct { uint32_t lo, hi; } RANGES[] = {
    { 0x20, 0x7E },   /* basic latin */
    { 0xA0, 0xFF },   /* latin-1 supplement */
    { 0x100, 0x17F }, /* latin extended-A */
    { 0x400, 0x4FF }, /* cyrillic + cyrillic supplement */
};

/* --- exact 1D squared distance transform (Felzenszwalb & Huttenlocher,
 * general form: arbitrary non-negative site costs f; seeds cost 0,
 * non-seeds cost INF=1e20 - finite so INF-INF never NaNs). ------- */
static void dt1d_gen(const double *f, double *d, int n) {
    const double INF = 1e20;
    int *v = malloc(sizeof(int) * (size_t)n);
    double *z = malloc(sizeof(double) * (size_t)(n + 1));
    int k = 0;
    v[0] = 0;
    z[0] = -INF;
    z[1] = INF;
    for (int q = 1; q < n; q++) {
        double s = ((double)(q * q - v[k] * v[k]) + f[q] - f[v[k]])
                   / (2.0 * (double)(q - v[k]));
        while (s <= z[k]) {
            k--;
            s = ((double)(q * q - v[k] * v[k]) + f[q] - f[v[k]])
                / (2.0 * (double)(q - v[k]));
        }
        k++;
        v[k] = q;
        z[k] = s;
        z[k + 1] = INF;
    }
    int qs = 0;
    for (int q = 0; q < n; q++) {
        while (z[qs + 1] < (double)q)
            qs++;
        double dq = (double)q - (double)v[qs];
        d[q] = dq * dq + f[v[qs]];
    }
    free(v);
    free(z);
}

/* 2D squared EDT of a binary grid (1 = seed). out: squared distance
 * to the nearest seed, exact (integer-valued as double). */
static void edt2d(const uint8_t *seed, int w, int h, double *out) {
    /* rows then columns; column pass over transposed buffers */
    double *tmp = malloc(sizeof(double) * (size_t)w * (size_t)h);
    double *f = calloc((size_t)(w > h ? w : h), sizeof(double)); /* gcc -Wmaybe-uninitialized pacifier */
    double *d = calloc((size_t)(w > h ? w : h), sizeof(double));
    const double INF = 1e20;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++)
            f[x] = seed[(size_t)y * w + x] ? 0.0 : INF;
        dt1d_gen(f, d, w);
        for (int x = 0; x < w; x++)
            tmp[(size_t)y * w + x] = d[x];
    }
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++)
            f[y] = tmp[(size_t)y * w + x];
        dt1d_gen(f, d, h);
        for (int y = 0; y < h; y++)
            out[(size_t)y * w + x] = d[y];
    }
    free(tmp);
    free(f);
    free(d);
}

typedef struct {
    uint32_t cp;
    uint16_t ax, ay, aw, ah; /* atlas cell rect (texels) */
    int16_t xoff, yoff;      /* pen-relative ink origin (px, base) */
    float advance;           /* px per codepoint (base) */
} dsdf_glyph;

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <font.ttf> <out_prefix>\n", argv[0]);
        return 1;
    }
    const char *ttf_path = argv[1];
    const char *prefix = argv[2];
    (void)argc;

    FILE *f = fopen(ttf_path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", ttf_path); return 1; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *data = malloc((size_t)len);
    if (!data || fread(data, 1, (size_t)len, f) != (size_t)len) {
        fprintf(stderr, "read fail\n");
        return 1;
    }
    fclose(f);

    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, data, 0)) {
        fprintf(stderr, "stbtt_InitFont failed\n");
        return 1;
    }
    /* metrics MUST match bake_font.c exactly (shared layout) */
    float scale = stbtt_ScaleForPixelHeight(&info, (float)BASE_PX);
    float scale4 = stbtt_ScaleForPixelHeight(&info, (float)BASE_PX * SS);
    int ascent, descent, line_gap;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
    (void)scale;

    /* pass 1: rasterize + build fields per glyph, keep in memory */
    int nranges = (int)(sizeof RANGES / sizeof RANGES[0]);
    int cap = 0;
    for (int r = 0; r < nranges; r++)
        cap += (int)(RANGES[r].hi - RANGES[r].lo) + 1;
    dsdf_glyph *glyphs = malloc(sizeof(dsdf_glyph) * (size_t)cap);
    float **cell_d = malloc(sizeof(float *) * (size_t)cap); /* downsampled field */
    int ng = 0;

    for (int r = 0; r < nranges; r++) {
        for (uint32_t cp = RANGES[r].lo; cp <= RANGES[r].hi; cp++) {
            int adv, lsb;
            stbtt_GetCodepointHMetrics(&info, (int)cp, &adv, &lsb);
            int gw4, gh4, xoff4, yoff4;
            unsigned char *bmp = stbtt_GetCodepointBitmap(
                &info, 0, scale4, (int)cp, &gw4, &gh4, &xoff4, &yoff4);
            int cw = gw4 / SS + 2 * MARGIN;
            int ch = gh4 / SS + 2 * MARGIN;
            if (gw4 == 0 || gh4 == 0) {
                stbtt_FreeBitmap(bmp, NULL);
                if (cp == ' ') {
                    glyphs[ng] = (dsdf_glyph){ .cp = cp, .ax = 0, .ay = 0,
                                               .aw = 0, .ah = 0, .xoff = 0,
                                               .yoff = 0,
                                               .advance = adv * scale };
                    cell_d[ng] = NULL;
                    ng++;
                }
                continue;
            }
            /* padded 4x grid: outside seeds on the pad border ring so
             * distances saturate at the cell edge (>= RANGE) */
            int pw = gw4 + 2 * (MARGIN * SS);
            int ph = gh4 + 2 * (MARGIN * SS);
            uint8_t *in_bin = malloc((size_t)pw * ph);
            uint8_t *out_bin = malloc((size_t)pw * ph);
            memset(in_bin, 0, (size_t)pw * ph);
            memset(out_bin, 0, (size_t)pw * ph);
            for (int y = 0; y < ph; y++) {
                int sy = y - MARGIN * SS;
                for (int x = 0; x < pw; x++) {
                    int sx = x - MARGIN * SS;
                    unsigned char cov = (sy >= 0 && sy < gh4 && sx >= 0
                                         && sx < gw4)
                                            ? bmp[(size_t)sy * gw4 + sx]
                                            : 0;
                    size_t at = (size_t)y * pw + x;
                    if (cov > 127) {
                        in_bin[at] = 0;      /* not an outside seed */
                        out_bin[at] = 1;     /* inside ink */
                    } else {
                        in_bin[at] = 1;      /* outside seed */
                        out_bin[at] = 0;
                    }
                }
            }
            double *din = malloc(sizeof(double) * (size_t)pw * ph);
            double *dout = malloc(sizeof(double) * (size_t)pw * ph);
            edt2d(in_bin, pw, ph, dout); /* distance to nearest OUTSIDE */
            edt2d(out_bin, pw, ph, din); /* distance to nearest INSIDE */
            /* signed: + inside ink (4x px units) */
            float *field = malloc(sizeof(float) * (size_t)cw * ch);
            for (int y = 0; y < ch; y++) {
                for (int x = 0; x < cw; x++) {
                    double acc = 0;
                    for (int yy = 0; yy < SS; yy++)
                        for (int xx = 0; xx < SS; xx++) {
                            int sx = x * SS + xx, sy = y * SS + yy;
                            size_t at = (size_t)sy * pw + sx;
                            double d4 = sqrt(dout[at]) - sqrt(din[at]);
                            acc += d4;
                        }
                    /* average of 4x samples, converted to atlas texels */
                    field[(size_t)y * cw + x] =
                        (float)(acc / (double)(SS * SS) / (double)SS);
                }
            }
            free(din);
            free(dout);
            free(in_bin);
            free(out_bin);
            stbtt_FreeBitmap(bmp, NULL);

            glyphs[ng] = (dsdf_glyph){ .cp = cp, .ax = 0, .ay = 0,
                                       .aw = (uint16_t)cw,
                                       .ah = (uint16_t)ch,
                                       .xoff = (int16_t)((int)floor(xoff4 / (double)SS) - MARGIN),
                                       .yoff = (int16_t)((int)floor(yoff4 / (double)SS) - MARGIN),
                                       .advance = adv * scale };
            cell_d[ng] = field;
            ng++;
        }
    }

    /* pass 2: shelf pack */
    int cx = 1, cy = 1, row_h = 0;
    for (int i = 0; i < ng; i++) {
        if (glyphs[i].aw == 0)
            continue;
        if (cx + glyphs[i].aw + 1 > ATLAS_W) {
            cx = 1;
            cy += row_h + 1;
            row_h = 0;
        }
        if (cy + glyphs[i].ah + 1 > ATLAS_H_MAX) {
            fprintf(stderr, "dsdf atlas overflow at U+%04X\n", glyphs[i].cp);
            return 1;
        }
        glyphs[i].ax = (uint16_t)cx;
        glyphs[i].ay = (uint16_t)cy;
        if (glyphs[i].ah > row_h)
            row_h = glyphs[i].ah;
        cx += glyphs[i].aw + 1;
    }
    int atlas_h = cy + row_h + 1;
    atlas_h = (atlas_h + 15) & ~15;

    /* pass 3: emit RGBA atlas (d, grad) */
    unsigned char *atlas = calloc((size_t)ATLAS_W * (size_t)atlas_h, 4);
    for (int i = 0; i < ng; i++) {
        if (!cell_d[i])
            continue;
        int cw = glyphs[i].aw, chh = glyphs[i].ah;
        const float *fd = cell_d[i];
        for (int y = 0; y < chh; y++) {
            for (int x = 0; x < cw; x++) {
                float d = fd[(size_t)y * cw + x];
                /* central differences (clamped at cell borders) */
                float dl = fd[(size_t)y * cw + (x > 0 ? x - 1 : x)];
                float dr = fd[(size_t)y * cw + (x + 1 < cw ? x + 1 : x)];
                float du = fd[(size_t)(y > 0 ? y - 1 : y) * cw + x];
                float dd = fd[(size_t)(y + 1 < chh ? y + 1 : y) * cw + x];
                float gx = (dr - dl) * 0.5f;
                float gy = (dd - du) * 0.5f;
                if (d <= -RANGE + 0.001f || d >= RANGE - 0.001f) {
                    gx = 0; /* saturated band: gradient unused */
                    gy = 0;
                }
                float dn = d / RANGE;
                if (dn > 1) dn = 1;
                if (dn < -1) dn = -1;
                unsigned char *px =
                    atlas + ((size_t)(glyphs[i].ay + y) * ATLAS_W
                             + (size_t)(glyphs[i].ax + x)) * 4;
                px[0] = (unsigned char)lroundf((0.5f + dn * 0.5f) * 255.0f);
                px[1] = (unsigned char)lroundf((gx * 0.5f + 0.5f) * 255.0f);
                px[2] = (unsigned char)lroundf((gy * 0.5f + 0.5f) * 255.0f);
                px[3] = 255;
            }
        }
        free(cell_d[i]);
    }

    /* pass 4: emit C source */
    char path_c[512], path_h[512];
    snprintf(path_c, sizeof path_c, "%s.c", prefix);
    snprintf(path_h, sizeof path_h, "%s.h", prefix);
    FILE *oc = fopen(path_c, "w");
    FILE *oh = fopen(path_h, "w");
    if (!oc || !oh) { fprintf(stderr, "cannot write %s/\n", prefix); return 1; }

    fprintf(oh, "/* GENERATED by tools/bake_font_dsdf.c - do not edit. */\n");
    fprintf(oh, "#ifndef FONT_ATLAS_DSDF_H\n#define FONT_ATLAS_DSDF_H\n");
    fprintf(oh, "#include <stdint.h>\n");
    fprintf(oh, "#define AME_DSDF_ATLAS_W %d\n", ATLAS_W);
    fprintf(oh, "#define AME_DSDF_ATLAS_H %d\n", atlas_h);
    fprintf(oh, "#define AME_DSDF_RANGE %.1ff\n", RANGE);
    /* vertical metrics at BASE_PX (same formula as bake_font.c) */
    fprintf(oh, "#define AME_DSDF_ASCENT %.1ff\n",
            (float)ascent * scale);
    fprintf(oh, "#define AME_DSDF_LINE_H %.1ff\n",
            (float)ascent * scale + (float)(-descent) * scale
                + (float)line_gap * scale + BASE_PX * 0.25f); /* bake_font
                * formula: same leading so line math is shared */
    fprintf(oh, "typedef struct {\n    uint32_t cp;\n"
                "    uint16_t ax, ay, aw, ah;\n"
                "    int16_t xoff, yoff;\n    float advance;\n"
                "} ame_dsdf_glyph;\n");
    fprintf(oh, "extern const unsigned char ame_dsdf_atlas[%d];\n",
            ATLAS_W * atlas_h * 4);
    fprintf(oh, "extern const ame_dsdf_glyph ame_dsdf_glyphs[];\n");
    fprintf(oh, "extern const int ame_dsdf_glyph_count;\n");
    fprintf(oh, "#endif\n");

    fprintf(oc, "#include \"font_atlas_dsdf.h\"\n");
    fprintf(oc, "const unsigned char ame_dsdf_atlas[] = {\n");
    for (int i = 0; i < ATLAS_W * atlas_h * 4; i++) {
        if (i % 32 == 0)
            fprintf(oc, "\n");
        fprintf(oc, "%d,", atlas[i]);
    }
    fprintf(oc, "\n};\n");
    fprintf(oc, "const ame_dsdf_glyph ame_dsdf_glyphs[] = {\n");
    for (int i = 0; i < ng; i++)
        fprintf(oc, "    { 0x%X, %d,%d,%d,%d, %d,%d, %.2ff },\n",
                glyphs[i].cp, glyphs[i].ax, glyphs[i].ay, glyphs[i].aw,
                glyphs[i].ah, glyphs[i].xoff, glyphs[i].yoff,
                glyphs[i].advance);
    fprintf(oc, "};\nconst int ame_dsdf_glyph_count = %d;\n", ng);
    fclose(oc);
    fclose(oh);
    free(atlas);
    free(glyphs);
    free(cell_d);
    free(data);
    printf("baked %d DSDF glyphs (%dx%d RGBA, range %.0f) -> "
           "font_atlas_dsdf.c/h\n", ng, ATLAS_W, atlas_h, RANGE);
    return 0;
}
