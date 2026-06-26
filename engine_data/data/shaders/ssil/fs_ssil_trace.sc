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

// Hi-Z mip the hierarchical raymarch walks down to. Mip 0 (full-res) is the only safe
// choice for an SSIL implementation feeding directly into the lighting consumer: the
// coarser mips snap hit positions to a 2x2-or-larger full-res tile, which on shading
// surfaces produces low-frequency variation that survives the spatial denoiser (the
// 5x5 a-trous kernel smooths INSIDE each tile but the tile-boundary discontinuities
// remain, visible as block-shaped indirect-colour blotches on otherwise smooth walls).
// The perf cost of starting at mip 0 is real (~30% more depth fetches in the trace) but
// the visual cost of any coarser start mip is much worse than the perf saving. Revisit
// only with a stronger denoiser that can re-smooth the tile boundaries.
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

    // Strip the metallic contribution from the direct-lighting bounce source.
    // LBUFFER stores diffuse + specular per light. Metals have ZERO diffuse: their
    // base_color is the specular tint and (1 - metalness) = 0 in the BRDF. Feeding the
    // metallic specular term -- which is camera-direction-dependent -- into a DIFFUSE
    // indirect estimator is both physically wrong (the specular bounce belongs to the
    // SSR pipeline, not SSIL) and a steady shimmer source: any small camera motion
    // shifts the hit pixel's specular highlight and the SSIL output dances with it.
    // For dielectrics (metalness ~ 0) the strip is a no-op; the ~4% Fresnel specular
    // surviving in LBUFFER is small enough that the temporal accumulator absorbs its
    // shimmer. A proper diffuse-only LBUFFER MRT would remove it for dielectrics too --
    // see the indirect-lighting pass for the eventual refactor target.
    direct *= (1.0 - nd.metalness);

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
        // lighting pass blends SSIL against the SH probe. SSIL history stores a radiance
        // mean; multiply by PI to convert it to irradiance before Lambertian bounce.
        vec4 prev = texture2DLod(s_prev_ssil, hit_uv, 0.0);
        vec3 prev_indirect = prev.rgb * PI * prev.a;

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

    // Per-pixel scramble for the Hammersley sequence. Driven by Interleaved Gradient
    // Noise instead of a PCG hash so the residual per-frame noise has a SCREEN-SPACE
    // BLUE-NOISE distribution: most of its energy sits in high spatial frequencies that
    // the a-trous spatial denoiser eliminates, with very little leaking into low
    // frequencies that would survive as blotches. The FrameId axis is independently
    // jittered so the spatial pattern also decorrelates across frames, which the
    // temporal accumulator integrates into a clean mean.
    vec2 scaled_uv = uv * u_ssil_resolution.xy;

    int ray_count = max(num_rays, 1);
    vec3 accumulated = vec3_splat(0.0);
    float total_hit_weight = 0.0;

    // Per-ray luminance ceiling -- source-level firefly suppression.
    //
    // The temporal pass has a moments-driven clamp that catches outliers once history
    // matures, but until then any single bright hit (sub-pixel emissive texel, an
    // unshadowed sky-disk fragment caught by a one-pixel hole, a near-miss ray that
    // grazes a glossy highlight in s_color) lands in the running mean AND pollutes the
    // moments themselves -- the very statistics the temporal clamp will rely on later.
    // Disocclusion, scene cuts, and freshly enabled SSIL all hit this same window where
    // the moments-clamp is dormant. Newly visible pixels then spend several frames
    // bleeding bright streaks until the polluted moments converge.
    //
    // Capping each ray to a multiple of the local SH irradiance kills the outlier at
    // the source, before it ever reaches the moments. The cap reference is the SH
    // probe -- a coarse but stable irradiance estimate of the local environment -- so
    // bright scenes get a high cap and dark scenes get a low cap. The 8x multiplier
    // preserves any legitimate diffuse bounce up to an order of magnitude above the
    // average (a sun-lit wall hit, a bright albedo, etc.) and only trims hits that are
    // by definition non-representative of the diffuse signal we are integrating.
    // The absolute floor stops the cap collapsing toward zero in night/dark scenes,
    // which would over-clamp legitimate small-scale bright detail.
    #define SSIL_FIREFLY_RATIO 8.0
    #define SSIL_FIREFLY_FLOOR 0.5
    float firefly_cap_luma = SSIL_FIREFLY_FLOOR;
    BRANCH
    if(u_env_intensity > 0.0)
    {
        vec3 reference_irradiance = eval_irradiance_sh(s_irradiance, world_normal) *
                                    RECIP_PI * u_env_intensity;
        firefly_cap_luma = max(SSIL_FIREFLY_FLOOR,
                               SSIL_FIREFLY_RATIO * Luminance(reference_irradiance));
    }

    // Spatial stratum: which slice of the SSIL_STRATA_COUNT virtual sample budget this
    // pixel owns, picked by its position inside the SSIL_STRATA_TILE block. Neighbours own
    // different slices, so together the block covers the full hemisphere sequence. Use the
    // TRACE-pixel coordinate (gl_FragCoord), not scaled_uv: at reduced trace resolution
    // scaled_uv is the full-res block centre, whose parity is constant across trace pixels
    // and would collapse every pixel onto the same stratum.
    //
    // No temporal stratum rotation: we considered cycling each pixel's stratum across
    // frames so the temporal mean converges to the full hemispherical integral instead of
    // its single stratum's mean. The convergence math holds, but the per-frame between-
    // stratum jump (one stratum hits a bright wall, another hits sky) is much larger than
    // the within-stratum jitter the temporal accumulator is sized for. With effective
    // history weight ~16 the cycle is visible as 15 Hz "dancing" -- exactly the perceptual-
    // flicker band. The 5x5 a-trous denoiser footprint already spans all
    // SSIL_STRATA_COUNT neighbours, so cross-stratum averaging happens spatially even
    // without temporal rotation. The static stratum bias that remains per-pixel is tiny
    // because the spatial denoise mixes neighbour strata into every output pixel.
    ivec2 pix = ivec2(gl_FragCoord.xy);
    int stratum = (pix.x & (SSIL_STRATA_TILE - 1)) +
                  (pix.y & (SSIL_STRATA_TILE - 1)) * SSIL_STRATA_TILE;
    int strata_budget = ray_count * SSIL_STRATA_COUNT;
    int strata_base = stratum * ray_count;

    LOOP for(int i = 0; i < ray_count; ++i)
    {
        vec2 E = Hammersley16_IGN(uint(strata_base + i), uint(strata_budget),
                                  scaled_uv, float(frame_idx));
        vec3 vs_sample_dir = ImportanceSampleCosine(E, vs_normal);

        vec3 hit_radiance = vec3_splat(0.0);
        float hit_weight = 0.0;

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
                                                       vs_ray_origin, vs_hit, effective_tol);

                    BRANCH
                    if(confidence > 0.0)
                    {
                        // brightness scales the on-screen bounce only; the environment
                        // fallback stays at probe intensity so unoccluded ambient matches SH.
                        vec3 hit_color = SampleRadiance(ss_hit_pos.xy) * u_brightness;

                        // Source-level firefly cap. Only the on-screen bounce can produce
                        // sub-pixel outliers (the environment fallback is the SH probe,
                        // already a low-frequency irradiance estimate that cannot
                        // firefly), so we cap only the bounce. See firefly_cap_luma
                        // derivation comment above the ray loop.
                        float hit_luma = Luminance(hit_color);
                        BRANCH
                        if(hit_luma > firefly_cap_luma)
                        {
                            hit_color *= firefly_cap_luma / max(hit_luma, 1e-4);
                        }

                        float dist_atten = 1.0 - smoothstep(0.0, u_max_distance, hit_dist);
                        float w = clamp(confidence * dist_atten, 0.0, 1.0);
                        hit_radiance = hit_color;
                        hit_weight = w;
                        total_hit_weight += w;
                    }
                }
            }
        }

        // Environment radiance along this ray (world space) is needed only for misses or
        // partial-confidence hits. Fully accepted screen hits replace it completely.
        vec3 env_radiance = vec3_splat(0.0);
        BRANCH
        if(u_env_intensity > 0.0 && hit_weight < 1.0)
        {
            vec3 ws_sample_dir = normalize(mul(u_invView, vec4(vs_sample_dir, 0.0)).xyz);
            env_radiance = eval_radiance_sh(s_irradiance, ws_sample_dir) * u_env_intensity;
        }

        // A confident near hit occludes the environment and replaces it with the bounce; a
        // weak/distant hit only partially occludes it.
        vec3 ray_radiance = mix(env_radiance, hit_radiance, hit_weight);
        accumulated += ray_radiance;
    }

    vec3 result = accumulated / float(ray_count);
    // result stays in radiance-mean units (NOT * PI). The PI factor that puts it in
    // irradiance units (matching eval_irradiance_sh) is applied by the consumer at blend time.
    //
    // Alpha = signal validity, not screen-hit coverage. Each ray's contribution to RGB is
    // already valid -- a hit produces an on-screen bounce, a miss produces SH radiance --
    // so the cosine-importance average IS the full hemispherical irradiance estimate.
    // Returning anything < 1 would make the consumer's mix(SH, SSIL*PI, alpha) re-add SH
    // on top of an SSIL.rgb that already contains an equivalent env contribution from the
    // missed rays, biasing indirect bright. Sky pixels return 0 in the early-out above.
    gl_FragColor = vec4(result, 1.0);
}
