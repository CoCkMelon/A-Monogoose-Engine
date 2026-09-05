#ifndef MEM_DRAW_H
#define MEM_DRAW_H

#include "ame/camera.h"
#include "ame/gfx.h"
#include "ame/memory.h"
#include "ame/text.h"

enum { MEM_ATLAS = 256 };

typedef struct mem_view {
    ame_pipeline pipeline;
    ame_camera   camera;
    ame_font     font;
    int          pixel_width;
    int          pixel_height;
    unsigned char atlas[MEM_ATLAS * MEM_ATLAS * 4];
} mem_view;

/* SETUP: chain lives inside. Returns v, or NULL on GL failure. */
mem_view *mem_view_init(mem_view *v, int pixel_width, int pixel_height);
mem_view *mem_view_resize(mem_view *v, int pixel_width, int pixel_height);
void      mem_view_draw(mem_view *v, const MemSnap *snap);
void      mem_view_shutdown(mem_view *v);

#endif
