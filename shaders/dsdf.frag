/* ame-next shader module: the DSDF pass fragment shader. Plain
 * textured fetch, then DSDF reconstruction on marked quads only
 * (v_dsdf > 0.5; the marker stamp forces unlit, so no extra guard).
 * Unmarked quads render exactly like the default textured pass. */
uniform sampler2D u_tex;
uniform vec2 u_dsdf_tsize;
uniform float u_dsdf_range;
in vec2 v_uv;
in vec4 v_col;
in float v_dsdf;
out vec4 o_col;
#include "dsdf.glsl"
void main() {
    vec4 c = texture(u_tex, v_uv) * v_col;
    vec2 t_dsdf = v_uv / u_dsdf_tsize - 0.5;
    float aa_dsdf = max(fwidth(t_dsdf.x), fwidth(t_dsdf.y));
    if (v_dsdf > 0.5) {
        c = vec4(c.rgb, c.a * ame_dsdf_alpha(u_tex, t_dsdf, aa_dsdf,
                                             u_dsdf_tsize, u_dsdf_range));
    }
    o_col = c;
}
