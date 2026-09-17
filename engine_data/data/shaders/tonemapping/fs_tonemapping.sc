$input v_texcoord0

#include "../common.sh"
#include "tonemapping.sh"
#include "output_noise.sh"

uniform vec4 u_tonemapping;
uniform vec4 u_grading;
uniform vec4 u_wb_lms;
uniform vec4 u_vignette;
uniform vec4 u_lift;
uniform vec4 u_gamma_inv;
uniform vec4 u_gain;

SAMPLER2D(s_input, 0);
SAMPLER2D(s_exposure, 1);
/// LOCAL EXPOSURE (UE's bilateral-grid method). The grid is the flattened 3D atlas
/// cs_local_exposure_grid.sc writes - texel (tile_x * slices + slice, tile_y), rg = the slice's
/// sum of log2 luminance and sum of weight per tile cell - and the blurred texture is the
/// tile-grid Gaussian of the plain tile means. Both are bound as white 1x1 when local exposure
/// is off, and the enable lane below keeps them unread.
SAMPLER2D(s_local_exposure_grid, 2);
SAMPLER2D(s_local_exposure_blurred, 3);

/// x = highlight contrast scale, y = shadow contrast scale, z = detail strength,
/// w = blurred-luminance blend.
uniform vec4 u_local_exposure;
/// x = log2 of the view pre-exposure, y = middle grey bias in stops, z = 1 when local exposure
/// runs, w = min log2 luminance of the grid axis.
uniform vec4 u_local_exposure2;
/// x = 1 / log2 luminance range, y = slice count, z = tiles x, w = tiles y.
uniform vec4 u_local_exposure3;

#define u_local_highlight_contrast u_local_exposure.x
#define u_local_shadow_contrast    u_local_exposure.y
#define u_local_detail_strength    u_local_exposure.z
#define u_local_blurred_blend      u_local_exposure.w
#define u_log2_pre_exposure        u_local_exposure2.x
#define u_local_middle_grey_bias   u_local_exposure2.y
#define u_local_exposure_enabled   (u_local_exposure2.z > 0.5)
#define u_local_min_log_lum        u_local_exposure2.w
#define u_local_inv_log_range      u_local_exposure3.x
#define u_local_slices             u_local_exposure3.y
#define u_local_tiles_x            u_local_exposure3.z
#define u_local_tiles_y            u_local_exposure3.w

// log2(0.18): the pivot the contrast scales turn around sits at middle grey, shifted by the
// frame's applied exposure bias (AUTO_EXPOSURE.b) and the settings' own bias.
#define LOG2_MIDDLE_GREY -2.4739311883
// A slice holding less than this share of its tile's cells is not a measurement; the blurred
// level answers instead (UE's own fallback rule).
#define LOCAL_EXPOSURE_MIN_WEIGHT 0.001

/// One cell of the flattened grid: (sum of log2 luminance, sum of weight) for a tile's slice.
vec2 sample_local_grid_cell(int tile_x, int tile_y, int slice)
{
    int slices = int(u_local_slices);
    ivec2 texel = ivec2(tile_x * slices + slice, tile_y);
    return texelFetch(s_local_exposure_grid, texel, 0).xy;
}

/**
 * The pixel's local level in log2 SCENE luminance: a trilinear gather of the bilateral grid
 * over the two tiles and two slices around it, with the blurred level standing in wherever the
 * gathered weight is too thin to be a measurement. Hand-written because a hardware bilinear on
 * the flattened layout would blend across slice and tile boundaries indiscriminately.
 */
float compute_local_base_log(vec2 uv, float scene_log_lum, float blurred_log_lum)
{
    vec2 tile_coord = uv * vec2(u_local_tiles_x, u_local_tiles_y) - vec2_splat(0.5);
    vec2 tile_base = floor(tile_coord);
    vec2 tile_fraction = tile_coord - tile_base;
    ivec2 last_tile = ivec2(int(u_local_tiles_x) - 1, int(u_local_tiles_y) - 1);
    float last_slice = max(u_local_slices - 1.0, 1.0);
    float slice_position = saturate((scene_log_lum - u_local_min_log_lum) * u_local_inv_log_range) * last_slice;
    float slice_base = floor(slice_position);
    float slice_fraction = slice_position - slice_base;
    int slice_low = int(min(slice_base, last_slice));
    int slice_high = int(min(slice_base + 1.0, last_slice));

    vec2 accumulated = vec2_splat(0.0);
    for (int corner = 0; corner < 4; ++corner)
    {
        ivec2 tile = clamp(ivec2(tile_base) + ivec2(corner & 1, corner >> 1), ivec2(0, 0), last_tile);
        float tile_weight = mix(1.0 - tile_fraction.x, tile_fraction.x, float(corner & 1)) *
                            mix(1.0 - tile_fraction.y, tile_fraction.y, float(corner >> 1));
        accumulated += sample_local_grid_cell(tile.x, tile.y, slice_low) * (tile_weight * (1.0 - slice_fraction));
        accumulated += sample_local_grid_cell(tile.x, tile.y, slice_high) * (tile_weight * slice_fraction);
    }
    float bilateral = accumulated.y > LOCAL_EXPOSURE_MIN_WEIGHT ? accumulated.x / accumulated.y : blurred_log_lum;
    return mix(bilateral, blurred_log_lum, saturate(u_local_blurred_blend));
}

#define u_tonemappingExposure u_tonemapping.x
#define u_tonemappingMode     int(u_tonemapping.y)
#define u_dithering           u_tonemapping.z
#define u_midgray_match       u_tonemapping.w
#define u_contrast            u_grading.x
#define u_saturation          u_grading.y
#define u_grain_amount        u_grading.z
#define u_grain_seed          u_grading.w
#define u_vignette_intensity  u_vignette.x
#define u_vignette_smoothness u_vignette.y

