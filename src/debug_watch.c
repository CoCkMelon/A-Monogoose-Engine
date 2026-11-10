/* debug_watch.c - the v1 debug overlay: a flat watch table drawn with the
 * trunk text stack. Registration is O(n) over <= AME_DEBUG_WATCH_MAX slots
 * and happens at init; drawing formats into one stack buffer per frame and
 * lays it out once (text_layout is CPU-only, quads go to the batch). */
#include "ame/debug.h"

#include "ame/text.h"

#include <stdio.h>
#include <string.h>

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
    strncpy(g_watch[k].name, name, AME_DEBUG_NAME_MAX - 1);
    g_watch[k].name[AME_DEBUG_NAME_MAX - 1] = '\0';
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

int ame_debug_watch_draw(float x, float y, float scale,
                         const float tint[4], float layer)
{
    char buf[AME_DEBUG_WATCH_MAX * (AME_DEBUG_NAME_MAX + 16)];
    int n = 0;
    size_t off = 0;
    for (int k = 0; k < AME_DEBUG_WATCH_MAX && n < AME_DEBUG_WATCH_MAX; k++) {
        if (!g_watch[k].used)
            continue;
        int w;
        if (g_watch[k].f)
            w = snprintf(buf + off, sizeof buf - off, "%s: %.2f\n",
                         g_watch[k].name, (double)*g_watch[k].f);
        else
            w = snprintf(buf + off, sizeof buf - off, "%s: %d\n",
                         g_watch[k].name, *g_watch[k].i);
        if (w <= 0 || off + (size_t)w >= sizeof buf)
            break;
        off += (size_t)w;
        n++;
    }
    if (n == 0)
        return 0;
    ame_text_layout l;
    if (text_layout_plain(buf, 0.0f, AME_TEXT_ALIGN_L, scale, &l) <= 0)
        return 0;
    text_draw_screen(&l, x, y, tint, layer);
    return n;
}
