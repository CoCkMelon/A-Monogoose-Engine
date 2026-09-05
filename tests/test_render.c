/* tests — golden render under headless GL (EGL surfaceless + llvmpipe).
 * No SDL window: the GL loader is injected straight into the render pipeline
 * (rp_set_gl_loader). Draws a known scene (ortho 2D sprites + 3D persp quad
 * + text), reads pixels back, and checks:
 *   - the same scene hashes identically across two frames (determinism)
 *   - center pixels differ from clear color (something actually drew)
 * Also writes render_golden.png for human/screenshot verification loop.
 */
#include "utest.h"
#include "ame/particles.h"
#include <ame/ame.h>
#include <ame/camera.h>
#include <ame/math.h>
#include <ame/render.h>
#include <ame/text.h>
#include "font_atlas.h" /* generated: ground truth for orientation test */

#include <EGL/egl.h>
#include <GL/glcorearb.h>
#include <stdio.h>
#include <string.h>

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

#define W 256
#define H 160

static uint8_t px[W * H * 4]; /* readback buffer (shared by the cases) */

/* find the bbox of bright pixels; returns false if none */
static bool bright_bbox(int *minx, int *maxx, int *miny, int *maxy) {
    *minx = W; *maxx = -1; *miny = H; *maxy = -1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (px[(y * W + x) * 4] > 120) {
                if (x < *minx) *minx = x; if (x > *maxx) *maxx = x;
                if (y < *miny) *miny = y; if (y > *maxy) *maxy = y;
            }
    return *maxx >= *minx && *maxy >= *miny;
}

static const ame_font_glyph *find_glyph(uint32_t cp) {
    for (int i = 0; i < ame_font_glyph_count; i++)
        if (ame_font_glyphs[i].cp == cp)
            return &ame_font_glyphs[i];
    return NULL;
}

/* SSD of the RENDERED glyph vs the atlas bitmap, direct and h-flipped.
 * ox,oy = expected cell origin in screen px; sx,sy = px per glyph-cell
 * pixel (computed EXACTLY per draw path - bbox inference drifts). */
static void f_ssd(const ame_font_glyph *g, float ox, float oy, float sx,
                  float sy, double *direct, double *flip) {
    double sd = 0, sf = 0;
    for (int yy = 0; yy < g->ah; yy++)
        for (int xx = 0; xx < g->aw; xx++) {
            float cov = ame_font_atlas_a8[(g->ay + yy) * AME_FONT_ATLAS_WIDTH
                                          + g->ax + xx] / 255.0f;
            int ry  = (int)(oy + yy * sy);
            int rxd = (int)(ox + xx * sx);
            int rxf = (int)(ox + (g->aw - 1 - xx) * sx);
            if (ry < 0 || ry >= H || rxd >= W || rxf < 0 || rxf >= W)
                continue;
            float rd = px[(ry * W + rxd) * 4] / 255.0f;
            float rf = px[(ry * W + rxf) * 4] / 255.0f;
            sd += (rd - cov) * (rd - cov);
            sf += (rf - cov) * (rf - cov);
        }
    *direct = sd;
    *flip = sf;
}

/* project a world point through a camera into screen px (y down) */
static void proj3(const ame_camera *c, ame_v3 p, float *sx, float *sy) {
    float x = c->vp.m[0]*p.x + c->vp.m[4]*p.y + c->vp.m[8]*p.z + c->vp.m[12];
    float y = c->vp.m[1]*p.x + c->vp.m[5]*p.y + c->vp.m[9]*p.z + c->vp.m[13];
    float w = c->vp.m[3]*p.x + c->vp.m[7]*p.y + c->vp.m[11]*p.z + c->vp.m[15];
    *sx = (x / w * 0.5f + 0.5f) * (float)c->vw;
    *sy = (1.0f - (y / w * 0.5f + 0.5f)) * (float)c->vh;
}

/* FBO: EGL surfaceless has NO default framebuffer - render into a texture */
static PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers_;
static PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer_;
static PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D_;
static PFNGLGENRENDERBUFFERSPROC glGenRenderbuffers_;
static PFNGLBINDRENDERBUFFERPROC glBindRenderbuffer_;
static PFNGLRENDERBUFFERSTORAGEPROC glRenderbufferStorage_;
static PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer_;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus_;

