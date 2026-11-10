/* Biscuit Fuel on the trunk app_* hooks (loop.txt, app.h).
 *
 * Ported from the game lineage's SDL-callback app.c: the engine bootstrap
 * (src/app_sdl.c) owns SDL/GL and the split threads; this file implements
 * the six hooks. Logic thread: input poll + bf_tick + cue drain + snapshot
 * publish at the fixed rate. Main thread: copy-out + draw. No SDL here.
 */
#include "bf_audio.h"
#include "config.h"
#include "gameplay.h"
#include "input.h"
#include "render/pipeline.h"
#include "ui.h"

#include <ame/ame.h>
#include <ame/app.h>
#include <ame/input.h>
#include <ame/debug.h>
#include <ame/render.h>
#include <ame/text.h>

AME_SNAP_DEFINE(BfSnap)

static bf_view VIEW;
static BfSnap_snap SNAP;
static double T0;

/* live watch floats for the trunk debug overlay (ame/debug.h): the sim
 * snapshot feeds them per frame, the overlay reads the pinned pointers. */
static float g_w_car_x, g_w_car_y, g_w_speed, g_w_fuel, g_w_hp;

int app_init(void)
{
    game_input_bindings();
    /* v1 debug overlay: hand-registered floats, no reflection */
    ame_debug_watch("car_x", &g_w_car_x);
    ame_debug_watch("car_y", &g_w_car_y);
    ame_debug_watch("vx", &g_w_speed);
    ame_debug_watch("fuel", &g_w_fuel);
    ame_debug_watch("hp", &g_w_hp);
    bf_reset(1);
    game_input_reset();
    bf_set_input_ok(1);

    if (!bf_view_init(&VIEW, APP_DEFAULT_WIDTH, APP_DEFAULT_HEIGHT))
        return 1;

    ame_rp_desc d;
    rp_desc_begin(&d);
    rp_desc_clear(&d, 0.10f, 0.13f, 0.20f, 1.0f);
    /* 2D painter order: `layer` is the draw order; a depth test would clip
     * the layer values (and the ribbon's extruded z) against the ortho
     * depth range */
    rp_desc_depth(&d, false);
    rp_desc_size(&d, VIEW.vw, VIEW.vh);
    /* the level ribbon is ~2.4k tris on its own; the default batch cap
     * would silently drop it plus everything pushed after it */
    rp_desc_max_quads(&d, 32768);
    rp_desc_camera(&d, &VIEW.cam);
    if (rp_init(&d) != 0)
        return 1;
    if (text_init(true) < 0)
        return 1;

    bf_audio_init();
    BfSnap_snap_init(&SNAP);
    BfSnap s;
    bf_snapshot(&s);
    BfSnap_publish(&SNAP, &s);
    T0 = ame_time_seconds();
    return 0;
}

int app_event(const ame_ev *ev)
{
    (void)ev;
    return 0;
}

int app_fixed(float dt)
{
    game_input_poll();
    if (in_pressed(BF_ACT_RELOAD)) {
        /* dev hot reload (levels.txt): rebuild the immutable mesh and the
         * course exactly once, the same way a cold load does */
        bf_view_reload_level(&VIEW);
        bf_reset(1);
    }
    bf_tick(dt, (float)(ame_time_seconds() - T0));
    bf_cues_drain();
    BfSnap s;
    bf_snapshot(&s);
    BfSnap_publish(&SNAP, &s);
    return 0;
}

void app_resize(int w, int h)
{
    bf_view_resize(&VIEW, w, h);
}

int app_render(void)
{
    static BfSnap frame;
    (void)BfSnap_latest_copy(&SNAP, &frame);
    bf_view_draw(&VIEW, &frame);
    ui_render_hud(&frame, VIEW.vw, VIEW.vh); /* same batch as the world */
#if APP_DEBUG_DRAW
    g_w_car_x = frame.car_x;
    g_w_car_y = frame.car_y;
    g_w_speed = frame.car_vx;
    g_w_fuel = frame.fuel;
    g_w_hp = frame.hp;
    ame_debug_watch_draw((float)VIEW.vw - 250.0f, 14.0f, 1.5f,
                         (const float[4]){ 0.75f, 0.95f, 0.75f, 1.0f }, 60.0f);
#endif
    bf_view_present(&VIEW);
    return 0;
}

void app_quit(void)
{
    bf_audio_shutdown();
    bf_view_shutdown(&VIEW);
    rp_shutdown();
}
