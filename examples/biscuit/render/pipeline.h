#ifndef BF_PIPELINE_H
#define BF_PIPELINE_H

#include "gameplay.h"
#include "ame/camera.h"
#include "ame/gfx.h"
#include "ame/mesh.h"
#include "ame/text.h"

enum { BF_ATLAS = 256 };

typedef struct bf_view {
    ame_pipeline pipeline;
    ame_camera   camera;
    ame_font     font;
    ame_mesh     level_mesh;       /* static ribbon — uploaded once */
    ame_vertex  *level_cpu;        /* owned conversion of level_verts */
    unsigned    *level_idx;        /* owned triangle indices */
    int          pixel_width;
    int          pixel_height;
    unsigned char atlas[BF_ATLAS * BF_ATLAS * 4];
} bf_view;

bf_view *bf_view_init(bf_view *v, int pixel_width, int pixel_height);
bf_view *bf_view_resize(bf_view *v, int pixel_width, int pixel_height);
void     bf_view_draw(bf_view *v, const BfSnap *snap);
void     bf_view_shutdown(bf_view *v);

#endif
