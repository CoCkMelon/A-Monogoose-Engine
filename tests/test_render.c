/* tests — golden render under headless GL (EGL surfaceless + llvmpipe).
 * No SDL window: the GL loader is injected straight into the render pipeline
 * (rp_set_gl_loader). Draws a known scene (ortho 2D sprites + 3D persp quad
 * + text), reads pixels back, and checks:
 *   - the same scene hashes identically across two frames (determinism)
 *   - center pixels differ from clear color (something actually drew)
 * Also writes render_golden.png for human/screenshot verification loop.
 */
#include "utest.h"
#include <ame/ame.h>
#include <ame/camera.h>
#include <ame/math.h>
#include <ame/render.h>
#include <ame/text.h>

#include <EGL/egl.h>
#include <GL/glcorearb.h>
#include <stdio.h>
#include <string.h>

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

#define W 256
#define H 160

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

    UT_CASE("output not the clear color (something drew)");
    static uint8_t px[W * H * 4];
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

    rp_shutdown();
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);

    UT_OK();
    return ut_done("test_render");
}
