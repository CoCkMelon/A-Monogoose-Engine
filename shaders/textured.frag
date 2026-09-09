/* ame-next shader module: the DEFAULT pass fragment shader.
 * ONE texture fetch, ONE multiply, zero branches, two varyings.
 * Everything else (lighting, shadow, DSDF) lives in app-created
 * passes so the common path never pays for it. */
uniform sampler2D u_tex;
in vec2 v_uv;
in vec4 v_col;
out vec4 o_col;
void main() {
    o_col = texture(u_tex, v_uv) * v_col;
}