static bool make_fbo(int w, int h, GLuint *fbo_out) {
    glGenFramebuffers_ = (void *)eglGetProcAddress("glGenFramebuffers");
    glBindFramebuffer_ = (void *)eglGetProcAddress("glBindFramebuffer");
    glFramebufferTexture2D_ = (void *)eglGetProcAddress("glFramebufferTexture2D");
    glGenRenderbuffers_ = (void *)eglGetProcAddress("glGenRenderbuffers");
    glBindRenderbuffer_ = (void *)eglGetProcAddress("glBindRenderbuffer");
    glRenderbufferStorage_ = (void *)eglGetProcAddress("glRenderbufferStorage");
    glFramebufferRenderbuffer_ = (void *)eglGetProcAddress("glFramebufferRenderbuffer");
    glCheckFramebufferStatus_ = (void *)eglGetProcAddress("glCheckFramebufferStatus");
    if (!glGenFramebuffers_ || !glBindFramebuffer_ || !glFramebufferTexture2D_
        || !glGenRenderbuffers_ || !glBindRenderbuffer_ || !glRenderbufferStorage_
        || !glFramebufferRenderbuffer_ || !glCheckFramebufferStatus_)
        return false;
    PFNGLGENTEXTURESPROC glGenTextures_ =
        (void *)eglGetProcAddress("glGenTextures");
    PFNGLBINDTEXTUREPROC glBindTexture_ =
        (void *)eglGetProcAddress("glBindTexture");
    PFNGLTEXIMAGE2DPROC glTexImage2D_ =
        (void *)eglGetProcAddress("glTexImage2D");
    GLuint fbo, color, depth;
    glGenFramebuffers_(1, &fbo);
    glGenTextures_(1, &color);
    glGenRenderbuffers_(1, &depth);
    glBindTexture_(GL_TEXTURE_2D, color);
    glTexImage2D_(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindRenderbuffer_(GL_RENDERBUFFER, depth);
    glRenderbufferStorage_(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glBindFramebuffer_(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D_(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_2D, color, 0);
    glFramebufferRenderbuffer_(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                               GL_RENDERBUFFER, depth);
    return glCheckFramebufferStatus_(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

static void *egl_proc(const char *name) {
    return (void *)eglGetProcAddress(name);
}

static uint32_t hash_frame(void) {
    static uint8_t px[W * H * 4];
    rp_read_pixels(px, W, H);
    return ame_fnv1a(2166136261u, px, sizeof px);
}

/* PNG writer via stb_image_write (third_party, this one test TU) */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

int main(void) {
    printf("=== test_render (headless GL) ===\n");

    UT_CASE("EGL surfaceless context");
    EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                                           EGL_DEFAULT_DISPLAY, NULL);
    UT_ASSERTF(dpy != EGL_NO_DISPLAY, "no surfaceless EGL display");
    UT_ASSERT(eglInitialize(dpy, NULL, NULL));
    eglBindAPI(EGL_OPENGL_API);
    EGLint cfgattr[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE };
    EGLConfig cfg;
    EGLint n = 0;
    UT_ASSERTF(eglChooseConfig(dpy, cfgattr, &cfg, 1, &n) && n > 0,
               "no GL config (n=%d) - is llvmpipe/mesa installed?", n);
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);
    UT_ASSERTF(ctx != EGL_NO_CONTEXT, "ctx failed egl_err=0x%x", eglGetError());
    UT_ASSERT(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx));

    /* --- 3D camera scene: one card-like quad + text world --------------- */
    UT_CASE("render pipeline init (fluent desc)");
    ame_camera cam3;
    camera_viewport(camera_pos(camera_persp3d(camera_desc(&cam3)),
                               0, 3.2f, 3.4f), W, H);
    camera_look(&cam3, 0, 0, 0);
    camera_fov_deg(&cam3, 50.0f);
    camera_depth_range(&cam3, 0.1f, 100.0f);
    camera_build(&cam3);

    UT_ASSERTF(make_fbo(W, H, &(GLuint){0}),
               "FBO creation failed (no default fb in surfaceless EGL)");

    rp_set_gl_loader(egl_proc);
    ame_rp_desc d;
    int rc = rp_init(rp_desc_clear(rp_desc_begin(&d), 0.10f, 0.12f, 0.16f, 1.0f),
                     &cam3, W, H);
    UT_ASSERTF(rc == 0, "rp_init rc=%d (renderer=%s)", rc, rp_gl_renderer());
    printf("    GL_RENDERER = %s\n", rp_gl_renderer());

    UT_CASE("single pass: 3D quad + world text draws");
    ame_text_layout hello;
    UT_ASSERT(text_init(true) >= 0);
    int ng = text_layout("Hello ame-next", 0, AME_TEXT_ALIGN_C, 1.0f, &hello);
    UT_ASSERT(ng > 0);

    /* world text LYING ON THE TABLE (like card faces): layout px -> units */
    const float ts = 2.0f / (float)text_font_px(); /* ~2-unit tall glyphs */
    float pose[16] = {
        ts, 0,  0,  0,      /* layout +x -> world +x */
        0,  0,  ts, 0,      /* layout +y (down) -> world +z (toward camera) */
        0,  1,  0,  0,
        -hello.w * ts * 0.5f, 0.012f, -hello.h * ts * 0.5f, 1,
    };
    float tint_w[4] = { 1, 1, 1, 1 };

    rp_begin_frame();
    /* table */
    float t0[3] = { -3, 0, -3 }, t1[3] = { 3, 0, -3 };
    float t2[3] = { 3, 0, 3 }, t3[3] = { -3, 0, 3 };
    float ttint[4] = { 0.2f, 0.24f, 0.32f, 1 };
    rp_push_quad(rp_white_texture(), t0, t1, t2, t3, 0, 0, 1, 1, ttint, 0);
    /* two "cards" */
    float c0[4][3] = { {-0.9f,0.01f,-0.5f},{-0.1f,0.01f,-0.5f},{-0.1f,0.01f,0.3f},{-0.9f,0.01f,0.3f} };
    float c1[4][3] = { { 0.1f,0.01f,-0.5f},{0.9f,0.01f,-0.5f},{0.9f,0.01f,0.3f},{0.1f,0.01f,0.3f} };
    float red[4] = { 0.9f, 0.3f, 0.3f, 1 }, blue[4] = { 0.3f, 0.5f, 0.95f, 1 };
    rp_push_quad(rp_white_texture(), c0[0], c0[1], c0[2], c0[3], 0, 0, 1, 1, red, 10);
    rp_push_quad(rp_white_texture(), c1[0], c1[1], c1[2], c1[3], 0, 0, 1, 1, blue, 10);
    text_draw_world(&hello, pose, tint_w, 20);
    rp_end_frame();
    uint32_t h1 = hash_frame();
    printf("    frame1 hash=0x%08x draws=%d quads=%d\n", h1,
           rp_draw_calls_last_frame(), rp_quads_last_frame());
    UT_ASSERT(rp_quads_last_frame() > 3);
    UT_ASSERT(rp_draw_calls_last_frame() <= 3); /* grouped by texture (2) */

    UT_CASE("deterministic: second frame hashes identically");
    rp_begin_frame();
    rp_push_quad(rp_white_texture(), t0, t1, t2, t3, 0, 0, 1, 1, ttint, 0);
    rp_push_quad(rp_white_texture(), c0[0], c0[1], c0[2], c0[3], 0, 0, 1, 1, red, 10);
    rp_push_quad(rp_white_texture(), c1[0], c1[1], c1[2], c1[3], 0, 0, 1, 1, blue, 10);
    text_draw_world(&hello, pose, tint_w, 20);
    rp_end_frame();
    uint32_t h2 = hash_frame();
    UT_ASSERT(h1 == h2);

    UT_CASE("Stage 2 lighting: lit differs by facing; unlit unchanged");
    {
        /* baseline: unlit frame AFTER setting lights must equal the
         * pre-lighting hash (lit=0 ignores the uniforms entirely) */
        float ldir[3] = { 0, -1, 0 }, lcol[3] = { 1, 1, 1 };
        float lamb[3] = { 0.1f, 0.1f, 0.1f };
        rp_lighting(ldir, lcol, lamb);
        rp_begin_frame();
        rp_push_quad(rp_white_texture(), t0, t1, t2, t3, 0, 0, 1, 1,
                     ttint, 0);
        rp_push_quad(rp_white_texture(), c0[0], c0[1], c0[2], c0[3], 0, 0,
                     1, 1, red, 10);
        rp_push_quad(rp_white_texture(), c1[0], c1[1], c1[2], c1[3], 0, 0,
                     1, 1, blue, 10);
        text_draw_world(&hello, pose, tint_w, 20);
        rp_end_frame();
        UT_ASSERT(hash_frame() == h1); /* unlit: byte-identical */

        long lum(bool face_up) {
            rp_begin_frame();
            rp_push_quad(rp_white_texture(), t0, t1, t2, t3, 0, 0, 1, 1,
                         ttint, 0);
            rp_set_lit(1);
            rp_set_normal(0, face_up ? 1.0f : -1.0f, 0);
            rp_push_quad(rp_white_texture(), c0[0], c0[1], c0[2], c0[3],
                         0, 0, 1, 1, red, 10);
            rp_set_lit(0);
            rp_push_quad(rp_white_texture(), c1[0], c1[1], c1[2], c1[3],
                         0, 0, 1, 1, blue, 10);
            text_draw_world(&hello, pose, tint_w, 20);
            rp_end_frame();
            rp_read_pixels(px, W, H);
            long sum = 0;
            for (int i = 0; i < W * H * 4; i += 4)
                sum += px[i] + px[i + 1] + px[i + 2];
            return sum;
        }
        long up = lum(true), down = lum(false);
        printf("    lum facing=%ld away=%ld\n", up, down);
        UT_ASSERTF(up > down, "facing light must be brighter (%ld vs %ld)",
                   up, down);
        /* the delta is the red card's ~330px flipping from ~full tint
         * (~420/px) to ambient (~39/px): >> any rounding noise */
        UT_ASSERTF(up - down > 50000L,
                   "delta must reflect the lit card (%ld)", up - down);
        rp_lighting_off();
    }

    UT_CASE("Stage 2 shadow: caster darkens ground; outside bit-exact");
    {
        float ldir[3] = { 0, -1, 0 }, lcol[3] = { 1, 1, 1 };
        float lamb[3] = { 0.15f, 0.15f, 0.15f };
        rp_lighting(ldir, lcol, lamb);
        float gtint[4] = { 0.55f, 0.55f, 0.55f, 1 };
        float g0[3] = { -4, 0, -4 }, g1[3] = { 4, 0, -4 };
        float g2[3] = { 4, 0, 4 }, g3[3] = { -4, 0, 4 };
        float cs[4][3] = { { 0.0f, 1.2f, -0.1f }, { 1.0f, 1.2f, -0.1f },
                           { 1.0f, 1.2f, 0.9f }, { 0.0f, 1.2f, 0.9f } };
        float ctint[4] = { 0.8f, 0.7f, 0.3f, 1 };
        /* sample under the caster (shadowed) and at a far corner (not) */
        float ux, uy, fx, fy;
        proj3(&cam3, ame_v3_(0.5f, 0.0f, 0.4f), &ux, &uy);
        proj3(&cam3, ame_v3_(-3.2f, 0.0f, -3.2f), &fx, &fy);
        int iu = (int)ux, iuy = (int)uy, ifx = (int)fx, ify = (int)fy;
#define RP_LUM(X, Y)                                                          \
    ((long)px[((Y) * W + (X)) * 4] + px[((Y) * W + (X)) * 4 + 1] +            \
        px[((Y) * W + (X)) * 4 + 2])
        rp_begin_frame();
        rp_set_lit(1);
        rp_set_normal(0, 1, 0);
        rp_push_quad(rp_white_texture(), g0, g1, g2, g3, 0, 0, 1, 1, gtint, 0);
        rp_push_quad(rp_white_texture(), cs[0], cs[1], cs[2], cs[3],
                     0, 0, 1, 1, ctint, 10);
        rp_set_lit(0);
        rp_end_frame();
        rp_read_pixels(px, W, H);
        long a_under = RP_LUM(iu, iuy), a_far = RP_LUM(ifx, ify);

        /* same scene WITH the shadow pass (light travels straight down) */
        rp_shadow(ldir, (float[3]){ 0, 0, 0 }, 5.0f);
        rp_begin_frame();
        rp_set_lit(1);
        rp_set_normal(0, 1, 0);
        rp_push_quad(rp_white_texture(), g0, g1, g2, g3, 0, 0, 1, 1, gtint, 0);
        rp_push_quad(rp_white_texture(), cs[0], cs[1], cs[2], cs[3],
                     0, 0, 1, 1, ctint, 10);
        rp_set_lit(0);
        rp_end_frame();
        uint32_t hs1 = hash_frame();
        rp_read_pixels(px, W, H);
        long b_under = RP_LUM(iu, iuy), b_far = RP_LUM(ifx, ify);
        rp_begin_frame();
        rp_set_lit(1);
        rp_set_normal(0, 1, 0);
        rp_push_quad(rp_white_texture(), g0, g1, g2, g3, 0, 0, 1, 1, gtint, 0);
        rp_push_quad(rp_white_texture(), cs[0], cs[1], cs[2], cs[3],
                     0, 0, 1, 1, ctint, 10);
        rp_set_lit(0);
        rp_end_frame();
        uint32_t hs2 = hash_frame();
#undef RP_LUM
        UT_ASSERTF(b_under < a_under - 40L,
                   "shadowed ground must darken (%ld -> %ld)", a_under,
                   b_under);
        UT_ASSERTF(b_far == a_far,
                   "outside the shadow must stay bit-exact (%ld vs %ld)",
                   a_far, b_far);
        UT_ASSERT(hs1 == hs2); /* deterministic shadow render */
        printf("    shadow: ground %ld -> %ld, outside %ld == %ld,"
               " hash 0x%08x\n",
               a_under, b_under, a_far, b_far, hs1);
        rp_shadow_off();
        rp_lighting_off();
    }

    UT_CASE("Stage 2 post: offscreen round trip is pixel-exact");
    {
        rp_shutdown();
        int rc = rp_init(
            rp_desc_post(
                rp_desc_clear(rp_desc_begin(&d), 0.10f, 0.12f, 0.16f, 1.0f),
                true),
            &cam3, W, H);
        UT_ASSERTF(rc == 0, "post rp_init rc=%d", rc);
        UT_ASSERT(text_init(true) >= 0); /* re-register the atlas */
        rp_begin_frame();
        rp_push_quad(rp_white_texture(), t0, t1, t2, t3, 0, 0, 1, 1, ttint, 0);
        rp_push_quad(rp_white_texture(), c0[0], c0[1], c0[2], c0[3], 0, 0, 1, 1,
                     red, 10);
        rp_push_quad(rp_white_texture(), c1[0], c1[1], c1[2], c1[3], 0, 0, 1, 1,
                     blue, 10);
        text_draw_world(&hello, pose, tint_w, 20);
        rp_end_frame();
        UT_ASSERTF(hash_frame() == h1,
                   "identity post (tint=1,vig=0) must equal the direct path");

        /* resize the target away and back: resources recreate, no leak,
         * image unchanged at the original size */
        rp_viewport(W / 2, H / 2);
        rp_begin_frame();
        rp_end_frame();
        rp_viewport(W, H);
        rp_begin_frame();
        rp_push_quad(rp_white_texture(), t0, t1, t2, t3, 0, 0, 1, 1, ttint, 0);
        rp_push_quad(rp_white_texture(), c0[0], c0[1], c0[2], c0[3], 0, 0, 1, 1,
                     red, 10);
        rp_push_quad(rp_white_texture(), c1[0], c1[1], c1[2], c1[3], 0, 0, 1, 1,
                     blue, 10);
        text_draw_world(&hello, pose, tint_w, 20);
        rp_end_frame();
        UT_ASSERTF(hash_frame() == h1, "resize cycle must not change pixels");

        /* vignette: corners darken, the center is untouched */
        rp_post_vignette(0.45f);
        rp_begin_frame();
        rp_push_quad(rp_white_texture(), t0, t1, t2, t3, 0, 0, 1, 1, ttint, 0);
        rp_push_quad(rp_white_texture(), c0[0], c0[1], c0[2], c0[3], 0, 0, 1, 1,
                     red, 10);
        rp_push_quad(rp_white_texture(), c1[0], c1[1], c1[2], c1[3], 0, 0, 1, 1,
                     blue, 10);
        text_draw_world(&hello, pose, tint_w, 20);
        rp_end_frame();
        rp_read_pixels(px, W, H);
        int cc = ((H / 2) * W + W / 2) * 4;          /* center */
        int corner = (2 * W + 2) * 4;                /* top-left  */
        long l_c = px[cc] + px[cc + 1] + px[cc + 2];
        long l_k = px[corner] + px[corner + 1] + px[corner + 2];
        printf("    vignette center=%ld corner=%ld\n", l_c, l_k);
        UT_ASSERTF(l_k < l_c, "corner (%ld) must be darker than center (%ld)",
                   l_k, l_c);
        rp_post_vignette(0);
        rp_shutdown();
        rc = rp_init(
            rp_desc_clear(rp_desc_begin(&d), 0.10f, 0.12f, 0.16f, 1.0f),
            &cam3, W, H);
        UT_ASSERTF(rc == 0, "restore direct rp_init rc=%d", rc);
        UT_ASSERT(text_init(true) >= 0);
    }

    UT_CASE("output not the clear color (something drew)");
    rp_read_pixels(px, W, H);
    /* project the red card CENTER through the camera to find its pixel */
    ame_v4 c = { 0 };
    ame_v3 world = ame_v3_(-0.5f, 0.01f, -0.1f); /* red card center */
    ame_m4 vp = cam3.vp;
    c.x = vp.m[0] * world.x + vp.m[4] * world.y + vp.m[8] * world.z + vp.m[12];
    c.y = vp.m[1] * world.x + vp.m[5] * world.y + vp.m[9] * world.z + vp.m[13];
    c.w = vp.m[3] * world.x + vp.m[7] * world.y + vp.m[11] * world.z + vp.m[15];
    int sx = (int)(((c.x / c.w) + 1.0f) * 0.5f * W);
    int sy = (int)((1.0f - ((c.y / c.w) + 1.0f) * 0.5f) * H);
    sx = sx < 0 ? 0 : sx >= W ? W - 1 : sx;
    sy = sy < 0 ? 0 : sy >= H ? H - 1 : sy;
    uint8_t r = px[(sy * W + sx) * 4], g = px[(sy * W + sx) * 4 + 1];
    UT_ASSERTF(r > g, "expected reddish pixel at (%d,%d), got %d,%d,%d",
               sx, sy, r, g, px[(sy * W + sx) * 4 + 2]);

    long bright = 0;
    for (long i = 0; i < W * H; i++)
        if (px[i * 4] > 200 && px[i * 4 + 1] > 200 && px[i * 4 + 2] > 200)
            bright++;
    printf("    bright(white text) pixels: %ld\n", bright);
    UT_ASSERTF(bright > 30, "world text not visible (bright=%ld)", bright);

    stbi_write_png("render_golden.png", W, H, 4, px, W * 4);
    printf("    wrote render_golden.png\n");

    /* --- ground-truth ORIENTATION check ---------------------------------
     * Render glyph 'F' (strongly asymmetric) big on an ortho screen and
     * correlate the pixels with the atlas bitmap: direct must beat the
     * horizontally-flipped hypothesis. Catches any L-R mirroring. */
    UT_CASE("particles survive a straight-down camera (basis guard)");
    {
        ame_camera down;
        camera_viewport(camera_pos(camera_persp3d(camera_desc(&down)),
                                   0, 4, 0), W, H);
        camera_look(&down, 0, 0, 0); /* f = (0,-1,0) PARALLEL to up */
        camera_fov_deg(&down, 50.0f);
        camera_depth_range(&down, 0.1f, 100.0f);
        camera_build(&down);
        rp_set_camera(&down);
        ame_particles *pt = &(ame_particles){ 0 };
        pt_reset(pt);
        uint8_t c0[4] = { 255, 255, 255, 255 }, c1[4] = { 255, 255, 255, 0 };
        for (int i = 0; i < 24; i++)
            pt_spawn(pt, -0.6f + 0.1f * i, 0.5f + 0.02f * i, 0, 0, 0, 0,
                     1.0f, 0.14f, 0.14f, c0, c1);
        rp_begin_frame();
        pt_draw(pt, &down, rp_white_texture(), 30);
        rp_end_frame();
        rp_read_pixels(px, W, H);
        int bright = 0;
        for (long i = 0; i < W * H; i++)
            if (px[i * 4] > 200 && px[i * 4 + 1] > 200
                && px[i * 4 + 2] > 200)
                bright++;
        printf("    straight-down camera: %d bright px\n", bright);
        UT_ASSERTF(bright > 100, "billboards vanished (degenerate basis)");
        rp_set_camera(&cam3); /* restore */
    }

    UT_CASE("Stage 2 mesh: Assimp-baked cube under world matrices");
    {
#include "assets/baked_cube.h"
        rp_set_camera(&cam3);
        /* identity + translated cube on the same batch, lit normals */
        float id[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        float mv[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0,
                         -1.6f, 0.9f, 0, 1 }; /* column-major */
        float tint[4] = { 0.8f, 0.7f, 0.95f, 1 };
        rp_begin_frame();
        rp_set_lit(1);
        int tris = rp_push_mesh(rp_white_texture(),
                                (const ame_mesh_vert *)baked_cube_verts,
                                baked_cube_vert_count, baked_cube_idx,
                                baked_cube_idx_count, id, tint, 5);
        int tris2 = rp_push_mesh(rp_white_texture(),
                                 (const ame_mesh_vert *)baked_cube_verts,
                                 baked_cube_vert_count, baked_cube_idx,
                                 baked_cube_idx_count, mv, tint, 5);
        rp_set_lit(0);
        rp_end_frame();
        UT_ASSERTF(tris == baked_cube_idx_count / 3 && tris2 == tris,
                   "one quad per tri (%d %d)", tris, tris2);
        UT_ASSERTF(rp_quads_last_frame() == 2 * tris, "batch holds both");
        UT_ASSERT(rp_draw_calls_last_frame() <= 2);
        uint32_t mh = hash_frame();
        /* determinism: identical draw -> identical frame */
        rp_begin_frame();
        rp_set_lit(1);
        rp_push_mesh(rp_white_texture(),
                     (const ame_mesh_vert *)baked_cube_verts,
                     baked_cube_vert_count, baked_cube_idx,
                     baked_cube_idx_count, id, tint, 5);
        rp_push_mesh(rp_white_texture(),
                     (const ame_mesh_vert *)baked_cube_verts,
                     baked_cube_vert_count, baked_cube_idx,
                     baked_cube_idx_count, mv, tint, 5);
        rp_set_lit(0);
        rp_end_frame();
        UT_ASSERTF(hash_frame() == mh, "mesh frame must be stable");
        printf("    baked cube: %d verts %d tris x2, stable hash 0x%08x\n",
               baked_cube_vert_count, tris, mh);
    }

    UT_CASE("Stage 2 particles: billboards batch, fade, expire");
    {
        rp_set_camera(&cam3); /* back to 3D after the later camera cases */
        ame_particles *pt = &(ame_particles){ 0 };
        pt_reset(pt);
        uint8_t c0[4] = { 255, 200, 90, 255 }, c1[4] = { 255, 60, 30, 0 };
        for (int i = 0; i < 64; i++) /* deterministic fan */
            pt_spawn(pt, -1.5f + 3.0f * (i / 64.0f), 0.6f, -0.4f,
                     0.2f * (i % 5 - 2), 0.8f + 0.02f * i, 0.1f * (i % 3),
                     0.5f + 0.01f * i, 0.10f, 0.02f, c0, c1);
        rp_begin_frame();
        int drawn = pt_draw(pt, &cam3, rp_white_texture(), 30);
        rp_end_frame();
        UT_ASSERTF(drawn == 64 && rp_quads_last_frame() == 64,
                   "one quad per particle (drawn=%d quads=%d)", drawn,
                   rp_quads_last_frame());
        uint32_t pa = hash_frame();
        /* re-draw same state: identical batch => identical frame */
        rp_begin_frame();
        pt_draw(pt, &cam3, rp_white_texture(), 30);
        rp_end_frame();
        UT_ASSERTF(hash_frame() == pa, "particle frame must be stable");
        /* step in lockstep twice: same trajectory => same hash */
        for (int k = 0; k < 30; k++)
            pt_step(pt, 1.0f / 60.0f, 0, -1.7f, 0, 0.12f);
        rp_begin_frame();
        pt_draw(pt, &cam3, rp_white_texture(), 30);
        rp_end_frame();
        uint32_t pb = hash_frame();
        ame_particles qt = *pt;
        for (int k = 0; k < 30; k++) {
            pt_step(pt, 1.0f / 60.0f, 0, -1.7f, 0, 0.12f);
            pt_step(&qt, 1.0f / 60.0f, 0, -1.7f, 0, 0.12f);
        }
        UT_ASSERT(pt->count == qt.count);
        rp_begin_frame();
        int d2 = pt_draw(pt, &cam3, rp_white_texture(), 30);
        rp_end_frame();
        UT_ASSERTF(d2 == pt->count && d2 < 64,
                   "particles expired (alive=%d)", d2);
        (void)pb;
        /* all dead eventually */
        for (int k = 0; k < 240; k++)
            pt_step(pt, 1.0f / 60.0f, 0, -1.7f, 0, 0.12f);
        UT_ASSERT(pt->count == 0);
        rp_begin_frame();
        rp_end_frame();
        UT_ASSERT(rp_quads_last_frame() == 0);
    }

    UT_CASE("text orientation matches atlas (SCREEN path)");
    {
        ame_camera cam2;
        camera_viewport(camera_ortho2d(camera_desc(&cam2)), W, H);
        camera_build(&cam2);
        rp_set_camera(&cam2);
        ame_text_layout f;
        text_layout("F", 0, AME_TEXT_ALIGN_L, 4.0f, &f);
        rp_begin_frame();
        text_draw_screen(&f, 40, 40, (float[4]){ 1, 1, 1, 1 }, 10);
        rp_end_frame();
        rp_read_pixels(px, W, H);
        const ame_font_glyph *fg = find_glyph('F');
        UT_ASSERT(fg != NULL);
        double sd, sf;
        f_ssd(fg, 40.0f + fg->xoff * 4.0f,
              40.0f + (AME_FONT_ASCENT + fg->yoff) * 4.0f, 4.0f, 4.0f, &sd, &sf);
        printf("    F screen  ssd direct=%.1f flipped=%.1f\n", sd, sf);
        UT_ASSERTF(sd < sf * 0.6, "SCREEN text mirrored (%.1f vs %.1f)", sd, sf);
    }

    UT_CASE("text orientation matches atlas (WORLD path, identity pose)");
    {
        ame_camera cam2;
        camera_viewport(camera_ortho2d(camera_desc(&cam2)), W, H);
        camera_build(&cam2);
        rp_set_camera(&cam2);
        ame_text_layout f;
        text_layout("F", 0, AME_TEXT_ALIGN_L, 1.0f, &f);
        /* raw glyph px x4 via the pose; the ortho camera centers world 0,0,
         * so anchor at the view's top-left (rp_screen_origin) to land at
         * true screen (40,40) */
        float oxx, oyy;
        rp_screen_origin(&oxx, &oyy);
        float pose[16] = { 4,0,0,0,  0,4,0,0,  0,0,4,0,
                           40 + oxx, 40 + oyy, 0, 1 };
        rp_begin_frame();
        text_draw_world(&f, pose, (float[4]){ 1, 1, 1, 1 }, 10);
        rp_end_frame();
        rp_read_pixels(px, W, H);
        const ame_font_glyph *fg = find_glyph('F');
        double sd, sf;
        f_ssd(fg, 40.0f + fg->xoff * 4.0f,
              40.0f + (AME_FONT_ASCENT + fg->yoff) * 4.0f, 4.0f, 4.0f, &sd, &sf);
        printf("    F world   ssd direct=%.1f flipped=%.1f\n", sd, sf);
        UT_ASSERTF(sd < sf * 0.6, "WORLD text mirrored (%.1f vs %.1f)", sd, sf);
    }

    UT_CASE("text orientation matches atlas (BILLBOARD pose, 3D camera)");
    {
        /* the exact pose construction the game's scoreboard uses:
         * col0 = camera right, col1 = -camera up (layout y-down) */
        ame_camera camF;
        camera_viewport(camera_pos(camera_persp3d(camera_desc(&camF)),
                                   0, 0, 5), W, H);
        camera_look(&camF, 0, 0, 0);
        camera_fov_deg(&camF, 50.0f);
        camera_depth_range(&camF, 0.1f, 100.0f);
        camera_build(&camF);
        rp_set_camera(&camF);
        ame_v3 fw = ame_v3_norm(ame_v3_sub(camF.look, camF.pos));
        ame_v3 r  = ame_v3_norm(ame_v3_cross(fw, camF.up));
        ame_v3 u  = ame_v3_cross(r, fw);
        const float s = 0.10f;
        ame_v3 t = ame_v3_(-1.1f, 0.8f, 0); /* top-left anchor, world */
        float pose[16] = {
            r.x * s,  r.y * s,  r.z * s,  0,
           -u.x * s, -u.y * s, -u.z * s,  0,
            0, 0, 0, 0,
            t.x, t.y, t.z, 1,
        };
        ame_text_layout f;
        text_layout("F", 0, AME_TEXT_ALIGN_L, 1.0f, &f);
        rp_begin_frame();
        text_draw_world(&f, pose, (float[4]){ 1, 1, 1, 1 }, 10);
        rp_end_frame();
        rp_read_pixels(px, W, H);
        const ame_font_glyph *fg = find_glyph('F');
        float o_x, o_y, e_x, e_y;
        /* cell origin = line origin + xoff right - (ascent+yoff) down */
        ame_v3 cell_t = ame_v3_add(
            ame_v3_add(t, ame_v3_scale(r, s * fg->xoff)),
            ame_v3_scale(u, -s * (AME_FONT_ASCENT + fg->yoff)));
        proj3(&camF, cell_t, &o_x, &o_y);
        proj3(&camF, ame_v3_add(t, ame_v3_scale(r, s * fg->aw)), &e_x, &e_y);
        proj3(&camF, ame_v3_add(cell_t, ame_v3_scale(r, s * fg->aw)), &e_x, &e_y);
        float px_sx = e_x - o_x;
        proj3(&camF, ame_v3_sub(cell_t, ame_v3_scale(u, s * fg->ah)), &e_x, &e_y);
        float px_sy = e_y - o_y;
        double sd, sf;
        f_ssd(fg, o_x, o_y, px_sx / fg->aw, px_sy / fg->ah, &sd, &sf);
        int bbx0, bbx1, bby0, bby1;
        bool has_ink = bright_bbox(&bbx0, &bbx1, &bby0, &bby1);
        printf("    F billboard ssd direct=%.3f flipped=%.3f (cell %.1fx%.1f px "
               "origin %.1f,%.1f ink_bbox=%d %d..%d x %d..%d)\n",
               sd, sf, px_sx, px_sy, o_x, o_y, (int)has_ink, bbx0, bbx1, bby0, bby1);
        {   /* sample diagnostics: how many samples had ink? */
            int n_ink_cov = 0, n_ink_rd = 0;
            for (int yy = 0; yy < fg->ah; yy++)
                for (int xx = 0; xx < fg->aw; xx++) {
                    float cov = ame_font_atlas_a8[(fg->ay + yy) * AME_FONT_ATLAS_WIDTH
                                                  + fg->ax + xx] / 255.0f;
                    int ry = (int)(o_y + yy * (px_sy / fg->ah));
                    int rx = (int)(o_x + xx * (px_sx / fg->aw));
                    if (cov > 0.3f) n_ink_cov++;
                    if (ry >= 0 && ry < H && rx >= 0 && rx < W
                        && px[(ry * W + rx) * 4] > 90) n_ink_rd++;
                }
            printf("      diag: atlas ink cells=%d rendered ink samples=%d\n",
                   n_ink_cov, n_ink_rd);
        }
        UT_ASSERTF(sd < sf * 0.6, "BILLBOARD text mirrored (%.1f vs %.1f)",
                   sd, sf);
    }

    UT_CASE("camera aspect: square stays square after viewport change");
    {
        /* project a 2x2 world square at z=0 through c->vp into SCREEN px */
        #define PROJ_PX(c, X, Y, ox, oy) do { \
            ame_v3 w_ = ame_v3_(X, Y, 0); \
            float cx_ = (c)->vp.m[0]*w_.x + (c)->vp.m[4]*w_.y + (c)->vp.m[8]*w_.z + (c)->vp.m[12]; \
            float cy_ = (c)->vp.m[1]*w_.x + (c)->vp.m[5]*w_.y + (c)->vp.m[9]*w_.z + (c)->vp.m[13]; \
            float cw_ = (c)->vp.m[3]*w_.x + (c)->vp.m[7]*w_.y + (c)->vp.m[11]*w_.z + (c)->vp.m[15]; \
            *(ox) = (cx_ / cw_ * 0.5f + 0.5f) * (c)->vw; \
            *(oy) = (0.5f - cy_ / cw_ * 0.5f) * (c)->vh; \
        } while (0)
        ame_camera c;
        camera_viewport(camera_persp3d(camera_desc(&c)), 800, 600);
        camera_pos(&c, 0, 0, 5);
        camera_look(&c, 0, 0, 0);
        camera_fov_deg(&c, 50.0f);
        camera_depth_range(&c, 0.1f, 100.0f);
        camera_build(&c);
        float x0, y0, x1, y1;
        PROJ_PX(&c, -1, -1, &x0, &y0);
        PROJ_PX(&c,  1,  1, &x1, &y1);
        float w1 = fabsf(x1 - x0), h1 = fabsf(y1 - y0);
        UT_ASSERTF(fabsf(w1 / h1 - 1.0f) < 0.05f,
                   "square not square at 800x600 (ratio %.3f)", w1 / h1);
        /* edge point: off-right at 4:3 ... */
        float ex, ey;
        PROJ_PX(&c, 3.2f, 0, &ex, &ey);
        UT_ASSERTF(ex > 800.0f, "point should be off-right at 800x600 (%.0f)", ex);
        camera_viewport(&c, 1600, 600); /* 8:3 viewport, aspect must follow */
        camera_build(&c);
        PROJ_PX(&c, -1, -1, &x0, &y0);
        PROJ_PX(&c,  1,  1, &x1, &y1);
        float w2 = fabsf(x1 - x0), h2 = fabsf(y1 - y0);
        UT_ASSERTF(fabsf(w2 / h2 - 1.0f) < 0.05f,
                   "square stretched at 1600x600 (ratio %.3f) - stale aspect",
                   w2 / h2);
        UT_ASSERTF(fabsf(w2 - w1) < w1 * 0.05f && fabsf(h2 - h1) < h1 * 0.05f,
                   "px size should be height-driven, unchanged (%.0f->%.0f)",
                   w1, w2);
        /* ... but on-screen at 8:3 (field widened, not the square stretched) */
        PROJ_PX(&c, 3.2f, 0, &ex, &ey);
        UT_ASSERTF(ex < 1600.0f, "point should be visible at 1600x600 (%.0f)", ex);
        #undef PROJ_PX
    }
    rp_shutdown();
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);

    UT_OK();
    return ut_done("test_render");
}
