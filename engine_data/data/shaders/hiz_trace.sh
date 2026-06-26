/*
 * Shared Hi-Z hierarchical ray marching functions.
 * Used by both SSR and SSIL passes.
 *
 * Requirements: the including shader must define:
 *   - SAMPLER2D(s_hiz, N)      -- Hi-Z depth mip chain
 *   - computeViewSpacePosition -- from common.sh / lighting.sh
 *
 * HizScreenPassToFullResUV: map reduced-res trace pass UV to full-res G-buffer/Hi-Z UV (SSR, SSIL).
 */

#ifndef __HIZ_TRACE_SH__
#define __HIZ_TRACE_SH__

#define FFX_SSSR_FLOAT_MAX 3.402823466e+38

vec3 HizComputeViewspacePosition(vec2 uv, float z)
{
    return computeViewSpacePosition(uv, z);
}

vec2 HizGetDepthMipResolution(sampler2D hiz_sampler, int mipLevel)
{
    return vec2(textureSize(hiz_sampler, mipLevel));
}

float HizFetchDepth(sampler2D hiz_sampler, vec2 coords, int mipLevel)
{
    return texelFetch(hiz_sampler, ivec2(coords), mipLevel).r;
}

void HizInitialAdvanceRay(vec3 ss_ray_origin,
                          vec3 ss_ray_dir,
                          vec3 ss_ray_dir_inv,
                          vec2 curr_mip_resolution,
                          vec2 curr_mip_resolution_inv,
                          vec2 floor_offset,
                          vec2 uv_offset,
                          out vec3 ss_pos,
                          out float curr_t)
{
    vec2 curr_mip_pos = curr_mip_resolution * ss_ray_origin.xy;
    vec2 xy_plane = floor(curr_mip_pos) + floor_offset;
    xy_plane = xy_plane * curr_mip_resolution_inv + uv_offset;
    vec2 t = xy_plane * ss_ray_dir_inv.xy - ss_ray_origin.xy * ss_ray_dir_inv.xy;
    curr_t = min(t.x, t.y);
    ss_pos = ss_ray_origin + curr_t * ss_ray_dir;
}

bool HizAdvanceRay(vec3 ss_ray_origin,
                   vec3 ss_ray_dir,
                   vec3 ss_ray_dir_inv,
                   vec2 curr_mip_pos,
                   vec2 curr_mip_resolution_inv,
                   vec2 floor_offset,
                   vec2 uv_offset,
                   float surface_z,
                   inout vec3 ss_pos,
                   inout float curr_t)
{
    vec2 xy_plane = floor(curr_mip_pos) + floor_offset;
    xy_plane = xy_plane * curr_mip_resolution_inv + uv_offset;
    vec3 boundary_planes = vec3(xy_plane, surface_z);
    vec3 t = boundary_planes * ss_ray_dir_inv - ss_ray_origin * ss_ray_dir_inv;

#ifdef INVERTED_DEPTH_RANGE
    t.z = ss_ray_dir.z < 0.0 ? t.z : FFX_SSSR_FLOAT_MAX;
#else
    t.z = ss_ray_dir.z > 0.0 ? t.z : FFX_SSSR_FLOAT_MAX;
#endif

    float t_min = min(min(t.x, t.y), t.z);

#ifdef INVERTED_DEPTH_RANGE
    bool above_surface = surface_z < ss_pos.z;
#else
    bool above_surface = surface_z > ss_pos.z;
#endif

#if BGFX_SHADER_LANGUAGE_GLSL
    // Avoid floatBitsToUint in GLSL fragment shaders: shaderc promotes any fragment shader
    // using bit reinterpretation builtins to GLSL 430, where legacy MRT writes are removed.
    // t_min is selected directly from t.x/t.y/t.z, so equality is stable for this branch.
    bool skipped_tile = t_min != t.z && above_surface;
#else
    bool skipped_tile = floatBitsToUint(t_min) != floatBitsToUint(t.z) && above_surface;
#endif
    curr_t = above_surface ? t_min : curr_t;
    ss_pos = ss_ray_origin + curr_t * ss_ray_dir;

    return skipped_tile;
}

