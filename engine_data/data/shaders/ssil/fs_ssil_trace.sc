$input v_texcoord0

/*
 * Screen-Space Indirect Lighting (SSIL) trace pass.
 *
 * Traces cosine-weighted hemisphere rays per pixel using Hi-Z hierarchical
 * ray marching, then samples radiance at hit points. Supports multi-bounce
 * by feeding back the previous frame's denoised SSIL output, attenuated by
 * the hit surface's diffuse albedo for energy conservation.
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

uniform vec4 u_ssil_params;
#define u_max_steps       u_ssil_params.x
#define u_max_rays        u_ssil_params.y
#define u_depth_tolerance u_ssil_params.z
#define u_brightness      u_ssil_params.w

uniform vec4 u_ssil_params2;
#define u_max_distance    u_ssil_params2.x
#define u_frame_index     u_ssil_params2.y
#define u_multi_bounce    u_ssil_params2.z

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

/// Sample total outgoing radiance at a hit point, including multi-bounce
/// feedback from the previous frame attenuated by the hit surface's diffuse albedo.
vec3 SampleRadiance(vec2 hit_uv)
{
    vec3 direct = texture2DLod(s_color, hit_uv, 0.0).rgb;

    GBufferDataEmissive ed = DecodeGBufferEmissiveLod(hit_uv, s_emissive, 0.0);
    vec3 radiance = direct + ed.emissive_color;

    BRANCH
    if(u_multi_bounce > 0.0)
    {
        GBufferDataColorAndAO cd = DecodeGBufferColorAndAOLod(hit_uv, s_albedo, 0.0);
        GBufferDataNormalMetalRoughness nd = DecodeGBufferNormalMetalRoughnessLod(hit_uv, s_normal, 0.0);
        vec3 hit_diffuse = cd.base_color * (1.0 - nd.metalness);

        vec3 prev_indirect = texture2DLod(s_prev_ssil, hit_uv, 0.0).rgb;
        prev_indirect = min(prev_indirect, vec3_splat(10.0));

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

    vec3 accumulated = vec3_splat(0.0);
    float total_weight = 0.0;

    LOOP for(int i = 0; i < num_rays; ++i)
    {
        vec2 E = Hammersley16(uint(i), uint(num_rays), rnd);
        vec3 vs_sample_dir = ImportanceSampleCosine(E, vs_normal);

        vec3 ss_ray_dir = HizProjectVsDirToSsDir(vs_ray_origin, vs_sample_dir, ss_ray_origin);

        if(dot(ss_ray_dir.xy, ss_ray_dir.xy) < 1e-12)
            continue;

        vec3 ss_hit_pos;
        bool valid_hit = HizHierarchicalRaymarch(s_hiz, ss_ray_origin, ss_ray_dir,
                                                  screen_size, BASE_LOD, max_steps, ss_hit_pos);

        BRANCH
        if(valid_hit)
        {
            vec3 vs_hit = HizComputeViewspacePosition(ss_hit_pos.xy, ss_hit_pos.z);
            float hit_dist = length(vs_hit - vs_ray_origin);

            if(hit_dist >= u_max_distance)
                continue;

            float confidence = HizValidateHit(s_hiz, s_normal, ss_hit_pos, uv,
                                               vs_ray_origin, vs_hit, screen_size, u_depth_tolerance);

            BRANCH
            if(confidence > 0.0)
            {
                vec3 hit_color = SampleRadiance(ss_hit_pos.xy);

                hit_color = min(hit_color, vec3_splat(10.0));

                hit_color /= 1.0 + Luminance(hit_color);

                float dist_atten = 1.0 - smoothstep(0.0, u_max_distance, hit_dist);

                float w = confidence * dist_atten;
                accumulated += hit_color * w;
                total_weight += w;
            }
        }
    }

    vec3 result = vec3_splat(0.0);
    float result_confidence = 0.0;
    if(total_weight > 0.0)
    {
        result = accumulated / total_weight;
        // Inverse Reinhard to restore HDR range
        result /= max(1.0 - Luminance(result), 1e-4);
        result *= u_brightness;
        result_confidence = min(total_weight / float(num_rays), 1.0);
    }

    gl_FragColor = vec4(result, result_confidence);
}
