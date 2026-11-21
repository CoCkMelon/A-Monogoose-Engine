/* debug_watch.c - the v1 debug overlay: a flat watch table drawn with the
 * trunk text stack. Registration is O(n) over <= AME_DEBUG_WATCH_MAX slots
 * and happens at init; drawing formats into one stack buffer per frame and
 * lays it out once (text_layout is CPU-only, quads go to the batch). */
#include "ame/debug.h"

#include "ame/render.h"
#include "ame/text.h"

#include <stdio.h>
#include <string.h>

/* A bounded name copy that is warning-clean on every toolchain this project
 * builds with, written as one path rather than a platform split.
 *
 * MSVC's UCRT marks strncpy deprecated in favour of strncpy_s, and under the
 * project's -Werror that is a hard error on the windows clang job:
 *   error: 'strncpy' is deprecated: This function or variable may be unsafe.
 *          Consider using strncpy_s instead. [-Werror,-Wdeprecated-declarations]
 * Keeping strncpy for POSIX instead is not available either: once the bound is
 * derived from strlen(src), gcc reports
 *   error: 'strncpy' specified bound depends on the length of the source
 *          argument [-Werror=stringop-truncation]
 * which is the same class of GCC-only -Werror failure as src/error.c's
 * format-truncation. So strncpy is out on both sides, for different reasons.
 *
 * memcpy of an explicitly clamped length plus an explicit terminator has
 * exactly the semantics this call wants, is not deprecated anywhere, and
 * triggers neither warning. No _CRT_SECURE_NO_WARNINGS: suppressing that
 * globally would hide the genuinely unsafe uses the deprecation exists to
 * flag. */

typedef struct {
    char name[AME_DEBUG_NAME_MAX];
    const float *f;
    const int *i;
    bool used;
} watch_t;

static watch_t g_watch[AME_DEBUG_WATCH_MAX];

static int watch_slot(const char *name)
{
    int free_slot = -1;
    if (!name || !name[0])
        return -1;
    for (int k = 0; k < AME_DEBUG_WATCH_MAX; k++) {
        if (g_watch[k].used) {
            if (strncmp(g_watch[k].name, name, AME_DEBUG_NAME_MAX) == 0)
                return k;
        } else if (free_slot < 0) {
            free_slot = k;
        }
    }
    return free_slot;
}

static int watch_add(const char *name, const float *f, const int *i)
{
    int k = watch_slot(name);
    if (k < 0 || (!f && !i))
        return -1;
    size_t nlen = strlen(name);
    if (nlen > AME_DEBUG_NAME_MAX - 1)
        nlen = AME_DEBUG_NAME_MAX - 1;
    memcpy(g_watch[k].name, name, nlen);
    g_watch[k].name[nlen] = '\0';
    g_watch[k].f = f;
    g_watch[k].i = i;
    g_watch[k].used = true;
    return k;
}

int ame_debug_watch(const char *name, const float *value)
{
    return watch_add(name, value, NULL);
}

int ame_debug_watch_int(const char *name, const int *value)
{
    return watch_add(name, NULL, value);
}

int ame_debug_watch_count(void)
{
    int n = 0;
    for (int k = 0; k < AME_DEBUG_WATCH_MAX; k++)
        n += g_watch[k].used ? 1 : 0;
    return n;
}

const char *ame_debug_watch_name(int slot)
{
    if (slot < 0 || slot >= AME_DEBUG_WATCH_MAX || !g_watch[slot].used)
        return NULL;
    return g_watch[slot].name;
}

void ame_debug_watch_clear(void)
{
    for (int k = 0; k < AME_DEBUG_WATCH_MAX; k++)
        g_watch[k].used = false;
}

/* column gap between the name and value columns, in layout px */
#define WATCH_COL_GAP 8.0f

