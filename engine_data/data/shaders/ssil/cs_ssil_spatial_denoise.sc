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
 * RGB is filtered as radiance. Alpha is filtered separately as the SSIL blend weight.
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
/// A-trous kernel radius in taps (1 => 3x3 / 8 taps, 2 => 5x5 / 24 taps). The wide half-
/// resolution tier uses radius 1 (indirect diffuse is low-frequency, so the outer ring
/// adds little) with a larger dilation step to keep its reach; the full-res tier uses 2.
#define u_kernel_radius u_denoise_params2.w

#define KW0 0.375
#define KW1 0.25
#define KW2 0.0625

// Luminance-sigma floor so the edge-stop never fully collapses on flat regions. Combined
// as max(LUMA_SIGMA_ABS, LUMA_SIGMA_REL * center_luma) so the floor scales with the local
// indirect intensity: an absolute 0.01 is sane for SDR indirect but becomes effectively 0
// for HDR scenes (luminance > 1), there disabling the luma stop on noisy pixels. The
// relative term keeps the tolerance proportional to the signal, the absolute term keeps
// it non-zero in fully-black regions so the kernel still smooths near-zero noise.
#define LUMA_SIGMA_ABS 0.01
#define LUMA_SIGMA_REL 0.02
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
    // The pixel-centre UV already maps to the centre of the block this sample covers,
    // so it is the correct geometry-guide UV at any trace resolution. Snapping it through
    // HizScreenPassToFullResUV biases the lookup by +0.5px relative to the SSIL samples,
    // which compounds across the widening denoise passes into a view-dependent shift.
    vec2 full_uv_center = uv;

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
        BRANCH
        if(u_has_moments > 0.5)
        {
            // SVGF "short-history" hybrid. The temporal variance is mu2 - mu1^2 from the
            // running luminance moments -- excellent on pixels with mature history, but
            // ZERO by construction on freshly-disoccluded pixels (silhouette edges during
            // motion, fresh pixels entering from the screen border): when validity dropped
            // history, the temporal pass wrote mu1 = luma_curr and mu2 = luma_curr^2, so
            // mu2 - mu1^2 = 0. A zero variance collapses the luminance edge-stop, only
            // identical-luma neighbours contribute, and the raw single-frame Monte Carlo
            // noise shows through directly -- the "edge speckle during motion" failure.
            //
            // Spatial variance over the 3x3 input neighbourhood reflects the actual noise
            // level for THOSE pixels (their neighbours are also fresh and noisy, so the
            // spatial spread is large). Driving luma_sigma off it for young pixels opens
            // the edge-stop, admits more neighbours into the weighted mean, and produces
            // the wider initial blur SVGF needs while the history hasn't accumulated.
            //
            // moments.a = W_new / max_accum_frames is the normalised per-pixel history
            // strength: ~0 immediately after a rejection, climbing toward 1 over many
            // valid-history frames. We use it as the blend factor between the two
            // variance estimators -- spatial when young, temporal when mature.
            vec4 m = texelFetch(s_ssil_moments, coord, 0);
            float temporal_variance = max(0.0, m.y - m.x * m.x);
            float spatial_variance = ssil_spatial_luma_variance(coord, size);
            // hist_strength in [0,1]: <=0.05 -> ~1 frame of history -> all spatial;
            // >=0.4 -> ~6 frames of valid history -> all temporal; smooth ramp between.
            // 6 frames is roughly where the running luminance mean has converged enough
            // that the temporal variance becomes the better estimator.
            float hist_strength = m.a;
            float age_blend = smoothstep(0.05, 0.4, hist_strength);
            variance = mix(spatial_variance, temporal_variance, age_blend);
        }
        else
        {
            variance = ssil_spatial_luma_variance(coord, size);
        }
    }
    else
    {
        variance = ssil_prefiltered_variance(coord, size);
    }

    float luma_floor = max(LUMA_SIGMA_ABS, LUMA_SIGMA_REL * max(center_luma, 0.0));
    float luma_sigma = u_luma_sigma * sqrt(variance) + luma_floor;

    // Two accumulators: radiance is always valid on surfaces (including environment
    // fallback); alpha is the separate final blend-weight signal.
    // Centre weight matches the separable B3-spline kernel at (0,0): KW0*KW0 = 0.1406.
    // Using bare KW0 here would give the noisy centre ~2.7x the weight the kernel actually
    // prescribes, biasing the result toward the un-filtered sample and weakening the blur.
    float center_kernel_w = KW0 * KW0;
    float center_color_w = center_kernel_w;
    vec3 color_sum = center.rgb * center_color_w;
    float color_w_sum = center_color_w;
    float variance_num = variance * center_color_w * center_color_w;

    float geom_sum = center_kernel_w;
    float coverage_sum = center_kernel_w * clamp(center.a, 0.0, 1.0);

    // Keep the final spatial filter deterministic. This pass runs after temporal resolve,
    // so per-frame kernel jitter would show up directly as crawling speckles.
    // Vary the per-pixel rotation by step_size so successive a-trous passes use distinct
    // dither orientations; without this every pass picks systematically correlated taps
    // and the rotated lattice leaves a stable, frame-coherent pattern on flat indirect.
    float angle = ssil_hash12(vec2(coord) + vec2(float(step_val), float(step_val) * 7.0)) * 6.2831853;
    float sa = sin(angle);
    float ca = cos(angle);
    vec2 base_uv = (vec2(coord) + 0.5) * texel_size;

    // Variance-collapsed kernel was REMOVED here. The previous version shrunk `radius`
    // to 0 when sqrt(variance) was small relative to centre luma and skipped the kernel
    // entirely, on the theory that "low variance == converged == no blur needed". The
    // failure mode that argument missed: variance is also ARTIFICIALLY low for any pixel
    // whose history was just rejected (depth or normal mismatch). For those pixels the
    // temporal pass writes mu1 = luma_curr and mu2 = luma_curr^2, so the moments give
    // variance = 0 -- but the COLOUR is the raw single-frame trace, not a converged mean.
    // Skipping the spatial blur on those pixels exposes the per-frame Monte Carlo noise
    // directly as the "blotches" the user saw. Detail preservation is already handled by
    // the variance-driven luma_sigma below: low variance -> tight luma stop -> only
    // similar-luma neighbours contribute -> detail kept. We let the kernel always run.
    int radius = int(u_kernel_radius);
    if(radius < 1)
        radius = 2;
    // The full-res detail tier (radius 2) samples axis-aligned + point-sampled to keep fine
    // detail; the wide half-res tier (radius 1) keeps the rotated bilinear tap.
    bool detail_tier = (radius >= 2);

    for(int y = -2; y <= 2; ++y)
    {
        for(int x = -2; x <= 2; ++x)
        {
            if(x == 0 && y == 0)
                continue;
            // Skip the outer ring when running the narrow (radius-1) kernel. Done before any
            // texture work so the half-res tier actually pays for only its 8 taps.
            if(abs(x) > radius || abs(y) > radius)
                continue;

            // Tap acquisition differs by tier:
            //  - Detail tier: axis-aligned, point-sampled (texelFetch). A rotated tap is
            //    fetched bilinearly, so each tap averages a 2x2 footprint and pulls
            //    neighbours across edges -- that softens the fine intra-surface detail this
            //    tier exists to preserve.
            //  - Wide tier: rotated, dilated tap sampled bilinearly, which dithers away the
            //    a-trous "plus" structure on the coarse, low-frequency signal.
            vec2 tap_uv;
            vec4 sample_value;
            float sample_variance;
            BRANCH
            if(detail_tier)
            {
                ivec2 tap_coord = coord + ivec2(x, y) * step_val;
                if(any(lessThan(tap_coord, ivec2(0, 0))) || any(greaterThanEqual(tap_coord, size)))
                    continue;
                sample_value = texelFetch(s_ssil_input, tap_coord, 0);
                tap_uv = (vec2(tap_coord) + 0.5) * texel_size;
                sample_variance = (u_first_pass > 0.5) ? variance : texelFetch(s_ssil_variance, tap_coord, 0).r;
            }
            else
            {
                vec2 lattice = vec2(x, y) * float(step_val);
                vec2 offset = vec2(ca * lattice.x - sa * lattice.y, sa * lattice.x + ca * lattice.y);
                tap_uv = base_uv + offset * texel_size;
                if(any(lessThan(tap_uv, vec2_splat(0.0))) || any(greaterThan(tap_uv, vec2_splat(1.0))))
                    continue;
                sample_value = texture2DLod(s_ssil_input, tap_uv, 0.0);
                sample_variance = (u_first_pass > 0.5) ? variance : texture2DLod(s_ssil_variance, tap_uv, 0.0).r;
            }

            vec2 full_tap_uv = tap_uv;
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
            float color_w = geom_w * luma_w;

            color_sum += sample_value.rgb * color_w;
            color_w_sum += color_w;

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
