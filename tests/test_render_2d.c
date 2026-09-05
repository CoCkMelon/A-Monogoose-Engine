/* render.txt CHECK line, 2D build: "A 2D build renders 1000 sprites in
 * a handful of draw calls with a pixel-perfect camera; resize/
 * camera-follow keep pixels aligned." Also proves the tilemap module
 * (Tiled .tmj parity port) lands in the SAME single batch. */
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h> /* headers only; desktop GL via the loader */

#include "ame/ame.h"
#include "ame/audio.h"
#include "ame/camera.h"
#include "ame/render.h"
#include "ame/tilemap.h"
#include "utest.h"

#define W 320
#define H 240

static uint8_t px[W * H * 4];

static void *egl_proc(const char *name) {
    return (void *)eglGetProcAddress(name);
}

static uint32_t hash_frame(void) {
    rp_read_pixels(px, W, H);
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof px; i++) {
        h ^= px[i];
        h *= 16777619u;
    }
    return h;
}

/* a hand-made FBO (surfaceless EGL has no default framebuffer) */
static bool make_fbo(int w, int h) {
    PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers_ =
        (void *)eglGetProcAddress("glGenFramebuffers");
    PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer_ =
        (void *)eglGetProcAddress("glBindFramebuffer");
    PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D_ =
        (void *)eglGetProcAddress("glFramebufferTexture2D");
    PFNGLGENRENDERBUFFERSPROC glGenRenderbuffers_ =
        (void *)eglGetProcAddress("glGenRenderbuffers");
    PFNGLBINDRENDERBUFFERPROC glBindRenderbuffer_ =
        (void *)eglGetProcAddress("glBindRenderbuffer");
    PFNGLRENDERBUFFERSTORAGEPROC glRenderbufferStorage_ =
        (void *)eglGetProcAddress("glRenderbufferStorage");
    PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer_ =
        (void *)eglGetProcAddress("glFramebufferRenderbuffer");
    PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus_ =
        (void *)eglGetProcAddress("glCheckFramebufferStatus");
    PFNGLGENTEXTURESPROC glGenTextures_ =
        (void *)eglGetProcAddress("glGenTextures");
    PFNGLBINDTEXTUREPROC glBindTexture_ =
        (void *)eglGetProcAddress("glBindTexture");
    PFNGLTEXIMAGE2DPROC glTexImage2D_ =
        (void *)eglGetProcAddress("glTexImage2D");
    GLuint fbo, color;
    glGenFramebuffers_(1, &fbo);
    glGenTextures_(1, &color);
    glBindTexture_(GL_TEXTURE_2D, color);
    glTexImage2D_(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                  GL_UNSIGNED_BYTE, NULL);
    glGenRenderbuffers_(1, &(GLuint){ 0 });
    glBindRenderbuffer_(GL_RENDERBUFFER, 0);
    glBindFramebuffer_(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D_(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_2D, color, 0);
    (void)glGenRenderbuffers_;
    (void)glFramebufferRenderbuffer_;
    (void)glRenderbufferStorage_;
    return glCheckFramebufferStatus_(GL_FRAMEBUFFER)
           == GL_FRAMEBUFFER_COMPLETE;
}

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

/* write a tiny Tiled .tmj map: 8x6 tiles, gids 0..3 */
static bool write_test_map(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f)
        return false;
    fprintf(f, "{ \"type\":\"map\", \"orientation\":\"orthogonal\",");
    fprintf(f, " \"renderorder\":\"right-down\",");
    fprintf(f, " \"width\":8, \"height\":6,");
    fprintf(f, " \"tilewidth\":16, \"tileheight\":16,");
    fprintf(f, " \"tilesets\":[ { \"firstgid\":1, \"columns\":2,");
    fprintf(f, " \"tilecount\":4 } ],");
    fprintf(f, " \"layers\":[ { \"type\":\"tilelayer\",");
    fprintf(f, " \"width\":8, \"height\":6,");
    fprintf(f, " \"data\":[");
    for (int i = 0; i < 8 * 6; i++)
        fprintf(f, "%d%s", (i * 5) % 4, i + 1 < 8 * 6 ? "," : "");
    fprintf(f, " ] } ] }\n");
    fclose(f);
    return true;
}

