#include <ame/mesh.h>

#include <stdlib.h>
#include <string.h>

void ame_mesh_init(ame_mesh *m)
{
    if (!m)
        return;
    memset(m, 0, sizeof *m);
}

void ame_mesh_free(ame_mesh *m)
{
    if (!m)
        return;
    if (m->owned) {
        free(m->verts);
        free(m->idx);
        free(m->groups);
    }
    memset(m, 0, sizeof *m);
}

const ame_mesh_group *ame_mesh_group_find(const ame_mesh *m, const char *name)
{
    if (!m || !name)
        return NULL;
    for (int i = 0; i < m->group_count; i++)
        if (strcmp(m->groups[i].name, name) == 0)
            return &m->groups[i];
    return NULL;
}
