/* ame-next shader module: shadow receive (3x3 PCF). Returns the
 * shadow factor 0..1 (1 = lit). Plain sampler2D depth reads + manual
 * compare: shadow samplers poison fragments on llvmpipe even when
 * never sampled. sc = light-space uvz in 0..1. */
float ame_shadow_pcf(sampler2D stex, vec3 sc, float ndl, float texel) {
    float bias = mix(0.0025, 0.0005, clamp(ndl, 0.0, 1.0));
    float sh = 0.0;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            float d = texture(stex, sc.xy +
                              vec2(float(dx), float(dy)) * texel).r;
            sh += (sc.z - bias) <= d ? 1.0 : 0.0;
        }
    return sh / 9.0;
}
