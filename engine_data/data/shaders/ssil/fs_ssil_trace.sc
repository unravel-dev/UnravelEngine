$input v_texcoord0

/*
 * Screen-Space Indirect Lighting (SSIL) trace pass.
 *
 * Traces cosine-weighted hemisphere rays per pixel using Hi-Z hierarchical
 * ray marching, then samples radiance at hit points. Rays that escape on-screen
 * geometry fall back to the environment SH radiance in their direction, so the
 * cosine-weighted mean estimates the full (screen-occluded) hemispherical
 * irradiance rather than only the on-screen bounce. RGB is left in radiance-mean
 * units; the consumer applies the PI factor that converts it to irradiance units
 * (matching eval_irradiance_sh) at blend time. Alpha carries the screen-hit
 * coverage/replacement weight so SSIL degrades gracefully to the SH probe where
 * the trace is mostly environment fallback.
 * Supports multi-bounce by feeding back the previous frame's denoised SSIL
 * output, attenuated by the hit surface's diffuse albedo for energy conservation.
 */

#include "../common.sh"
#include "../lighting.sh"
#include "../hiz_trace.sh"
#include "../sampling.sh"

SAMPLER2D(s_color, 0);
SAMPLER2D(s_normal, 1);
SAMPLER2D(s_hiz, 2);
SAMPLER2D(s_emissive, 3);
SAMPLER2D(s_albedo, 4);
SAMPLER2D(s_prev_ssil, 5);
// Environment SH (9 radiance coeffs x 3 channels); sampled per ray as the miss fallback.
SAMPLER2D(s_irradiance, 6);

uniform vec4 u_ssil_params;
#define u_max_steps       u_ssil_params.x
#define u_max_rays        u_ssil_params.y
#define u_depth_tolerance u_ssil_params.z
/// Gain applied to the on-screen BOUNCE only (not the environment fallback), so toggling
/// SSIL leaves the unoccluded ambient level matched to the SH probe and brightness acts
/// as an indirect-bounce strength control.
#define u_brightness      u_ssil_params.w

uniform vec4 u_ssil_params2;
#define u_max_distance    u_ssil_params2.x
#define u_frame_index     u_ssil_params2.y
#define u_multi_bounce    u_ssil_params2.z
/// > 0 enables the SH environment radiance fallback for rays that miss on-screen
/// geometry (0 on the first frame before the SH coefficients exist).
#define u_env_intensity   u_ssil_params2.w

uniform vec4 u_ssil_params3;
/// View-space acceptance-band ("thickness") added on top of u_depth_tolerance, scaled by
/// the hit distance. Far hits resolve against a coarser Hi-Z depth, so a fixed band over-
/// rejects them and leaks environment light; widening it with distance reduces that leak.
#define u_thickness       u_ssil_params3.x

// Spatial ray stratification tile. The hemisphere strata are spread across an
// SSIL_STRATA_TILE x SSIL_STRATA_TILE block of pixels so that a low per-pixel ray count
// still integrates SSIL_STRATA_TILE^2 distinct directions across a denoiser footprint
// (the per-pixel noise becomes anti-correlated with its neighbours and averages out under
// the spatial + temporal filters). Each pixel's mean stays an unbiased estimate because it
// is still an average of independent cosine-importance samples.
#define SSIL_STRATA_TILE  2
#define SSIL_STRATA_COUNT (SSIL_STRATA_TILE * SSIL_STRATA_TILE)

/// xy = full G-buffer size (pixels); zw = per-axis (full_dim / trace_dim) scale.
/// Per-axis is required because odd full-res W with even full-res H produces different
/// X and Y ratios; see HizScreenPassToFullResUV docs in hiz_trace.sh.
uniform vec4 u_ssil_resolution;

#define BASE_LOD 0

/// Cosine-weighted hemisphere sample around N (tangent space -> world).
vec3 ImportanceSampleCosine(vec2 E, vec3 N)
{
    float phi = 2.0 * PI * E.x;
    float cos_theta = sqrt(1.0 - E.y);
    float sin_theta = sqrt(E.y);

    vec3 H;
    H.x = cos(phi) * sin_theta;
    H.y = sin(phi) * sin_theta;
    H.z = cos_theta;

    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    return tangent * H.x + bitangent * H.y + N * H.z;
}

