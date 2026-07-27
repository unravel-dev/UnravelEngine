$input v_texcoord0

/*
 * Final gather from the surface-cache atlas.
 *
 * Atlas pages store OUTGOING radiance. This pass estimates INDIRECT irradiance
 * via multi-tap card gather (area-light form factors). Membership of the GPU
 * card set is stabilized on the CPU with distance hysteresis — no screen
 * temporal (that only ghosted without fixing hard set pops).
 */

#include "../common.sh"
#include "../lighting.sh"

SAMPLER2D(s_gbuffer0, 0);
SAMPLER2D(s_gbuffer1, 1);
SAMPLER2D(s_gbuffer4, 2);
SAMPLER2D(s_atlas, 3);
SAMPLER2D(s_cards, 4);
SAMPLER2D(s_irradiance, 5);

uniform vec4 u_scache_params;
uniform vec4 u_scache_params2;
uniform vec4 u_camera_position;

#define u_card_count         u_scache_params.x
#define u_page_uv_size       u_scache_params.y
#define u_card_thickness     u_scache_params.z
#define u_upload_outer       u_scache_params.w
#define u_cache_blend        u_scache_params2.y
#define u_seed_sky           u_scache_params2.z
#define u_max_gather_dist    u_scache_params2.w
#define u_gather_intensity   u_camera_position.w

#define SCACHE_MAX_SAMPLE_CARDS 512

void accumulate_tap(vec3 world_pos,
                    vec3 world_normal,
                    vec3 origin,
                    vec3 card_normal,
                    vec3 tangent,
                    vec3 bitangent,
                    float half_u,
                    float half_v,
                    float page_u0,
                    float page_v0,
                    float u,
                    float v,
                    float tap_area,
                    inout vec3 irradiance,
                    inout float weight)
{
    u = clamp(u, -half_u, half_u);
    v = clamp(v, -half_v, half_v);
    vec3 sample_pos = origin + tangent * u + bitangent * v;
    vec3 to_card = sample_pos - world_pos;
    float dist_sq = dot(to_card, to_card);
    float max_dist = max(u_max_gather_dist, 0.5);
    float max_dist_sq = max_dist * max_dist;
    if(dist_sq < 1e-4 || dist_sq > max_dist_sq)
    {
        return;
    }
    float dist = sqrt(dist_sq);
    // Soft falloff near gather radius (keep mid-range bounce visible).
    float fade_start = max_dist * 0.7;
    float dist_fade = 1.0 - saturate((dist - fade_start) / max(max_dist - fade_start, 1e-3));
    vec3 dir = to_card / dist;
    float n_recv = saturate(dot(world_normal, dir));
    float n_emit = abs(dot(card_normal, -dir));
    if(n_recv < 0.02 || n_emit < 0.02)
    {
        return;
    }
    vec2 local_uv = vec2(u / (2.0 * half_u) + 0.5, v / (2.0 * half_v) + 0.5);
    local_uv = clamp(local_uv, vec2_splat(0.02), vec2_splat(0.98));
    vec2 atlas_uv = vec2(page_u0, page_v0) + local_uv * u_page_uv_size;
    vec4 cached = texture2D(s_atlas, atlas_uv);
    if(cached.a < 1e-3)
    {
        return;
    }
    float form = n_recv * n_emit * tap_area / (dist_sq + tap_area + 0.35);
    form *= dist_fade;
    irradiance += cached.rgb * form * cached.a;
    weight += form * cached.a;
}

