/* ame-next shader module: the POST pass fragment shader. Tint
 * multiply + radial vignette (later effects chain here). */
in vec2 v_uv;
uniform sampler2D u_tex;
uniform vec3 u_tint;
uniform float u_vig;
out vec4 o_col;
void main() {
    vec2 d = v_uv - vec2(0.5);
    float f = 1.0 - u_vig * min(1.0, 2.0 * dot(d, d));
    vec4 s = texture(u_tex, v_uv);
    o_col = vec4(s.rgb * u_tint * f, s.a);
}
