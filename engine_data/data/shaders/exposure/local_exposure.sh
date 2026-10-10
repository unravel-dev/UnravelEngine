#ifndef LOCAL_EXPOSURE_SH_HEADER_GUARD
#define LOCAL_EXPOSURE_SH_HEADER_GUARD

/*
 * LOCAL EXPOSURE applied per pixel: the shared lookup of every pass that scales scene colour by
 * it (the tonemapper for the scene, the bloom's first downsample for the bloom source). Both must
 * measure the SAME image the grid was built from - the scene before bloom - or a pixel's
 * luminance lands in grid slices that hold no data and snaps to the blurred level along
 * luminance contours.
 *
 * The grid is the flattened 3D atlas cs_local_exposure_grid.sc writes - texel (tile_x * slices +
 * slice, tile_y), rg = the slice's raw sum of log2 luminance and sum of weight - read with
 * texelFetch; the blurred texture is the 1/32 resolution Gaussian of the log luminance
 * (cs_local_exposure_blur.sc), read with a bilinear sampler. Both are bound as 1x1 stand-ins when
 * local exposure is off, and the enable lane keeps them unread. Includers keep sampler stages
 * 2 and 3 free for them (local_exposure_uniforms on the C++ side binds them).
 */

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
/// xy = screen uv -> tile grid uv scale, zw = screen uv -> blurred texture uv scale: the share of
/// each the view covers, their last tile / texel being partial.
uniform vec4 u_local_exposure4;

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
#define u_local_grid_uv_scale      u_local_exposure4.xy
#define u_local_blurred_uv_scale   u_local_exposure4.zw

// log2(0.18): the pivot the contrast scales turn around sits at middle grey, shifted by the
// frame's applied exposure bias (AUTO_EXPOSURE.b) and the settings' own bias.
#define LOCAL_EXPOSURE_LOG2_MIDDLE_GREY -2.4739311883
// A gathered weight below this is no measurement (the grid holds raw cell counts, so this means
// no cell at all); the blurred level answers instead.
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
    vec2 tile_coord = uv * u_local_grid_uv_scale * vec2(u_local_tiles_x, u_local_tiles_y) - vec2_splat(0.5);
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

/**
 * The local exposure factor of a pre-exposed SCENE colour at screen @p uv: the neighbourhood
 * level is pulled toward the middle-grey pivot by the contrast scales and the pixel's own detail
 * is added back, so a sunlit window and the room around it can both sit in range without
 * flattening either. 1 when local exposure is off.
 *
 * @p display_exposure_log is log2 of the scale from scene luminance to the exposed image (the
 * manual exposure times the adapted exposure, pre-exposure excluded); @p applied_bias is the
 * frame's exposure bias in stops (AUTO_EXPOSURE.b).
 */
float compute_local_exposure(vec3 pre_exposed_color, vec2 uv, float display_exposure_log, float applied_bias)
{
    if (!u_local_exposure_enabled)
    {
        return 1.0;
    }
    // The histogram's luminance (uniform weights, floored at the bottom of its range): the grid
    // was binned on it, so the pixel is measured with the same function.
    float scene_luminance = max(dot(pre_exposed_color, vec3_splat(1.0 / 3.0)), 1e-30);
    float scene_log_lum = max(log2(scene_luminance) - u_log2_pre_exposure, u_local_min_log_lum);
    float blurred_log_lum = texture2DLod(s_local_exposure_blurred, uv * u_local_blurred_uv_scale, 0.0).x;
    float base_log_lum = compute_local_base_log(uv, scene_log_lum, blurred_log_lum);
    // Into the display-referred space the pivot lives in.
    float luminance_log = scene_log_lum + display_exposure_log;
    float base_log = base_log_lum + display_exposure_log;
    float pivot = LOCAL_EXPOSURE_LOG2_MIDDLE_GREY + applied_bias + u_local_middle_grey_bias;
    // Highlights and shadows are the two sides of the pivot, each with its own contrast scale.
    float contrast = base_log > pivot ? u_local_highlight_contrast : u_local_shadow_contrast;
    float target_log = pivot + (base_log - pivot) * contrast + (luminance_log - base_log) * u_local_detail_strength;
    return exp2(target_log - luminance_log);
}

#endif // LOCAL_EXPOSURE_SH_HEADER_GUARD