/// Sample total outgoing radiance at a hit point: direct lighting + emissive + the hit
/// surface's own ambient (sky/probe) bounce + optional multi-bounce feedback.
vec3 SampleRadiance(vec2 hit_uv)
{
    vec3 direct = texture2DLod(s_color, hit_uv, 0.0).rgb;

    GBufferDataEmissive ed = DecodeGBufferEmissiveLod(hit_uv, s_emissive, 0.0);
    GBufferDataColorAndAO cd = DecodeGBufferColorAndAOLod(hit_uv, s_albedo, 0.0);
    GBufferDataNormalMetalRoughness nd = DecodeGBufferNormalMetalRoughnessLod(hit_uv, s_normal, 0.0);
    vec3 hit_diffuse = cd.base_color * (1.0 - nd.metalness);

    vec3 radiance = direct + ed.emissive_color;

    // Hit surface's own ambient bounce. s_color holds DIRECT lighting only (SSIL runs
    // before the indirect pass), so a shadowed occluder reads ~black there. Without this
    // term, screen-space occlusion of the environment collapses any pixel whose rays all
    // hit such occluders to black (the "black spots", worst at reduced trace res where a
    // dark texel covers a whole block). The occluder is really lit by the sky/probe, so add
    // its Lambertian ambient response (albedo/PI * irradiance * AO) -- this gives soft,
    // albedo-tinted ambient occlusion instead of pure black.
    BRANCH
    if(u_env_intensity > 0.0)
    {
        vec3 hit_ambient = eval_irradiance_sh(s_irradiance, nd.world_normal) * RECIP_PI * u_env_intensity;
        radiance += hit_diffuse * hit_ambient * cd.ambient_occlusion;
    }

    BRANCH
    if(u_multi_bounce > 0.0)
    {
        // Weight the fed-back indirect by its final blend weight, matching how the
        // lighting pass blends SSIL against the SH probe.
        vec4 prev = texture2DLod(s_prev_ssil, hit_uv, 0.0);
        vec3 prev_indirect = prev.rgb * prev.a;

        radiance += hit_diffuse * prev_indirect * u_multi_bounce;
    }

    return radiance;
}

