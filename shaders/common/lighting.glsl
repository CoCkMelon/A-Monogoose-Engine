/* ame-next shader module: forward-lighting equation for lit vertex
 * shaders. dir = the direction the light TRAVELS. range <= 0 disables
 * the point light. ndl is returned for the shadow bias + diffuse terms. */
vec3 ame_forward_light(vec3 nrm, vec3 pos,
                       vec3 ldir, vec3 lcol, vec3 lamb,
                       vec3 ppos, vec3 pcol, float prange,
                       out float ndl) {
    vec3 n = normalize(nrm);
    ndl = max(dot(n, -ldir), 0.0);
    vec3 light = lamb + lcol * ndl;
    if (prange > 0.0) {
        vec3 d3 = ppos - pos;
        float att = max(0.0, 1.0 - length(d3) / prange);
        light += pcol * (max(dot(n, normalize(d3)), 0.0)
                         * att * att);
    }
    return light;
}
