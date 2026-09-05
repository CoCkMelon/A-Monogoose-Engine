#include "ame/mesh.h"

#include <stdlib.h>
#include <string.h>

void ame_mesh_reset(ame_mesh *m)
{
    if (!m) return;
    memset(m, 0, sizeof(*m));
}

void ame_mesh_free(ame_mesh *m)
{
    if (!m) return;
    free(m->verts);
    free(m->idx);
    memset(m, 0, sizeof(*m));
}