bool gather_from_card(vec3 world_pos, vec3 world_normal, int card_index, out vec3 irradiance, out float weight)
{
    vec4 t0 = texelFetch(s_cards, ivec2(0, card_index), 0);
    vec4 t1 = texelFetch(s_cards, ivec2(1, card_index), 0);
    vec4 t2 = texelFetch(s_cards, ivec2(2, card_index), 0);
    vec4 t3 = texelFetch(s_cards, ivec2(3, card_index), 0);
    vec3 origin = t0.xyz;
    float half_u = max(t0.w, 1e-3);
    vec3 card_normal = t1.xyz;
    float half_v = max(t1.w, 1e-3);
    vec3 tangent = t2.xyz;
    float page_u0 = t2.w;
    vec3 bitangent = t3.xyz;
    float page_v0 = t3.w;
    vec3 delta = world_pos - origin;
    float plane_dist = abs(dot(delta, card_normal));
    float same_facing = saturate(dot(world_normal, card_normal));
    if(plane_dist < u_card_thickness * 2.0 && same_facing > 0.85)
    {
        return false;
    }
    // Very soft camera-range fade only at the far upload edge. Mid-street shade points
    // must still gather from cards tens of meters away from the camera.
    float cam_dist = length(origin - u_camera_position.xyz);
    float fade_end = max(u_upload_outer, 1.0);
    float fade_start = max(fade_end * 0.85, 1.0);
    float membership = 1.0 - saturate((cam_dist - fade_start) / max(fade_end - fade_start, 1e-3));
    if(membership < 1e-3)
    {
        return false;
    }
    irradiance = vec3_splat(0.0);
    weight = 0.0;
    float area = 4.0 * half_u * half_v;
    float tap_area = area * 0.25;
    float u0 = clamp(dot(delta, tangent), -half_u, half_u);
    float v0 = clamp(dot(delta, bitangent), -half_v, half_v);
    accumulate_tap(world_pos, world_normal, origin, card_normal, tangent, bitangent, half_u, half_v,
                   page_u0, page_v0, u0, v0, tap_area, irradiance, weight);
    float ou = half_u * 0.55;
    float ov = half_v * 0.55;
    accumulate_tap(world_pos, world_normal, origin, card_normal, tangent, bitangent, half_u, half_v,
                   page_u0, page_v0, -ou, -ov, tap_area, irradiance, weight);
    accumulate_tap(world_pos, world_normal, origin, card_normal, tangent, bitangent, half_u, half_v,
                   page_u0, page_v0, ou, -ov, tap_area, irradiance, weight);
    accumulate_tap(world_pos, world_normal, origin, card_normal, tangent, bitangent, half_u, half_v,
                   page_u0, page_v0, -ou, ov, tap_area, irradiance, weight);
    accumulate_tap(world_pos, world_normal, origin, card_normal, tangent, bitangent, half_u, half_v,
                   page_u0, page_v0, ou, ov, tap_area, irradiance, weight);
    irradiance *= membership;
    weight *= membership;
    return weight > 1e-6;
}

void main()
{
    vec2 uv = v_texcoord0;
    float depth01 = DecodeGBufferDepthLod(uv, s_gbuffer4, 0.0).depth01;
    if(depth01 >= 0.9999)
    {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    vec3 world_normal = DecodeGBufferNormalMetalRoughness(uv, s_gbuffer1).world_normal;
    vec3 clip = vec3(uv * 2.0 - 1.0, depth01);
    clip = clipTransform(clip);
    vec3 world_pos = clipToWorld(u_invViewProj, clip);
    vec3 accum = vec3_splat(0.0);
    float weight_sum = 0.0;
    int count = min(int(u_card_count), SCACHE_MAX_SAMPLE_CARDS);
    LOOP for(int i = 0; i < SCACHE_MAX_SAMPLE_CARDS; ++i)
    {
        if(i >= count)
        {
            break;
        }
        vec3 irr;
        float w;
        if(gather_from_card(world_pos, world_normal, i, irr, w))
        {
            accum += irr;
            weight_sum += w;
        }
    }
    float intensity = max(u_gather_intensity, 0.0);
    vec3 gathered = accum * intensity;
    float confidence = saturate(weight_sum * intensity * 1.25) * u_cache_blend;
    if(confidence < 1e-3 && u_seed_sky > 0.5)
    {
        gathered = eval_irradiance_sh(s_irradiance, world_normal);
        confidence = 0.15 * u_cache_blend;
    }
    gl_FragColor = vec4(gathered, saturate(confidence));
}
