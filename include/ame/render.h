/* ame-next — render pipeline (render.txt): ONE renderer, ONE shader set,
 * single pass, batched quads for 2D sprites AND 3D meshes AND text AND UI.
 *
 * Setup layer: ame_rp_desc fluent builder (pointer in, same pointer out).
 * Hot layer: the per-frame vertex buffer is ONE owned buffer rewritten in
 * place each frame — never reallocated, never wrapped in objects.
 *
 * GL function pointers are loaded through an injected proc-address getter:
 * SDL_GL_GetProcAddress on desktop/Android, eglGetProcAddress in headless
 * tests, resolved by Emscripten on web. The GLSL is one source compiled per
 * context flavor (desktop GL vs GLES) — no second renderer.
 */
#ifndef AME_RENDER_H
#define AME_RENDER_H

#include <ame/ame.h>
#include <ame/camera.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- setup: fluent descriptor -------------------------------------------- */
typedef struct {
    bool  depth_test;
    bool  blend;
    bool  gles;          /* GLSL 300 es vs 330 core (context flavor) */
    float clear[4];
    int   max_quads;     /* batch capacity */
} ame_rp_desc;

ame_rp_desc *rp_desc_begin(ame_rp_desc *d);
ame_rp_desc *rp_desc_depth(ame_rp_desc *d, bool on);
ame_rp_desc *rp_desc_blend(ame_rp_desc *d, bool on);
ame_rp_desc *rp_desc_gles(ame_rp_desc *d, bool on);
ame_rp_desc *rp_desc_clear(ame_rp_desc *d, float r, float g, float b, float a);
ame_rp_desc *rp_desc_max_quads(ame_rp_desc *d, int n);

typedef void *(*ame_gl_getproc_fn)(const char *name);
void rp_set_gl_loader(ame_gl_getproc_fn get_proc); /* call before rp_init */

/* returns 0 on success */
int  rp_init(const ame_rp_desc *desc, const ame_camera *cam, int w, int h);
void rp_shutdown(void);
void rp_viewport(int w, int h);
void rp_set_camera(const ame_camera *cam);
const char *rp_gl_renderer(void);

/* --- hot: one frame --------------------------------------------------------
 * rp_begin_frame: clear, reset the batch.
 * push sprites/quads (z/layer order stable within a texture page).
 * rp_end_frame: upload, draw ranges grouped by texture (few draw calls).
 */
void rp_begin_frame(void);
void rp_end_frame(void);

/* 2D sugar: axis-aligned quad in pixel space (camera must be ortho2d) */
void rp_push_sprite(int tex, float x, float y, float w, float h,
                    float u0, float v0, float u1, float v1,
                    const float tint[4], float layer);
/* 3D/general: explicit corners in world space (column order: p0..p3 CCW) */
void rp_push_quad(int tex,
                  const float p0[3], const float p1[3],
                  const float p2[3], const float p3[3],
                  float u0, float v0, float u1, float v1,
                  const float tint[4], float layer);

/* textures: small integer ids into a static registry, loaded once */
int  rp_load_texture(const uint8_t *pixels, int w, int h, int comps,
                     bool nearest_sampling);
void rp_free_texture(int id);
int  rp_white_texture(void); /* 1x1 white, always id 0 */

/* top-left of the current view in WORLD px (ortho pixel camera only).
 * Screen-anchored drawing (UI text) adds this so (0,0) means the window
 * corner, no matter where the camera center sits. */
void rp_screen_origin(float *ox, float *oy);

/* screenshot for golden tests: RGBA8 bottom-up rows flipped to top-down */
bool rp_read_pixels(uint8_t *rgba_out, int w, int h);

/* diagnostics */
int  rp_draw_calls_last_frame(void);
int  rp_quads_last_frame(void);

#ifdef __cplusplus
}
#endif

#endif /* AME_RENDER_H */
