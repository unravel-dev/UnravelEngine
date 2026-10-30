$input v_texcoord0

#include "../common.sh"
#include "tonemapping_lut.sh"
#include "output_noise.sh"
#include "../exposure/local_exposure.sh"
#include "../bloom/bloom_common.sh"

uniform vec4 u_tonemapping;
uniform vec4 u_grading;
uniform vec4 u_vignette;
// x = bloom intensity, y = lens dirt intensity (0 without a mask), z = 1 when the bloom ran (bloom_pass::composite).
uniform vec4 u_bloom;
// The assembled pyramid's tint * weight.
uniform vec4 u_bloom_tint;

SAMPLER2D(s_input, 0);
SAMPLER2D(s_exposure, 1);
/// The bloom pyramid's half-resolution level and the lens dirt mask (stages 2, 3 are local exposure's).
SAMPLER2D(s_bloom, 4);
SAMPLER2D(s_dirt, 5);
/// The colour grading, tone curve and display encoding as a table (fs_tonemapping_lut.sc, tonemapping_lut.sh).
SAMPLER2D(s_tonemapping_lut, 6);

#define u_tonemappingExposure u_tonemapping.x
#define u_dithering           u_tonemapping.z
#define u_grain_amount        u_grading.z
#define u_grain_seed          u_grading.w
#define u_vignette_intensity  u_vignette.x
#define u_vignette_smoothness u_vignette.y
#define u_bloom_intensity     u_bloom.x
#define u_bloom_dirt          u_bloom.y
#define u_has_bloom           u_bloom.z

/// What the bloom adds to the scene: the pyramid read bilinearly at this pixel, tinted, times the intensity and the
/// lens dirt mask. Finite and in half-float range, so a NaN in the pyramid cannot reach the image.
vec3 sample_bloom(vec2 texcoord)
{
    if(u_has_bloom < 0.5)
    {
        return vec3_splat(0.0);
    }
    vec3 bloom = texture2D(s_bloom, texcoord).rgb * u_bloom_tint.rgb;
    vec3 scale = vec3_splat(u_bloom_intensity);
    // Most views have no lens dirt: no fetch of the mask for a term that would be 0.
    if(u_bloom_dirt != 0.0)
    {
        scale += texture2D(s_dirt, texcoord).rgb * u_bloom_dirt;
    }
    return bloom_safe_hdr(bloom * scale);
}

/// The graded, tone-mapped, display-encoded @p color (linear, post-exposure): the table's two slices around its blue,
/// each read bilinearly in red and green, blended by hand across blue.
vec3 sample_tonemapping_lut(vec3 color)
{
    float size = float(TONEMAPPING_LUT_SIZE);
    vec3 index = tonemapping_lut_index(color);
    float slice = floor(index.z);
    float next_slice = min(slice + 1.0, size - 1.0);
    vec2 uv = (index.xy + vec2_splat(0.5)) / vec2(size * size, size);
    vec3 low = texture2DLod(s_tonemapping_lut, uv + vec2(slice / size, 0.0), 0.0).rgb;
    vec3 high = texture2DLod(s_tonemapping_lut, uv + vec2(next_slice / size, 0.0), 0.0).rgb;
    return mix(low, high, index.z - slice);
}

void main()
{
    vec3 scene_color = texture2D(s_input, v_texcoord0).rgb;

    // u_tonemappingExposure divides out the CPU's share of the pre-exposure; the GPU's share goes here.
    float exposure = u_tonemappingExposure / u_pre_exposure_gpu_scale;
    vec4 exposure_texel = texture2DLod(s_exposure, vec2(0.5, 0.5), 0.0);
    float adapted = exposure_texel.r;
    if ((adapted != adapted) || adapted <= 0.0 || adapted >= 1.0e10)
    {
        adapted = 1.0;
    }
    exposure *= max(adapted, 1e-5);

    // LOCAL EXPOSURE, per pixel, on top of the global one, measured on the scene the grid was
    // built from. The bloom part keeps the global exposure only: its source already took local
    // exposure in the bloom pass, and its glow is no luminance the grid ever saw.
    float display_exposure_log = log2(max(exposure, 1e-10)) + u_log2_pre_exposure;
    float local_exposure = compute_local_exposure(scene_color, v_texcoord0, display_exposure_log, exposure_texel.b);
    vec3 color = scene_color * local_exposure + sample_bloom(v_texcoord0);

    // Exposure first so the grading in the table operates in post-AE space.
    color *= exposure;

    // Vignette in linear, ahead of the grading the table holds: behaves like lens light falloff, so darkened
    // highlights still roll through the tone curve instead of graying out.
    if (u_vignette_intensity > 0.0)
    {
        float dist = length((v_texcoord0 - 0.5) * 2.0);
        float start = mix(0.8, 0.05, saturate(u_vignette_smoothness));
        float falloff = smoothstep(start, 1.55, dist);
        color *= 1.0 - u_vignette_intensity * falloff;
    }

    color = sample_tonemapping_lut(color);

    // Grain and TPDF dither: skipped (uniforms zeroed) when FXAA follows so
    // the AA filter does not smear them; FXAA applies the same helper after.
    color = apply_output_noise(color, gl_FragCoord.xy, u_grain_amount, u_grain_seed, u_dithering);

    gl_FragColor = vec4(color, 1.0f);
}
