/*
 * Shared gather weighting for surface-cache bounce / probes / sample.
 * Prefer sunlit floor chroma (red ground); damp cool neon emitters.
 */

#ifndef SCACHE_GATHER_WEIGHT_SH
#define SCACHE_GATHER_WEIGHT_SH

float scache_emitter_form_boost(vec3 rad_rgb, float rad_lum, vec3 card_normal)
{
    float floor_w = saturate(card_normal.y);
    float chroma = length(rad_rgb - vec3(rad_lum, rad_lum, rad_lum));
    // Warm bias: red channel above green/blue (floor blood, curtains).
    float warm = saturate(rad_rgb.r - max(rad_rgb.g, rad_rgb.b));
    float cool = saturate(max(rad_rgb.g, rad_rgb.b) - rad_rgb.r);
    float boost = 1.0;
    boost *= 1.0 + 3.0 * floor_w;
    // Chroma boost only on floors — generic chroma made cyan emissives win.
    boost *= 1.0 + 3.5 * floor_w * saturate(chroma * 2.5);
    boost *= 1.0 + 2.5 * floor_w * warm;
    // Non-floor cool high-chroma (neon cyan) must not dominate gather.
    boost *= 1.0 - 0.75 * (1.0 - floor_w) * cool * saturate(chroma * 3.0);
    boost *= 1.0 - 0.45 * (1.0 - floor_w) * saturate(rad_lum * 1.25) * cool;
    return max(boost, 0.05);
}

#endif // SCACHE_GATHER_WEIGHT_SH
