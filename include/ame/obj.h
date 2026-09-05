#ifndef AME_OBJ_H
#define AME_OBJ_H

/*
 * Wavefront OBJ → CPU mesh. No Flecs, no Box2D colliders.
 * Supports v / vt / vn and triangle/quad faces (fan-triangulated).
 */

#include "ame/mesh.h"

int ame_obj_parse(const char *text, ame_mesh *out);
int ame_obj_load_file(const char *path, ame_mesh *out);

#endif
