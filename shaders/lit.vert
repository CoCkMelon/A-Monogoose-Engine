/* ame-next shader module: the LIT pass vertex shader (app-created).
 * Per-vertex forward lighting + the shadow varyings: light-space pos,
 * the DIFFUSE part a shadow may remove (ambient/point stay), and the
 * bias inputs. Unlit quads (a_lit=0) keep v_col == a_col exactly. */
in vec3 a_pos;
in vec3 a_nrm;
in float a_lit;
in vec2 a_uv;
in vec4 a_col;
uniform mat4 u_vp;
uniform vec3 u_ldir;
uniform vec3 u_lcol;
uniform vec3 u_lamb;
uniform vec3 u_ppos;
uniform vec3 u_pcol;
uniform float u_prange;
uniform mat4 u_svp;
out vec2 v_uv;
out vec4 v_col;
out vec4 v_shadow;
out vec3 v_diff;
out vec2 v_ndl_lit;
#include "lighting.glsl"
void main() {
    v_uv = a_uv;
    float ndl;
    vec3 light = ame_forward_light(a_nrm, a_pos, u_ldir, u_lcol, u_lamb,
                                   u_ppos, u_pcol, u_prange, ndl);
    v_col = a_col * vec4(mix(vec3(1.0), light, a_lit), 1.0);
    v_shadow = u_svp * vec4(a_pos, 1.0);
    v_diff = a_col.rgb * (u_lcol * ndl * a_lit);
    v_ndl_lit = vec2(ndl, a_lit);
    gl_Position = u_vp * vec4(a_pos, 1.0);
}
