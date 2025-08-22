#ifndef AME_SCENE2D_H
#define AME_SCENE2D_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <glad/gl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Single-pass 2D batching: order in the vertex buffer defines depth.
// Matches the shader layout in examples: a_pos (loc 0), a_col (loc 1), a_uv (loc 2)

typedef struct AmeVertex2D {
    float x, y;      // position
    float r, g, b, a;// color
    float u, v;      // uv
    float l;         // texture layer (for sampler2DArray); default 0
} AmeVertex2D;

typedef struct AmeDrawRange {
    GLuint tex;      // texture to bind for this range (0 if untextured)
    uint32_t first;  // first vertex in the big VBO
    uint32_t count;  // number of vertices to draw
} AmeDrawRange;

typedef struct AmeScene2DBatch {
    AmeVertex2D* verts;
    uint32_t count;
    uint32_t capacity;

    AmeDrawRange* ranges;
    uint32_t range_count;
    uint32_t range_capacity;

    GLuint current_tex;
    uint32_t current_first;
    int range_open; // 0/1
} AmeScene2DBatch;

static inline void ame_scene2d_batch_init(AmeScene2DBatch* b) {
    memset(b, 0, sizeof(*b));
}

static inline void ame_scene2d_batch_free(AmeScene2DBatch* b) {
    free(b->verts); b->verts = NULL; b->count = b->capacity = 0;
    free(b->ranges); b->ranges = NULL; b->range_count = b->range_capacity = 0;
    b->current_tex = 0; b->current_first = 0; b->range_open = 0;
}

static inline void ame_scene2d_batch_reset(AmeScene2DBatch* b) {
    b->count = 0;
    b->range_count = 0;
    b->current_tex = 0;
    b->current_first = 0;
    b->range_open = 0;
}

static inline void ame_scene2d_batch_ensure(AmeScene2DBatch* b, uint32_t more) {
    if (b->count + more > b->capacity) {
        uint32_t new_cap = b->capacity ? b->capacity : 1024;
        while (new_cap < b->count + more) new_cap *= 2;
        AmeVertex2D* nv = (AmeVertex2D*)realloc(b->verts, new_cap * sizeof(AmeVertex2D));
        if (!nv) abort();
        b->verts = nv;
        b->capacity = new_cap;
    }
}

static inline void ame_scene2d_ranges_ensure(AmeScene2DBatch* b, uint32_t more) {
    if (b->range_count + more > b->range_capacity) {
        uint32_t new_cap = b->range_capacity ? b->range_capacity : 32;
        while (new_cap < b->range_count + more) new_cap *= 2;
        AmeDrawRange* nr = (AmeDrawRange*)realloc(b->ranges, new_cap * sizeof(AmeDrawRange));
        if (!nr) abort();
        b->ranges = nr;
        b->range_capacity = new_cap;
    }
}

static inline void ame_scene2d_batch_switch_texture(AmeScene2DBatch* b, GLuint tex) {
    if (!b->range_open) {
        // start first range
        ame_scene2d_ranges_ensure(b, 1);
        b->ranges[b->range_count++] = (AmeDrawRange){ .tex = tex, .first = b->count, .count = 0 };
        b->current_tex = tex;
        b->current_first = b->count;
        b->range_open = 1;
        return;
    }
    if (tex != b->current_tex) {
        // close previous range
        b->ranges[b->range_count - 1].count = b->count - b->current_first;
        // start new
        ame_scene2d_ranges_ensure(b, 1);
        b->ranges[b->range_count++] = (AmeDrawRange){ .tex = tex, .first = b->count, .count = 0 };
        b->current_tex = tex;
        b->current_first = b->count;
    }
}

static inline void ame_scene2d_batch_finalize(AmeScene2DBatch* b) {
    if (b->range_open) {
        b->ranges[b->range_count - 1].count = b->count - b->current_first;
    }
}

static inline void ame_scene2d_batch_push(AmeScene2DBatch* b, GLuint tex,
                                          float x, float y, float r, float g, float bl, float a,
                                          float u, float v) {
    ame_scene2d_batch_switch_texture(b, tex);
    ame_scene2d_batch_ensure(b, 1);
    b->verts[b->count++] = (AmeVertex2D){ x, y, r, g, bl, a, u, v, 0.0f };
}

static inline void ame_scene2d_batch_push_ex(AmeScene2DBatch* b, GLuint tex,
                                             float x, float y, float r, float g, float bl, float a,
                                             float u, float v, float layer) {
    ame_scene2d_batch_switch_texture(b, tex);
    ame_scene2d_batch_ensure(b, 1);
    b->verts[b->count++] = (AmeVertex2D){ x, y, r, g, bl, a, u, v, layer };
}

// Append a triangle list from separate position and uv arrays (length = vert_count)
static inline void ame_scene2d_batch_append_arrays(AmeScene2DBatch* b, GLuint tex,
                                                   const float* pos, const float* uv, uint32_t vert_count,
                                                   float r, float g, float bl, float a) {
    if (vert_count == 0) return;
    ame_scene2d_batch_switch_texture(b, tex);
    ame_scene2d_batch_ensure(b, vert_count);
    for (uint32_t i = 0; i < vert_count; ++i) {
        const float px = pos[i*2 + 0];
        const float py = pos[i*2 + 1];
        const float tu = uv ? uv[i*2 + 0] : 0.0f;
        const float tv = uv ? uv[i*2 + 1] : 0.0f;
        b->verts[b->count++] = (AmeVertex2D){ px, py, r, g, bl, a, tu, tv, 0.0f };
    }
}

// Append a textured axis-aligned rectangle as two triangles
static inline void ame_scene2d_batch_append_rect(AmeScene2DBatch* b, GLuint tex,
                                                 float x, float y, float w, float h,
                                                 float r, float g, float bl, float a) {
    const float x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    // tri 1
    ame_scene2d_batch_push(b, tex, x0, y0, r, g, bl, a, 0.0f, 0.0f);
    ame_scene2d_batch_push(b, tex, x1, y0, r, g, bl, a, 1.0f, 0.0f);
    ame_scene2d_batch_push(b, tex, x0, y1, r, g, bl, a, 0.0f, 1.0f);
    // tri 2
    ame_scene2d_batch_push(b, tex, x1, y0, r, g, bl, a, 1.0f, 0.0f);
    ame_scene2d_batch_push(b, tex, x1, y1, r, g, bl, a, 1.0f, 1.0f);
    ame_scene2d_batch_push(b, tex, x0, y1, r, g, bl, a, 0.0f, 1.0f);
}

#ifdef __cplusplus
}
#endif

#endif // AME_SCENE2D_H

