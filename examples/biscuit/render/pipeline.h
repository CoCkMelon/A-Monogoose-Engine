#ifndef BF_PIPELINE_H
#define BF_PIPELINE_H

/* Biscuit render module on the trunk renderer (render.txt).
 *
 * The lineage's gfx batch/pipeline is GONE: this module owns ONE
 * ame_camera (pixel-perfect ortho2d), the immutable static level mesh
 * (ame_mesh - loaded from the runtime OBJ on the dev path, or built from
 * the baked level_gen arrays on the cook path; levels.txt "TWO PATHS, ONE
 * MESH TYPE"), and translates the game's world units into view pixels.
 * Everything is pushed through rp_push_mesh / rp_push_quad / rp_push_tri
 * into the default textured pass; the HUD lives in ui.c.
 */
#include "gameplay.h"

#include <ame/camera.h>
#include <ame/mesh.h>

typedef struct bf_view {
    ame_camera cam;
    ame_mesh level; /* static ribbon, built ONCE per load/reload */
    int level_from_obj;
    int vw, vh;
    float ppu;   /* pixels per world unit (viewport height / camera height) */
    float cam_x, cam_y; /* world centre of the view */
} bf_view;

int  bf_view_init(bf_view *v, int vw, int vh);
void bf_view_resize(bf_view *v, int vw, int vh);
/* begins the frame and draws the world; the caller draws UI (ui.c) and
 * then calls bf_view_present() - glyphs must land in the SAME batch. */
void bf_view_draw(bf_view *v, const BfSnap *s);
void bf_view_present(bf_view *v);
void bf_view_shutdown(bf_view *v);

/* dev hot reload (levels.txt): re-read the level OBJ and rebuild the
 * immutable mesh once. 0 on success, -1 when the file is gone/bad (the
 * previous mesh stays). */
int bf_view_reload_level(bf_view *v);

#endif /* BF_PIPELINE_H */
