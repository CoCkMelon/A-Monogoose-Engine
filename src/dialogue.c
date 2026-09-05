#include "ame/dialogue.h"

#include <string.h>

static const ame_dialogue_scene *g_reg[32];
static int g_nreg;

static void build_labels(ame_dialogue_runtime *rt)
{
    rt->label_count = 0;
    if (!rt->scene || !rt->scene->lines) return;
    for (size_t i = 0; i < rt->scene->line_count && rt->label_count < 128; i++) {
        const ame_dialogue_line *ln = &rt->scene->lines[i];
        if (ln->id && ln->id[0]) {
            rt->labels[rt->label_count].id = ln->id;
            rt->labels[rt->label_count].index = i;
            rt->label_count++;
        }
    }
}

int ame_dialogue_runtime_init(ame_dialogue_runtime *rt,
                              const ame_dialogue_scene *scene,
                              ame_dialogue_trigger_fn trigger_fn,
                              void *trigger_user)
{
    if (!rt) return 0;
    memset(rt, 0, sizeof(*rt));
    rt->scene = scene;
    rt->trigger_fn = trigger_fn;
    rt->trigger_user = trigger_user;
    if (!scene || !scene->lines || scene->line_count == 0) return 0;
    build_labels(rt);
    return 1;
}

static const ame_dialogue_line *get_line(const ame_dialogue_runtime *rt)
{
    if (!rt || !rt->scene || !rt->scene->lines) return NULL;
    if (rt->current_index >= rt->scene->line_count) return NULL;
    return &rt->scene->lines[rt->current_index];
}

const ame_dialogue_line *ame_dialogue_play_current(ame_dialogue_runtime *rt)
{
    const ame_dialogue_line *ln = get_line(rt);
    if (!ln) return NULL;
    if (ln->trigger && ln->trigger[0] && rt->trigger_fn)
        rt->trigger_fn(ln->trigger, ln, rt->trigger_user);
    return ln;
}

const ame_dialogue_line *ame_dialogue_advance(ame_dialogue_runtime *rt)
{
    if (!rt || !rt->scene) return NULL;
    if (rt->current_index < rt->scene->line_count)
        rt->current_index++;
    return ame_dialogue_play_current(rt);
}

const ame_dialogue_line *ame_dialogue_select_choice(ame_dialogue_runtime *rt,
                                                    const char *next_id)
{
    if (!rt || !next_id || !next_id[0]) return NULL;
    for (size_t i = 0; i < rt->label_count; i++) {
        if (rt->labels[i].id && strcmp(rt->labels[i].id, next_id) == 0) {
            rt->current_index = rt->labels[i].index;
            return ame_dialogue_play_current(rt);
        }
    }
    return NULL;
}

int ame_dialogue_current_has_choices(const ame_dialogue_runtime *rt)
{
    const ame_dialogue_line *ln = get_line(rt);
    return ln && ln->options && ln->option_count > 0;
}

int ame_dialogue_finished(const ame_dialogue_runtime *rt)
{
    return get_line(rt) == NULL;
}

void ame_dialogue_registry_reset(void)
{
    g_nreg = 0;
    memset(g_reg, 0, sizeof(g_reg));
}

int ame_dialogue_register(const ame_dialogue_scene *scene)
{
    if (!scene || !scene->name || g_nreg >= 32) return 0;
    g_reg[g_nreg++] = scene;
    return 1;
}

const ame_dialogue_scene *ame_dialogue_find(const char *name)
{
    if (!name) return NULL;
    for (int i = 0; i < g_nreg; i++)
        if (g_reg[i]->name && strcmp(g_reg[i]->name, name) == 0)
            return g_reg[i];
    return NULL;
}