bool HizHierarchicalRaymarch(sampler2D hiz_sampler,
                             vec3 ss_ray_origin,
                             vec3 ss_ray_dir,
                             vec2 screen_size,
                             int most_detailed_mip,
                             int max_iterations,
                             inout vec3 ss_hit_pos)
{
    vec3 ss_ray_dir_inv;
    ss_ray_dir_inv.x = (ss_ray_dir.x != 0.0) ? rcp(ss_ray_dir.x) : FFX_SSSR_FLOAT_MAX;
    ss_ray_dir_inv.y = (ss_ray_dir.y != 0.0) ? rcp(ss_ray_dir.y) : FFX_SSSR_FLOAT_MAX;
    ss_ray_dir_inv.z = (ss_ray_dir.z != 0.0) ? rcp(ss_ray_dir.z) : FFX_SSSR_FLOAT_MAX;

    int curr_mip = most_detailed_mip;
    vec2 curr_mip_resolution = HizGetDepthMipResolution(hiz_sampler, curr_mip);
    vec2 curr_mip_resolution_inv = rcp(curr_mip_resolution);

    vec2 uv_offset = 0.005 * exp2(most_detailed_mip) / screen_size;
    uv_offset.x = ss_ray_dir.x < 0.0 ? -uv_offset.x : uv_offset.x;
    uv_offset.y = ss_ray_dir.y < 0.0 ? -uv_offset.y : uv_offset.y;

    vec2 floor_offset;
    floor_offset.x = (ss_ray_dir.x < 0.0) ? 0.0 : 1.0;
    floor_offset.y = (ss_ray_dir.y < 0.0) ? 0.0 : 1.0;

    float curr_t;
    HizInitialAdvanceRay(ss_ray_origin, ss_ray_dir, ss_ray_dir_inv,
                         curr_mip_resolution, curr_mip_resolution_inv,
                         floor_offset, uv_offset,
                         ss_hit_pos, curr_t);

    int i = 0;
    LOOP while(i < max_iterations && curr_mip >= most_detailed_mip)
    {
        vec2 curr_mip_pos = curr_mip_resolution * ss_hit_pos.xy;
        float surface_z = HizFetchDepth(hiz_sampler, curr_mip_pos, curr_mip);
        bool skipped_tile = HizAdvanceRay(ss_ray_origin, ss_ray_dir, ss_ray_dir_inv,
                                          curr_mip_pos, curr_mip_resolution_inv,
                                          floor_offset, uv_offset,
                                          surface_z, ss_hit_pos, curr_t);

        curr_mip += skipped_tile ? 1 : -1;
        curr_mip_resolution *= skipped_tile ? 0.5 : 2.0;
        curr_mip_resolution_inv *= skipped_tile ? 2.0 : 0.5;

        i++;
    }

    return i < max_iterations;
}

/// Validate a screen-space hit for indirect lighting.
/// Returns confidence in [0,1].  Rejects background, self-intersection, and backfaces.
/// vs_hit_pos must be pre-computed by the caller via HizComputeViewspacePosition.
///
/// The function always validates against the FULL-RESOLUTION (mip 0) Hi-Z depth (the
/// conservative coarser mips give worst-case bounds that are not appropriate for the
/// pixel-accurate self-intersection / surface re-fetch test). It computes the mip-0
/// dimensions itself so that callers tracing from a non-zero base mip (e.g. SSIL starting
/// the hierarchical march at mip 1 for performance) still get a correctly-scaled self-hit
/// reject and a correctly-indexed depth refetch -- previously this used the caller's
/// `screen_size` for both, which silently corrupted both gates when the caller's screen
/// size was not the mip-0 resolution.
float HizValidateHit(sampler2D hiz_sampler,
                     sampler2D normal_sampler,
                     vec3 ss_hit_pos,
                     vec2 uv,
                     vec3 vs_ray_origin,
                     vec3 vs_hit_pos,
                     float depth_tolerance)
{
    BRANCH
    if(any(lessThan(ss_hit_pos.xy, vec2_splat(0.0))) || any(greaterThan(ss_hit_pos.xy, vec2_splat(1.0))))
        return 0.0;

    vec2 mip0_size = HizGetDepthMipResolution(hiz_sampler, 0);
    vec2 manhattan_dist = abs(ss_hit_pos.xy - uv);
    vec2 inv_mip0_size = rcp(mip0_size);

    BRANCH
    if(all(lessThan(manhattan_dist, inv_mip0_size * 0.5)))
        return 0.0;

    float surface_z = HizFetchDepth(hiz_sampler, mip0_size * ss_hit_pos.xy, 0);

    BRANCH
#ifdef INVERTED_DEPTH_RANGE
    if(surface_z == 0.0)
        return 0.0;
#else
    if(surface_z == 1.0)
        return 0.0;
#endif

    vec3 vs_ray_dir = vs_hit_pos - vs_ray_origin;

    GBufferDataNormalMetalRoughness normal_data = DecodeGBufferNormalMetalRoughness(ss_hit_pos.xy, normal_sampler);
    vec3 vs_normal = mul(u_view, vec4(normal_data.world_normal, 0.0)).xyz;

    if(dot(vs_ray_dir, vs_normal) > 0.0)
        return 0.0;

    vec3 vs_hit_surface = HizComputeViewspacePosition(ss_hit_pos.xy, surface_z);
    float dist = length(vs_hit_pos - vs_hit_surface);

    float confidence = 1.0 - smoothstep(0.0, depth_tolerance, dist);

    vec2 fade_in = vec2(0.1, 0.2);
    vec2 border = smoothstep(vec2_splat(0.0), fade_in, ss_hit_pos.xy)
                * (1.0 - smoothstep(1.0 - fade_in, vec2_splat(1.0), ss_hit_pos.xy));

    return clamp(confidence * border.x * border.y, 0.0, 1.0);
}