int main(void) {
    printf("=== test_render_2d (headless GL, 2D build) ===\n");

    UT_CASE("EGL surfaceless context");
    EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                                           EGL_DEFAULT_DISPLAY, NULL);
    UT_ASSERTF(dpy != EGL_NO_DISPLAY, "no surfaceless EGL display");
    UT_ASSERT(eglInitialize(dpy, NULL, NULL));
    eglBindAPI(EGL_OPENGL_API);
    EGLint cfgattr[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8, EGL_NONE };
    EGLConfig cfg;
    EGLint n = 0;
    UT_ASSERTF(eglChooseConfig(dpy, cfgattr, &cfg, 1, &n) && n > 0,
               "no GL config (n=%d)", n);
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);
    UT_ASSERTF(ctx != EGL_NO_CONTEXT, "ctx failed egl_err=0x%x",
               eglGetError());
    UT_ASSERT(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx));
    UT_ASSERT(make_fbo(W, H));

    UT_CASE("tilemap .tmj parses (Tiled parity subset)");
    UT_ASSERT(write_test_map("/tmp/ame_test_map.tmj"));
    static ame_tilemap tm;
    char err[128];
    UT_ASSERTF(ame_tilemap_load_tmj("/tmp/ame_test_map.tmj", &tm, err,
                                    sizeof err),
               "load failed: %s", err);
    UT_ASSERT(tm.width == 8 && tm.height == 6);
    UT_ASSERT(tm.tile_width == 16 && tm.tile_height == 16);
    UT_ASSERT(tm.layer_count == 1);
    UT_ASSERT(ame_tilemap_gid(&tm, 0, 0, 0) == 0);
    UT_ASSERT(ame_tilemap_gid(&tm, 0, 1, 0) == 1);
    UT_ASSERT(ame_tilemap_gid(&tm, 0, 2, 0) == 2);
    UT_ASSERT(ame_tilemap_gid(&tm, 0, 0, -1) == 0); /* OOB = 0 */

    UT_CASE("1000 sprites + tilemap in a handful of draw calls");
    {
        ame_camera cam2;
        camera_viewport(camera_ortho2d(camera_desc(&cam2)), W, H);
        camera_build(&cam2);
        rp_set_gl_loader(egl_proc);
        ame_rp_desc d;
        int rc = rp_init(
            rp_desc_clear(rp_desc_begin(&d), 0.05f, 0.06f, 0.09f, 1.0f),
            &cam2, W, H);
        UT_ASSERTF(rc == 0, "rp_init rc=%d (%s)", rc, rp_gl_renderer());

        rp_begin_frame();
        int tiles = ame_tilemap_draw(&tm, 0 /* white tex: gid tints */,
                                     0, 0, 0);
        /* 1000 sprites spread across the view (same texture = same batch
         * bucket; layer sorts within it) */
        for (int i = 0; i < 1000; i++) {
            float x = (float)(i % 40) * 8.0f;
            float y = (float)(i / 40) * 9.0f;
            float tint[4] = { 0.9f, 0.8f, 0.3f, 1.0f };
            rp_push_sprite(0, x, y, 6, 6, 0, 0, 1, 1, tint, 10);
        }
        rp_end_frame();
        int nonzero = 0;
        for (int i = 0; i < tm.width * tm.height; i++)
            nonzero += tm.layer[0].data[i] != 0;
        UT_ASSERTF(tiles == nonzero, "tile quads %d != non-zero gids %d",
                   tiles, nonzero);
        UT_ASSERTF(rp_quads_last_frame() == 1000 + nonzero,
                   "quads %d != %d", rp_quads_last_frame(),
                   1000 + nonzero);
        UT_ASSERTF(rp_draw_calls_last_frame() <= 3,
                   "a handful of draws, got %d",
                   rp_draw_calls_last_frame());
        printf("    %d quads in %d draw calls (tilemap %d + 1000 sprites)\n",
               rp_quads_last_frame(), rp_draw_calls_last_frame(), tiles);
        uint32_t h1 = hash_frame();
        UT_ASSERT(h1 != 0);

        UT_CASE("pixel-perfect camera: sub-pixel moves snap");
        {
            /* +0.9px camera move must NOT change a snapped frame */
            cam2.pos.x += 0.9f;
            camera_build(&cam2);
            rp_set_camera(&cam2);
            rp_begin_frame();
            ame_tilemap_draw(&tm, 0, 0, 0, 0);
            for (int i = 0; i < 1000; i++) {
                float x = (float)(i % 40) * 8.0f;
                float y = (float)(i / 40) * 9.0f;
                float tint[4] = { 0.9f, 0.8f, 0.3f, 1.0f };
                rp_push_sprite(0, x, y, 6, 6, 0, 0, 1, 1, tint, 10);
            }
            rp_end_frame();
            uint32_t h2 = hash_frame();
            UT_ASSERTF(h2 == h1, "0.9px camera move changed pixels "
                                 "(snap not on?)");
            /* a full pixel move DOES change the frame */
            cam2.pos.x += 0.1f; /* total 1.0 */
            camera_build(&cam2);
            rp_set_camera(&cam2);
            rp_begin_frame();
            ame_tilemap_draw(&tm, 0, 0, 0, 0);
            for (int i = 0; i < 1000; i++) {
                float x = (float)(i % 40) * 8.0f;
                float y = (float)(i / 40) * 9.0f;
                float tint[4] = { 0.9f, 0.8f, 0.3f, 1.0f };
                rp_push_sprite(0, x, y, 6, 6, 0, 0, 1, 1, tint, 10);
            }
            rp_end_frame();
            UT_ASSERTF(hash_frame() != h1,
                       "1.0px move must shift pixels");
        }

        UT_CASE("resize cycle keeps pixels aligned");
        {
            cam2.pos.x = 0.0f;
            camera_build(&cam2);
            rp_set_camera(&cam2);
            rp_viewport(W / 2, H / 2);
            rp_viewport(W, H);
            rp_begin_frame();
            ame_tilemap_draw(&tm, 0, 0, 0, 0);
            for (int i = 0; i < 1000; i++) {
                float x = (float)(i % 40) * 8.0f;
                float y = (float)(i / 40) * 9.0f;
                float tint[4] = { 0.9f, 0.8f, 0.3f, 1.0f };
                rp_push_sprite(0, x, y, 6, 6, 0, 0, 1, 1, tint, 10);
            }
            rp_end_frame();
            UT_ASSERTF(hash_frame() == h1,
                       "viewport cycle must not change pixels");
        }
    }

    UT_OK();
    return ut_done("test_render_2d");
}
