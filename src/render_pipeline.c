#include "ame/render_pipeline.h"
#include "ame/tilemap.h"
#include "ame/scene2d.h"
#include <glad/gl.h>
#include <string.h>

static int g_rp_w=0, g_rp_h=0;

void ame_rp_begin_frame(int screen_w, int screen_h){
    g_rp_w = screen_w; g_rp_h = screen_h;
}

void ame_rp_submit_tile_layers(const AmeRP_TileLayer* layers, int layer_count,
                               int map_w, int map_h,
                               float cam_x, float cam_y, float cam_zoom, float cam_rot){
    (void)map_w; (void)map_h; (void)cam_rot;
    // Bridge to existing compositor to avoid duplicating shader logic
    extern void ame_tilemap_renderer_init(void);
    extern void ame_tilemap_render_layers(const struct AmeCamera* cam, int screen_w, int screen_h,
                                          int map_w, int map_h,
                                          const AmeTileLayerGpuDesc* layers, int layer_count);
    typedef struct AmeTileLayerGpuDesc AmeTileLayerGpuDesc; // bring type name
    struct AmeCamera cam = {0}; cam.x = cam_x; cam.y = cam_y; cam.zoom = cam_zoom; cam.rotation = 0.0f; cam.viewport_w = g_rp_w; cam.viewport_h = g_rp_h;

    // Temporary conversion: AmeRP_TileLayer to AmeTileLayerGpuDesc
    AmeTileLayerGpuDesc tmp[16];
    int cnt = (layer_count>16?16:layer_count);
    for (int i=0;i<cnt;i++){
        tmp[i].atlas_tex = layers[i].atlas_tex; tmp[i].gid_tex = layers[i].gid_tex;
        tmp[i].atlas_w = layers[i].atlas_w; tmp[i].atlas_h = layers[i].atlas_h;
        tmp[i].tile_w = layers[i].tile_w; tmp[i].tile_h = layers[i].tile_h;
        tmp[i].firstgid = layers[i].firstgid; tmp[i].columns = layers[i].columns;
    }
    ame_tilemap_renderer_init();
    ame_tilemap_render_layers(&cam, g_rp_w, g_rp_h, map_w, map_h, (const AmeTileLayerGpuDesc*)tmp, cnt);
}

void ame_rp_submit_sprites(const AmeRP_Sprite* sprites, int count,
                           float cam_x, float cam_y, float cam_zoom, float cam_rot){
    (void)cam_rot;
    // Minimal immediate-mode: build a temporary batch and draw with current shader
    // Reuse engine 2D batch structure from ame/scene2d.h

    AmeScene2DBatch b; ame_scene2d_batch_init(&b); ame_scene2d_batch_reset(&b);
    for (int i=0;i<count;i++){
        const AmeRP_Sprite* s = &sprites[i];
        float x=s->x, y=s->y, w=s->w, h=s->h;
        // tri1
        ame_scene2d_batch_push(&b, s->tex, x,y, s->r,s->g,s->b,s->a, s->u0,s->v0);
        ame_scene2d_batch_push(&b, s->tex, x+w,y, s->r,s->g,s->b,s->a, s->u1,s->v0);
        ame_scene2d_batch_push(&b, s->tex, x,y+h, s->r,s->g,s->b,s->a, s->u0,s->v1);
        // tri2
        ame_scene2d_batch_push(&b, s->tex, x+w,y, s->r,s->g,s->b,s->a, s->u1,s->v0);
        ame_scene2d_batch_push(&b, s->tex, x+w,y+h, s->r,s->g,s->b,s->a, s->u1,s->v1);
        ame_scene2d_batch_push(&b, s->tex, x,y+h, s->r,s->g,s->b,s->a, s->u0,s->v1);
    }
    // Draw with example shader: the engine currently draws batches in examples; keep pipeline minimal (no shader here)
    // In examples, the caller should bind shader, set MVP then draw b. For now we just free batch data (placeholder to avoid GL calls here).
    ame_scene2d_batch_free(&b);
}

void ame_rp_end_frame(void){
}

