#ifndef OUTPUT_NOISE_SH_HEADER_GUARD
#define OUTPUT_NOISE_SH_HEADER_GUARD

// Display-referred grain + TPDF dither, applied after the tone curve (and after
// FXAA when that pass runs). Shared by fs_tonemapping and fs_fxaa.

// Per-pixel, per-frame white noise for film grain. The tiled ign16x16 lookup is
// deliberately NOT used here: it is a static 16x16 pattern (right for dithering,
// where a stable pattern is the point) and sliding it around reads as texture,
// not grain. Grain needs decorrelated noise every pixel, every frame.
float grain_hash(vec2 p)
{
    vec3 p3 = fract(vec3(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, p3.yzx + vec3_splat(33.33));
    return fract((p3.x + p3.y) * p3.z);
}

vec3 apply_output_noise(vec3 color, vec2 frag_coord, float grain_amount, float grain_seed, float dithering)
{
    if (grain_amount > 0.0)
    {
        vec2 grain_seed_offset = vec2(grain_seed * 127.1, grain_seed * 311.7);
        float noise = grain_hash(frag_coord + grain_seed_offset) - 0.5;
        float display_luma = saturate(dot(color, vec3(0.2126, 0.7152, 0.0722)));
        float response = 1.0 - 0.8 * display_luma;
        color += vec3_splat(noise * grain_amount * response);
    }
    if (dithering > 0.5)
    {
        float n1 = ign16x16(frag_coord);
        float n2 = ign16x16(frag_coord + vec2(5.588238, 5.588238));
        float tpdf = (n1 + n2) - 1.0;
        color += vec3_splat(tpdf * (1.0 / 255.0));
    }
    return color;
}

#endif // OUTPUT_NOISE_SH_HEADER_GUARD