/// Project a view-space direction to a screen-space direction vector.
vec3 HizProjectVsDirToSsDir(vec3 vs_pos, vec3 vs_dir, vec3 ss_origin)
{
    vec3 end_point = vs_pos + vs_dir;
    vec4 ss_pj4 = mul(u_proj, vec4(end_point, 1.0));
    vec3 ss_pj = ss_pj4.xyz / ss_pj4.w;
    ss_pj = clipTransform(ss_pj);
    ss_pj.xy = ss_pj.xy * 0.5 + 0.5;

#if BGFX_SHADER_LANGUAGE_GLSL
    ss_pj.z = ss_pj.z * 0.5 + 0.5;
#endif

    return ss_pj - ss_origin;
}

/// Map normalized UV in a reduced-resolution screen pass to full-res G-buffer / Hi-Z UV
/// (block centre, biased for FP-safe integer casts).
///
/// `resolution_scale` is PER-AXIS: (full_w / pass_w, full_h / pass_h).
///
/// At ODD full-res dimensions the per-axis float scale isn't integer (e.g.
/// 1233/616 = 2.00162). If we map via the float scale directly --
/// `(pass_px * scale + 0.5) / full_dim` -- then `uv * full_dim`'s fractional part drifts
/// across the screen. Two failure modes follow:
///
///   1. The drift reaches EXACTLY 0 at the middle pass pixel (i = pass_dim/2), making
///      the product `617.0` (etc.) integer-exact. `int()` / `ivec()` casts inside the
///      trace then tip either side of that integer from FP roundoff, while neighbour
///      columns cast unambiguously to the lower integer. The result is a one-column
///      stride discontinuity in the integer PCG seed sequence and the Hi-Z `texelFetch`
///      index, visible as a sharp seam through screen center.
///   2. The smooth bilinear-weight drift across the screen makes some columns sample
///      the gbuffer sharply (frac ~ 0 or ~ 1) and others smoothly (frac ~ 0.5). For
///      surfaces with high-frequency normal/colour detail this manifests as moire.
///
/// We instead anchor everything to INTEGER math on the pass index: take the integer
/// divisor `D = floor(scale)` (always 2/4/8 for trace_resolution::half/quarter/eighth)
/// and map pass pixel `i` to the centre of a representative full-res texel inside its
/// D-wide block, i.e. `full_px = D*i + floor(D/2) + 0.5`. The float scale never enters
/// the per-pixel math; only the integer divisor does. This gives a clean STRIDE-D pattern
/// across the screen at any full-res dimension (even or odd), while keeping depth
/// texelFetch and filtered G-buffer samples on the same full-res texel.
vec2 HizScreenPassToFullResUV(vec2 pass_uv, vec2 resolution_scale, vec2 full_buffer_dim)
{
    BRANCH
    if(all(lessThan(resolution_scale, vec2_splat(1.5))))
    {
        return pass_uv;
    }
    vec2 pass_dim = full_buffer_dim / resolution_scale;
    vec2 pass_px = floor(pass_uv * pass_dim);
    pass_px = max(pass_px, vec2_splat(0.0));
    vec2 divisor = floor(resolution_scale);
    vec2 full_px = divisor * pass_px + floor(0.5 * divisor) + vec2_splat(0.5);
    return clamp(full_px / full_buffer_dim,
                 vec2_splat(1e-5), vec2_splat(1.0 - 1e-5));
}

#endif // __HIZ_TRACE_SH__
