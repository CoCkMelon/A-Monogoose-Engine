/* ame-next shader module: the DSDF pass vertex shader (app-created,
 * experimental - DSDF quality is another agent's task). Textured
 * transform plus the DSDF marker varying (text quads stamp nrm.y=1). */
in vec3 a_pos;
in vec3 a_nrm;
in vec2 a_uv;
in vec4 a_col;
uniform mat4 u_vp;
out vec2 v_uv;
out vec4 v_col;
out float v_dsdf;
void main() {
    v_uv = a_uv;
    v_col = a_col;
    v_dsdf = a_nrm.y;
    gl_Position = u_vp * vec4(a_pos, 1.0);
}
