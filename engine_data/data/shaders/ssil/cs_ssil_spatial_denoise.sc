/*
 * Variance-guided a-trous wavelet spatial denoiser for SSIL (SVGF-style).
 *
 * Run with step_size = 1, 2, 4, ... ping-ponging the colour and variance buffers.
 *
 * Edge-stops (all multiplicative weights, never folded into the tap value):
 *   - Plane distance in VIEW space: |dot(p_tap - p_center, n_center)| relative to depth.
 *     Slope- and scale-invariant, so it does not collapse on angled / distant surfaces
 *     or as the a-trous step grows (a raw device-depth threshold does both).
 *   - World-normal similarity (pow edge-stop).
 *   - Variance-guided luminance edge-stop; the sigma widens on low-history pixels so
 *     freshly disoccluded / sparse regions still blur, and tightens as the estimate
 *     converges so detail is preserved.
 *
 * Accumulation is confidence-weighted (by each tap's alpha = trace/temporal sample
 * count), so holes and low-history pixels fill from valid same-surface neighbours while
 * fully converged regions (alpha ~= 1 everywhere) are unaffected.
 */

#include "../bgfx_compute.sh"
#include "../common.sh"
#include "../lighting.sh"
#include "../hiz_trace.sh"

SAMPLER2D(s_ssil_input, 0);
IMAGE2D_WO(i_ssil_output, rgba16f, 1);
SAMPLER2D(s_normal, 2);
SAMPLER2D(s_depth, 3);
SAMPLER2D(s_ssil_moments, 4);
SAMPLER2D(s_ssil_variance, 5);
IMAGE2D_WO(i_ssil_variance_output, r16f, 6);

// Resolution-agnostic: every position is derived from this dispatch's output size vs the
// full-res G-buffer, so the SAME shader runs the full-res tier and the half-res wide tier
// of the mixed-resolution denoiser (see ssil_pass::run_spatial_denoise) with no changes.

uniform vec4 u_denoise_params;
#define u_step_size    u_denoise_params.x
#define u_depth_sigma  u_denoise_params.y
#define u_normal_power u_denoise_params.z
#define u_luma_sigma   u_denoise_params.w

uniform vec4 u_denoise_params2;
#define u_has_moments  u_denoise_params2.x
#define u_first_pass   u_denoise_params2.y
#define u_frame_seed   u_denoise_params2.z

#define KW0 0.375
#define KW1 0.25
#define KW2 0.0625

// Luminance-sigma floor so the edge-stop never fully collapses on flat regions.
#define LUMA_SIGMA_EPS 0.05
// Max luminance-sigma multiplier applied to zero-history pixels. They are essentially
// raw single-frame noise, so the luminance edge-stop is widened until it is a near pure
// geometry blur; it ramps back to 1.0 as temporal/spatial confidence accumulates.
#define HISTORY_BLUR_BOOST 8.0

float ssil_kernel_weight(int dx, int dy)
{
    int ax = abs(dx);
    int ay = abs(dy);
    float wx = (ax == 0) ? KW0 : ((ax == 1) ? KW1 : KW2);
    float wy = (ay == 0) ? KW0 : ((ay == 1) ? KW1 : KW2);
    return wx * wy;
}

// Hash -> [0,1) (Dave Hoskins). Used for the per-pixel kernel rotation.
float ssil_hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, vec3(p3.y, p3.z, p3.x) + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Plain 3x3 luminance variance of the current (noisy) colour. Used as the spatial
// estimate on the first pass and as the fallback when temporal moments are unavailable.
float ssil_spatial_luma_variance(ivec2 coord, ivec2 size)
{
    float lum_sum = 0.0;
    float lum2_sum = 0.0;
    for(int y = -1; y <= 1; ++y)
    {
        for(int x = -1; x <= 1; ++x)
        {
            ivec2 tc = clamp(coord + ivec2(x, y), ivec2(0, 0), size - ivec2(1, 1));
            float lum = Luminance(texelFetch(s_ssil_input, tc, 0).rgb);
            lum_sum += lum;
            lum2_sum += lum * lum;
        }
    }
    float mean = lum_sum / 9.0;
    return max(0.0, lum2_sum / 9.0 - mean * mean);
}

