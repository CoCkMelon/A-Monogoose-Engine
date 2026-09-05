#ifndef AME_DETAIL_CGLM_H
#define AME_DETAIL_CGLM_H

/*
 * Private: recp/cglm with its vec3/mat4 typedefs renamed so they do not
 * collide with ame Unity structs. Only src/math.c includes this.
 */

#ifndef CGLM_ALL_UNALIGNED
#  define CGLM_ALL_UNALIGNED
#endif

#define ivec2  cglm_ivec2
#define ivec3  cglm_ivec3
#define ivec4  cglm_ivec4
#define vec2   cglm_vec2
#define vec3   cglm_vec3
#define vec4   cglm_vec4
#define versor cglm_versor
#define mat2   cglm_mat2
#define mat3   cglm_mat3
#define mat4   cglm_mat4
#define mat2x3 cglm_mat2x3
#define mat2x4 cglm_mat2x4
#define mat3x2 cglm_mat3x2
#define mat3x4 cglm_mat3x4
#define mat4x2 cglm_mat4x2
#define mat4x3 cglm_mat4x3

#include <cglm/vec2.h>
#include <cglm/vec3.h>
#include <cglm/vec4.h>
#include <cglm/mat3.h>
#include <cglm/mat4.h>
#include <cglm/quat.h>
#include <cglm/affine.h>
#include <cglm/euler.h>
#include <cglm/project.h>
#include <cglm/util.h>

#undef ivec2
#undef ivec3
#undef ivec4
#undef vec2
#undef vec3
#undef vec4
#undef versor
#undef mat2
#undef mat3
#undef mat4
#undef mat2x3
#undef mat2x4
#undef mat3x2
#undef mat3x4
#undef mat4x2
#undef mat4x3

#endif
