/* ame-next shader module: the DEFAULT pass vertex shader.
 * Branchless textured: position through the view-projection, uv + tint
 * straight to the fragment stage. No lighting, no shadow, no DSDF. */
in vec3 a_pos;
in vec2 a_uv;
in vec4 a_col;
uniform mat4 u_vp;
out vec2 v_uv;
out vec4 v_col;
void main() {
    v_uv = a_uv;
    v_col = a_col;
    gl_Position = u_vp * vec4(a_pos, 1.0);
}
