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
    bool  post;          /* Stage 2: offscreen scene target + post pass */
    float clear[4];
    int   max_quads;     /* batch capacity */
} ame_rp_desc;

ame_rp_desc *rp_desc_begin(ame_rp_desc *d);
ame_rp_desc *rp_desc_depth(ame_rp_desc *d, bool on);
ame_rp_desc *rp_desc_blend(ame_rp_desc *d, bool on);
ame_rp_desc *rp_desc_gles(ame_rp_desc *d, bool on);
ame_rp_desc *rp_desc_post(ame_rp_desc *d, bool on);
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
/* single triangle (batched; emitted as a quad with a degenerate 2nd tri) */
void rp_push_tri(int tex,
                 const float p0[3], const float p1[3], const float p2[3],
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
/* Update an existing texture in place (same id/dims/format) - the
 * dynamic-texture path for software-shaded content (a CPU raymarcher
 * uploading each frame, video frames later). false on mismatch. */
bool rp_update_texture(int id, const uint8_t *pixels, int w, int h, int comps);
int  rp_white_texture(void); /* 1x1 white, always id 0 */

/* top-left of the current view in WORLD px (ortho pixel camera only).
 * Screen-anchored drawing (UI text) adds this so (0,0) means the window
 * corner, no matter where the camera center sits. */
void rp_screen_origin(float *ox, float *oy);

/* --- Stage 2: forward lighting in the ONE shader (single pass) -------
 * Geometry is lit only when pushed between rp_set_lit(1) and
 * rp_set_lit(0); UI/text/billboards stay unlit and byte-identical.
 * rp_set_normal stamps the normal used by the Lambert term (per-push,
 * like a tint). Defaults (lit=0, no lights) reproduce the v0 unlit
 * look exactly. dir = the direction the light TRAVELS. range <= 0
 * disables the point light. */
void rp_set_lit(int on);
void rp_set_normal(float nx, float ny, float nz);
void rp_lighting(const float dir[3], const float col[3], const float amb[3]);
void rp_point_light(const float pos[3], const float col[3], float range);
void rp_lighting_off(void);

/* --- Stage 2: post pass (THE MULTIPASS DECISION, made) -----------------
 * render.txt deferred multipass until a real need; Stage 2 is it.
 * DECISION: the ONE geometry program/batch is unchanged - when post
 * is on, the frame renders into an offscreen RGBA8 scene target
 * (same clear, same depth config) and a tiny second program (one
 * fullscreen triangle, no depth, no blend) composes it into the
 * default framebuffer with: tint multiply + radial vignette + (later
 * effects chained here). Identity settings (tint 1, vignette 0) are
 * PIXEL-EXACT with the direct path (test-proven) - post is a pure
 * add-on, never a second renderer. Effects are cheap uniforms, so
 * they may be set per frame. */
/* --- Stage 2 mesh path: Assimp-baked geometry in the ONE batch ------
 * tools/assimp2c.c bakes models to C arrays at BUILD time (levels.txt:
 * no runtime obj/parser). verts are interleaved pos xyz / nrm xyz /
 * uv (matches ame_mesh_vert). xform = optional column-major 4x4
 * (ame_m4) applied to positions; normals rotate by the upper 3x3.
 * Per-vertex normals pair with rp_set_lit(1). Returns tris pushed. */
typedef struct {
    float pos[3];
    float nrm[3];
    float uv[2];
} ame_mesh_vert;

int rp_push_mesh(int tex, const ame_mesh_vert *verts, int vert_count,
                 const unsigned int *idx, int idx_count,
                 const float *xform_or_null, const float tint[4],
                 float layer);

/* --- Stage 2: directional shadow map (the second pass) ------------------
 * rp_shadow() enables a shadow for THIS frame: the already-pushed batch
 * is drawn once more, depth-only, through an ortho light camera (dir =
 * the direction the light TRAVELS - same convention as rp_lighting;
 * ortho box side 2*extent centered at center). CASTERS are the lit
 * geometry (rp_set_lit); unlit UI/text never casts and never receives.
 * The main pass removes only the DIFFUSE direct term inside shadow
 * (3x3 PCF, slope-scaled bias); ambient/point light stay. With shadows
 * off - or on unlit pixels - output is BIT-IDENTICAL to the one-pass
 * path (the shader subtracts v_diff*(1-shadow), i.e. zero). */
void rp_shadow(const float dir[3], const float center[3], float extent);
void rp_shadow_off(void);

void rp_post_tint(float r, float g, float b);
void rp_post_vignette(float strength); /* 0 = off .. ~0.5 strong */

/* screenshot for golden tests: RGBA8 bottom-up rows flipped to top-down */
bool rp_read_pixels(uint8_t *rgba_out, int w, int h);

/* diagnostics */
int  rp_draw_calls_last_frame(void);
int  rp_quads_last_frame(void);

#ifdef __cplusplus
}
#endif

#endif /* AME_RENDER_H */