// Color grading in LINEAR space, on post-exposure values (the contrast pivot
// only means "18% mid-gray" after exposure has normalized the scene).
vec3 apply_color_grading(vec3 color)
{
    // White balance: von Kries scaling in CAT02 LMS. u_wb_lms is (1,1,1) at
    // neutral settings, computed on the CPU from temperature/tint.
    CONST(mat3) lin2lms = mtxFromRows3(
        vec3(3.90405e-1, 5.49941e-1, 8.92632e-3),
        vec3(7.08416e-2, 9.63172e-1, 1.35775e-3),
        vec3(2.31082e-2, 1.28021e-1, 9.36245e-1));
    CONST(mat3) lms2lin = mtxFromRows3(
        vec3( 2.85847e+0, -1.62879e+0, -2.48910e-2),
        vec3(-2.10182e-1,  1.15820e+0,  3.24281e-4),
        vec3(-4.18120e-2, -1.18169e-1,  1.06867e+0));
    vec3 lms = mul(lin2lms, color);
    lms *= u_wb_lms.xyz;
    color = max(mul(lms2lin, lms), vec3_splat(0.0));

    // Log-space contrast around 18% gray: mids keep their exposure while the
    // stops above/below expand (>1) or compress (<1).
    // The exponent must be a vec3: glsl-optimizer folds exp2(log2(x) * y)
    // back into pow(x, y), and a scalar y yields pow(vec3, float) - an
    // overload GLSL does not have (C1115 on NVIDIA GL at runtime).
    const float mid_gray = 0.18;
    vec3 log_c = log2(max(color, vec3_splat(1e-6)) / mid_gray);
    color = exp2(log_c * vec3_splat(u_contrast)) * mid_gray;

    // Saturation around Rec.709 luma, still in linear.
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = max(vec3_splat(luma) + (color - luma) * u_saturation, vec3_splat(0.0));

    return color;
}

void main()
{
    vec3 color = texture2D(s_input, v_texcoord0).rgb;

    float exposure = u_tonemappingExposure;
    vec4 exposure_texel = texture2DLod(s_exposure, vec2(0.5, 0.5), 0.0);
    float adapted = exposure_texel.r;
    if ((adapted != adapted) || adapted <= 0.0 || adapted >= 1.0e10)
    {
        adapted = 1.0;
    }
    exposure *= max(adapted, 1e-5);

    // LOCAL EXPOSURE, per pixel, on top of the global one (UE PostProcessTonemap.usf:411-423):
    // the neighbourhood level is pulled toward the middle-grey pivot by the contrast scales and
    // the pixel's own detail is added back, so a sunlit window and the room around it can both
    // sit in range without flattening either.
    float local_exposure = 1.0;
    if (u_local_exposure_enabled)
    {
        float scene_luminance = max(dot(color, vec3(0.2126, 0.7152, 0.0722)), 1e-30);
        // The input carries the view pre-exposure; the grid was binned on scene luminance.
        float scene_log_lum = log2(scene_luminance) - u_log2_pre_exposure;
        float blurred_log_lum = texture2DLod(s_local_exposure_blurred, v_texcoord0, 0.0).x;
        float base_log_lum = compute_local_base_log(v_texcoord0, scene_log_lum, blurred_log_lum);
        // Into the same display-referred space the pivot lives in: the pixel's own exposure,
        // which is the global exposure with the pre-exposure taken back out.
        float display_exposure_log = log2(max(exposure, 1e-10)) + u_log2_pre_exposure;
        float luminance_log = scene_log_lum + display_exposure_log;
        float base_log = base_log_lum + display_exposure_log;
        float pivot = LOG2_MIDDLE_GREY + exposure_texel.b + u_local_middle_grey_bias;
        // Highlights and shadows are the two sides of the pivot, as UE splits them.
        float contrast = base_log > pivot ? u_local_highlight_contrast : u_local_shadow_contrast;
        float target_log = pivot + (base_log - pivot) * contrast +
                           (luminance_log - base_log) * u_local_detail_strength;
        local_exposure = exp2(target_log - luminance_log);
    }

    // Exposure first so grading operates in post-AE space, then the tone curve.
    // u_midgray_match stays 1: operators keep their native response. Display-white
    // comes from Auto Exposure Compensation, not from remapping curves onto AgX.
    color *= exposure * local_exposure;
    color = apply_color_grading(color);

    // Vignette in linear: behaves like lens light falloff, so darkened
    // highlights still roll through the tone curve instead of graying out.
    if (u_vignette_intensity > 0.0)
    {
        float dist = length((v_texcoord0 - 0.5) * 2.0);
        float start = mix(0.8, 0.05, saturate(u_vignette_smoothness));
        float falloff = smoothstep(start, 1.55, dist);
        color *= 1.0 - u_vignette_intensity * falloff;
    }

    color = apply_tonemapping(color, u_tonemappingMode, u_midgray_match);

    // Lift/gamma/gain on the display-referred image (classic video grading):
    // lift offsets the toe and fades toward white, gain scales the top end,
    // gamma bends the mids. Neutral uniforms make this an exact identity.
    color = max(color * u_gain.xyz + u_lift.xyz * (1.0 - color), vec3_splat(0.0));
    color = pow(color, u_gamma_inv.xyz);

    // Grain and TPDF dither: skipped (uniforms zeroed) when FXAA follows so
    // the AA filter does not smear them; FXAA applies the same helper after.
    color = apply_output_noise(color, gl_FragCoord.xy, u_grain_amount, u_grain_seed, u_dithering);

    gl_FragColor = vec4(color, 1.0f);
}
