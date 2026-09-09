/* ame-next shader module: the LIT pass fragment shader.
 * Branchless shadow: amt = 0 subtracts exactly 0.0, so output stays
 * BIT-IDENTICAL to the unshadowed path when shadows are off. */
uniform sampler2D u_tex;
uniform sampler2D u_stex;
uniform float u_shadow_amt;
uniform float u_stexel;
in vec2 v_uv;
in vec4 v_col;
in vec4 v_shadow;
in vec3 v_diff;
in vec2 v_ndl_lit;
out vec4 o_col;
#include "shadow_recv.glsl"
void main() {
    vec4 c = texture(u_tex, v_uv) * v_col;
    vec3 sc = v_shadow.xyz / v_shadow.w * 0.5 + 0.5;
    float sh = 1.0;
    if (u_shadow_amt > 0.5 && v_ndl_lit.y > 0.0 &&
        sc.x > 0.0 && sc.x < 1.0 && sc.y > 0.0 &&
        sc.y < 1.0 && sc.z > 0.0 && sc.z < 1.0) {
        sh = ame_shadow_pcf(u_stex, sc, v_ndl_lit.x, u_stexel);
    }
    c.rgb = c.rgb - v_diff * (1.0 - sh) * u_shadow_amt;
    o_col = c;
}