void main()
{
    vec2 uv = HizScreenPassToFullResUV(v_texcoord0,
                                       max(u_ssil_resolution.zw, vec2_splat(1.0)),
                                       u_ssil_resolution.xy);

    GBufferDataNormalMetalRoughness nd = DecodeGBufferNormalMetalRoughness(uv, s_normal);
    vec3 world_normal = nd.world_normal;

    vec2 screen_size = HizGetDepthMipResolution(s_hiz, BASE_LOD);
    float surface_z = HizFetchDepth(s_hiz, screen_size * uv, BASE_LOD);

    // Skip sky pixels
    BRANCH
#ifdef INVERTED_DEPTH_RANGE
    if(surface_z == 0.0)
#else
    if(surface_z == 1.0)
#endif
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    vec3 vs_normal = mul(u_view, vec4(world_normal, 0.0)).xyz;
    vec3 ss_ray_origin = vec3(uv, surface_z);
    vec3 vs_ray_origin = HizComputeViewspacePosition(uv, surface_z);

    int num_rays = int(u_max_rays);
    int max_steps = int(u_max_steps);
    int frame_idx = int(u_frame_index);

    vec2 scaled_uv = uv * u_ssil_resolution.xy;
    uvec2 rnd = Rand3DPCG16(ivec3(scaled_uv, frame_idx)).xy;

    int ray_count = max(num_rays, 1);
    vec3 accumulated = vec3_splat(0.0);
    float total_hit_weight = 0.0;

    // Spatial stratum: which slice of the SSIL_STRATA_COUNT virtual sample budget this
    // pixel owns, picked by its position inside the SSIL_STRATA_TILE block. Neighbours own
    // different slices, so together the block covers the full hemisphere sequence. Use the
    // TRACE-pixel coordinate (gl_FragCoord), not scaled_uv: at reduced trace resolution
    // scaled_uv is the full-res block centre, whose parity is constant across trace pixels
    // and would collapse every pixel onto the same stratum.
    ivec2 pix = ivec2(gl_FragCoord.xy);
    int stratum = (pix.x & (SSIL_STRATA_TILE - 1)) + (pix.y & (SSIL_STRATA_TILE - 1)) * SSIL_STRATA_TILE;
    int strata_budget = ray_count * SSIL_STRATA_COUNT;
    int strata_base = stratum * ray_count;

    LOOP for(int i = 0; i < ray_count; ++i)
    {
        vec2 E = Hammersley16(uint(strata_base + i), uint(strata_budget), rnd);
        vec3 vs_sample_dir = ImportanceSampleCosine(E, vs_normal);

        // Environment radiance along this ray (world space) -- the value the ray
        // integrates if it escapes on-screen geometry. Becomes the per-ray default so
        // the cosine-weighted mean of all rays estimates the FULL hemispherical
        // irradiance (occluded by what the screen can see) rather than only the bounce.
        vec3 ws_sample_dir = normalize(mul(u_invView, vec4(vs_sample_dir, 0.0)).xyz);
        vec3 env_radiance = (u_env_intensity > 0.0)
                                ? eval_radiance_sh(s_irradiance, ws_sample_dir) * u_env_intensity
                                : vec3_splat(0.0);
        vec3 ray_radiance = env_radiance;

        vec3 ss_ray_dir = HizProjectVsDirToSsDir(vs_ray_origin, vs_sample_dir, ss_ray_origin);

        BRANCH
        if(dot(ss_ray_dir.xy, ss_ray_dir.xy) >= 1e-12)
        {
            vec3 ss_hit_pos;
            bool valid_hit = HizHierarchicalRaymarch(s_hiz, ss_ray_origin, ss_ray_dir,
                                                      screen_size, BASE_LOD, max_steps, ss_hit_pos);

            BRANCH
            if(valid_hit)
            {
                vec3 vs_hit = HizComputeViewspacePosition(ss_hit_pos.xy, ss_hit_pos.z);
                float hit_dist = length(vs_hit - vs_ray_origin);

                BRANCH
                if(hit_dist < u_max_distance)
                {
                    // Distance-aware thickness: widen the view-space acceptance band with
                    // hit distance so coarse far-field Hi-Z depth does not falsely reject
                    // hits (which would leak the environment fallback through occluders).
                    float effective_tol = u_depth_tolerance +
                                          u_thickness * (hit_dist / max(u_max_distance, 1e-3));
                    float confidence = HizValidateHit(s_hiz, s_normal, ss_hit_pos, uv,
                                                       vs_ray_origin, vs_hit, screen_size, effective_tol);

                    BRANCH
                    if(confidence > 0.0)
                    {
                        // brightness scales the on-screen bounce only; the environment
                        // fallback stays at probe intensity so unoccluded ambient matches SH.
                        vec3 hit_color = SampleRadiance(ss_hit_pos.xy) * u_brightness;
                        float dist_atten = 1.0 - smoothstep(0.0, u_max_distance, hit_dist);
                        float w = clamp(confidence * dist_atten, 0.0, 1.0);
                        // A confident near hit occludes the environment and replaces it
                        // with the bounce; a weak/distant hit only partially occludes it.
                        ray_radiance = mix(env_radiance, hit_color, w);
                        total_hit_weight += w;
                    }
                }
            }
        }

        accumulated += ray_radiance;
    }

    vec3 result = accumulated / float(ray_count);
    // result stays in radiance-mean units (NOT * PI). The PI factor that puts it in
    // irradiance units (matching eval_irradiance_sh) is applied by the consumer at blend time.
    float confidence = clamp(total_hit_weight / float(ray_count), 0.0, 1.0);

    gl_FragColor = vec4(result, confidence);
}