/* compose the two column buffers (same line order); returns line count */
static int watch_compose(char *names, size_t nsz, char *vals, size_t vsz)
{
    int n = 0;
    size_t no = 0, vo = 0;
    for (int k = 0; k < AME_DEBUG_WATCH_MAX && n < AME_DEBUG_WATCH_MAX; k++) {
        if (!g_watch[k].used)
            continue;
        int wn = snprintf(names + no, nsz - no, "%s\n", g_watch[k].name);
        int wv;
        if (g_watch[k].f)
            wv = snprintf(vals + vo, vsz - vo, "%.2f\n",
                          (double)*g_watch[k].f);
        else
            wv = snprintf(vals + vo, vsz - vo, "%d\n", *g_watch[k].i);
        if (wn <= 0 || wv <= 0 || no + (size_t)wn >= nsz
            || vo + (size_t)wv >= vsz)
            break;
        no += (size_t)wn;
        vo += (size_t)wv;
        n++;
    }
    return n;
}

/* lay both columns out; returns line count (0 = nothing to draw) */
static int watch_layout(float scale, ame_text_layout *ln, ame_text_layout *lv)
{
    char names[AME_DEBUG_WATCH_MAX * (AME_DEBUG_NAME_MAX + 1)];
    char vals[AME_DEBUG_WATCH_MAX * 16];
    int n = watch_compose(names, sizeof names, vals, sizeof vals);
    if (n == 0)
        return 0;
    if (text_layout_plain(names, 0.0f, AME_TEXT_ALIGN_L, scale, AME_FONT_PIXEL , ln) <= 0)
        return 0;
    if (text_layout_plain(vals, 0.0f, AME_TEXT_ALIGN_L, scale, AME_FONT_PIXEL , lv) <= 0)
        return 0;
    return n;
}

bool ame_debug_watch_size(float scale, float *w, float *h)
{
    if (w)
        *w = 0.0f;
    if (h)
        *h = 0.0f;
    ame_text_layout ln, lv;
    if (watch_layout(scale, &ln, &lv) == 0)
        return false;
    if (w)
        *w = ln.w + WATCH_COL_GAP * scale + lv.w;
    if (h)
        *h = ln.h;
    return true;
}

int ame_debug_watch_draw(float x, float y, const ame_debug_watch_style *st)
{
    if (!st)
        return 0;
    ame_text_layout ln, lv;
    int n = watch_layout(st->scale, &ln, &lv);
    if (n == 0)
        return 0;
    float vt[4];
    if (st->value_tint[3] < 0.0f) {
        vt[0] = st->tint[0]; vt[1] = st->tint[1];
        vt[2] = st->tint[2]; vt[3] = st->tint[3];
    } else {
        vt[0] = st->value_tint[0]; vt[1] = st->value_tint[1];
        vt[2] = st->value_tint[2]; vt[3] = st->value_tint[3];
    }
    /* Scrim pushed BEFORE the columns at layer+1: with depth on, z is
     * layer*0.001 under LEQUAL, so the later-pushed glyphs (layer) pass
     * against it; with depth off (2D painter order, biscuit) the later
     * push is on top anyway. layer-1 would depth-clip the glyphs. */
    if (st->bg[3] > 0.0f) {
        float w = ln.w + WATCH_COL_GAP * st->scale + lv.w;
        /* px_y_down UI rect: rp_push_sprite_screen applies the view's
         * top-left so the overlay lands at the window corner whatever the
         * camera centre is (biscuit follows the car) */
        /* the overlay is px_y_down UI: hand the rect to the SCREEN push and
         * let the renderer do the one y-up conversion (coords.txt rule 2) */
        ame_rp_sprite sc = {
            .tex = rp_white_texture(),
            .x = x - st->pad, .y = y - st->pad,
            .w = w + 2.0f * st->pad, .h = ln.h + 2.0f * st->pad,
            .u0 = 0.0f, .v0 = 0.0f, .u1 = 1.0f, .v1 = 1.0f,
            .tint = { st->bg[0], st->bg[1], st->bg[2], st->bg[3] },
            .layer = st->layer + 1.0f,
        };
        rp_push_sprite_screen(&sc);
    }
text_draw_screen(&ln, x, y, st->tint, st->layer, AME_FONT_PIXEL);
    text_draw_screen(&lv, x + ln.w + WATCH_COL_GAP * st->scale, y, vt,
                     st->layer, AME_FONT_PIXEL);
    return n;
}