// 3x3 gaussian prefilter of the propagated variance buffer. SVGF prefilters the variance
// before driving the luminance sigma so the edge-stop is stable pass to pass.
float ssil_prefiltered_variance(ivec2 coord, ivec2 size)
{
    float gw[3];
    gw[0] = 0.25; gw[1] = 0.5; gw[2] = 0.25;
    float acc = 0.0;
    float wsum = 0.0;
    for(int y = -1; y <= 1; ++y)
    {
        for(int x = -1; x <= 1; ++x)
        {
            ivec2 tc = clamp(coord + ivec2(x, y), ivec2(0, 0), size - ivec2(1, 1));
            float g = gw[x + 1] * gw[y + 1];
            acc += g * texelFetch(s_ssil_variance, tc, 0).r;
            wsum += g;
        }
    }
    return acc / max(wsum, 1e-6);
}

NUM_THREADS(8, 8, 1)
void main()
{
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(i_ssil_output);
    if(any(greaterThanEqual(coord, size)))
        return;

    vec2 texel_size = 1.0 / vec2(size);
    vec2 uv = (vec2(coord) + 0.5) * texel_size;
    vec2 depth_dim = vec2(textureSize(s_depth, 0));
    vec2 resolution_scale = depth_dim / max(vec2(size), vec2_splat(1.0));
    vec2 full_uv_center = HizScreenPassToFullResUV(uv, resolution_scale, depth_dim);

    vec4 center = texelFetch(s_ssil_input, coord, 0);
    float center_depth = DecodeGBufferDepthLod(full_uv_center, s_depth, 0.0).depth01;

    // Sky / background carries no indirect signal and has no surface to borrow from.
    BRANCH
#ifdef INVERTED_DEPTH_RANGE
    if(center_depth == 0.0)
#else
    if(center_depth == 1.0)
#endif
    {
        imageStore(i_ssil_output, coord, center);
        imageStore(i_ssil_variance_output, coord, vec4_splat(0.0));
        return;
    }

    vec3 center_normal_ws = DecodeGBufferNormalMetalRoughnessLod(full_uv_center, s_normal, 0.0).world_normal;
    vec3 center_vpos = computeViewSpacePosition(full_uv_center, center_depth);
    vec3 center_normal_vs = normalize(mul(u_view, vec4(center_normal_ws, 0.0)).xyz);
    float center_linear_z = max(abs(center_vpos.z), 1e-3);
    float center_luma = Luminance(center.rgb);
    int step_val = int(u_step_size);

    // Variance that drives the luminance edge-stop.
    float variance;
    BRANCH
    if(u_first_pass > 0.5)
    {
        float spatial_variance = ssil_spatial_luma_variance(coord, size);
        variance = spatial_variance;

        BRANCH
        if(u_has_moments > 0.5)
        {
            vec2 moments = texelFetch(s_ssil_moments, coord, 0).rg;
            float temporal_variance = max(0.0, moments.y - moments.x * moments.x);
            float temporal_trust = clamp(center.a, 0.0, 1.0);
            variance = mix(spatial_variance, temporal_variance, temporal_trust);
        }
    }
    else
    {
        variance = ssil_prefiltered_variance(coord, size);
    }

    // Widen the luminance edge-stop for low-history pixels (raw noise), tighten it as the
    // estimate converges. Bounded, so it never blows up the way a 1/confidence term does.
    float history_boost = mix(HISTORY_BLUR_BOOST, 1.0, clamp(center.a, 0.0, 1.0));
    float luma_sigma = u_luma_sigma * history_boost * (sqrt(variance) + LUMA_SIGMA_EPS);

    // Two accumulators:
    //   colour  -> confidence + edge-stop weighted (fills holes, denoises radiance).
    //   coverage-> geometry-only weighted alpha, for the propagated confidence channel.
    float center_color_w = KW0 * max(center.a, 0.0);
    vec3 color_sum = center.rgb * center_color_w;
    float color_w_sum = center_color_w;
    float variance_num = variance * center_color_w * center_color_w;

    float geom_sum = KW0;
    float coverage_sum = KW0 * clamp(center.a, 0.0, 1.0);

    // Per-pixel kernel rotation. A fixed axis-aligned a-trous lattice makes neighbouring
    // pixels filter near-identical dilated sample sets, so kernel truncation around bright
    // features and the dilated comb show up as static rectangular / grid banding. Rotating
    // the lattice by a per-pixel angle decorrelates neighbours: the structured grid becomes
    // incoherent high-frequency noise that the temporal pass averages away (ReLAX/ReBLUR).
    // The seed advances each frame (u_frame_seed) so temporal accumulation integrates many
    // distinct orientations, converging the spatial filter far faster than a static rotation.
    // The irrational per-axis offset keeps successive frames' seeds well decorrelated.
    float angle = ssil_hash12(vec2(coord) + vec2(u_frame_seed, u_frame_seed * 0.7548777)) * 6.2831853;
    float sa = sin(angle);
    float ca = cos(angle);
    vec2 base_uv = (vec2(coord) + 0.5) * texel_size;

    for(int y = -2; y <= 2; ++y)
    {
        for(int x = -2; x <= 2; ++x)
        {
            if(x == 0 && y == 0)
                continue;

            // Rotated, dilated tap offset (in trace-buffer texels) sampled bilinearly.
            vec2 lattice = vec2(x, y) * float(step_val);
            vec2 offset = vec2(ca * lattice.x - sa * lattice.y, sa * lattice.x + ca * lattice.y);
            vec2 tap_uv = base_uv + offset * texel_size;
            if(any(lessThan(tap_uv, vec2_splat(0.0))) || any(greaterThan(tap_uv, vec2_splat(1.0))))
                continue;

            vec4 sample_value = texture2DLod(s_ssil_input, tap_uv, 0.0);
            vec2 full_tap_uv = HizScreenPassToFullResUV(tap_uv, resolution_scale, depth_dim);
            float sample_depth = DecodeGBufferDepthLod(full_tap_uv, s_depth, 0.0).depth01;

            // Plane-distance depth edge-stop: distance of the neighbour from the centre's
            // tangent plane, relative to depth. ~0 along a flat surface (even angled), so
            // it never collapses the kernel the way a device-depth threshold does.
            vec3 sample_vpos = computeViewSpacePosition(full_tap_uv, sample_depth);
            float plane_dist = abs(dot(sample_vpos - center_vpos, center_normal_vs));
            float depth_w = exp(-plane_dist / (u_depth_sigma * center_linear_z + 1e-4));

            vec3 sample_normal_ws = DecodeGBufferNormalMetalRoughnessLod(full_tap_uv, s_normal, 0.0).world_normal;
            float normal_w = pow(max(0.0, dot(center_normal_ws, sample_normal_ws)), u_normal_power);

            float sample_luma = Luminance(sample_value.rgb);
            float luma_w = exp(-abs(center_luma - sample_luma) / max(luma_sigma, 1e-6));

            float kernel_w = ssil_kernel_weight(x, y);
            float geom_w = kernel_w * depth_w * normal_w;
            float sample_alpha = max(sample_value.a, 0.0);
            float color_w = geom_w * luma_w * sample_alpha;

            color_sum += sample_value.rgb * color_w;
            color_w_sum += color_w;

            float sample_variance = (u_first_pass > 0.5) ? variance : texture2DLod(s_ssil_variance, tap_uv, 0.0).r;
            variance_num += sample_variance * color_w * color_w;

            geom_sum += geom_w;
            coverage_sum += geom_w * clamp(sample_value.a, 0.0, 1.0);
        }
    }

    // If no valid weighted data was gathered (isolated hole) keep the centre untouched so
    // the filter never amplifies a single noisy sample or divides by ~0.
    vec3 filtered_rgb = (color_w_sum > 1e-5) ? (color_sum / color_w_sum) : center.rgb;
    float filtered_alpha = coverage_sum / max(geom_sum, 1e-6);
    float color_w_norm = max(color_w_sum, 1e-5);
    float filtered_variance = variance_num / (color_w_norm * color_w_norm);

    imageStore(i_ssil_output, coord, vec4(filtered_rgb, filtered_alpha));
    imageStore(i_ssil_variance_output, coord, vec4_splat(filtered_variance));
}
