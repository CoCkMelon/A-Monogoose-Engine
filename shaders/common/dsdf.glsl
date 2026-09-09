/* ame-next shader module: DSDF text reconstruction (Acta Cybernetica
 * 25 (2021): first-order densely sampled distance field). 4-corner
 * Taylor reconstruction - EXACT for straight edges.
 *   tex   = the DSDF atlas (R = distance, GB = gradient)
 *   t     = cell-space coord (uv/tsize - 0.5)
 *   aa    = fwidth-based edge width (computed OUTSIDE any branch:
 *           fwidth() on values defined only under divergent control
 *           flow is undefined)
 *   tsize = 1/atlas_w, 1/atlas_h; range = the bake distance range
 * Returns the coverage alpha multiplier. Fetch coords are CLAMPED
 * (llvmpipe kills fragments on out-of-range texelFetch - stale
 * uniforms degrade to no-ink, never kill the quad).
 * NOTE: DSDF quality/artifacts are another agent's task; this module
 * is the untouched reconstruction, relocated from render.c. */
float ame_dsdf_alpha(sampler2D tex, vec2 t, float aa, vec2 tsize,
                     float range) {
    vec2 cb = floor(t);
    vec2 f = t - cb;
    vec2 dims = vec2(1.0) / tsize - 1.0;
    ivec2 p00 = ivec2(clamp(cb, vec2(0.0), dims - 1.0));
    vec4 s00 = texelFetch(tex, p00, 0);
    vec4 s10 = texelFetch(tex, p00 + ivec2(1, 0), 0);
    vec4 s11 = texelFetch(tex, p00 + ivec2(1, 1), 0);
    vec4 s01 = texelFetch(tex, p00 + ivec2(0, 1), 0);
    float r = range;
    float d00 = (s00.r * 2.0 - 1.0) * r
              + dot(s00.gb * 2.0 - 1.0, f - vec2(0.5, 0.5));
    float d10 = (s10.r * 2.0 - 1.0) * r
              + dot(s10.gb * 2.0 - 1.0, f - vec2(1.5, 0.5));
    float d01 = (s01.r * 2.0 - 1.0) * r
              + dot(s01.gb * 2.0 - 1.0, f - vec2(0.5, 1.5));
    float d11 = (s11.r * 2.0 - 1.0) * r
              + dot(s11.gb * 2.0 - 1.0, f - vec2(1.5, 1.5));
    float w00 = (1.0 - f.x) * (1.0 - f.y);
    float w10 = f.x * (1.0 - f.y);
    float w01 = (1.0 - f.x) * f.y;
    float w11 = f.x * f.y;
    float d = w00 * d00 + w10 * d10 + w01 * d01 + w11 * d11;
    return clamp(0.5 + d / max(aa, 1e-5), 0.0, 1.0);
}
