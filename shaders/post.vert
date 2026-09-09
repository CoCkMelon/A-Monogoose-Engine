/* ame-next shader module: the POST pass vertex shader. One fullscreen
 * triangle; uv = pos*0.5+0.5 maps NDC(-1,-1)->(0,0)=scene texel
 * lower-left, so the compose is pixel-exact with the direct path at
 * identity settings. */
in vec2 a_pos;
out vec2 v_uv;
void main() {
    v_uv = a_pos * 0.5 + 0.5;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
