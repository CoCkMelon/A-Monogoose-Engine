/* ame-next shader module: the SHADOW pass vertex shader (depth-only
 * caster pass, app-created). Same batch positions through the light
 * VP; unlit (UI) verts collapse outside clip = never cast. Branchless
 * (mix/step with a 0/1 factor is exact). */
in vec3 a_pos;
in float a_lit;
uniform mat4 u_svp;
void main() {
    vec4 lit_pos = u_svp * vec4(a_pos, 1.0);
    gl_Position = mix(vec4(2.0, 2.0, 2.0, 1.0), lit_pos, step(0.5, a_lit));
}
