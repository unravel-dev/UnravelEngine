/*
 * Amortized world probe update: gather surface-cache cards into cascade probes.
 * Probe irradiance is persistent — camera rotate does not thrash membership.
 */

#include "../bgfx_compute.sh"
#include "../common.sh"
#include "../lighting.sh"

// Atlas must be imageLoad — texture2DLod of a UAV-written page reads black on D3D.
IMAGE2D_RO(i_atlas, rgba16f, 0);
SAMPLER2D(s_cards, 1);
SAMPLER2D(s_irradiance, 2);
IMAGE2D_RW(i_probes, rgba16f, 3);
IMAGE3D_RO(i_opacity, rgba16f, 4);
#include "scache_opacity_trace.sh"
#include "scache_gather_weight.sh"

uniform vec4 u_probe_params0;
uniform vec4 u_probe_params1;
uniform vec4 u_probe_params2;
uniform vec4 u_probe_origin_near;
uniform vec4 u_probe_spacing_near;
uniform vec4 u_probe_origin_far;
uniform vec4 u_probe_spacing_far;
uniform vec4 u_opacity_params0;
uniform vec4 u_opacity_params1;

#define u_op_origin  u_opacity_params0.xyz
#define u_op_voxel   u_opacity_params0.w
#define u_op_dims    u_opacity_params1.xyz
#define u_op_enabled u_opacity_params1.w

#define u_card_count        u_probe_params0.x
#define u_page_uv_size      u_probe_params0.y
#define u_card_thickness    u_probe_params0.z
#define u_gather_intensity  u_probe_params0.w
#define u_max_gather_dist   u_probe_params1.x
#define u_probe_history     u_probe_params1.y
#define u_seed_sky          u_probe_params1.z
#define u_update_start      u_probe_params1.w
#define u_update_count      u_probe_params2.x
#define u_near_count        u_probe_params2.y
#define u_total_probes      u_probe_params2.z
#define u_cache_blend       u_probe_params2.w
// page_uv_size = PAGE_SIZE / ATLAS_SIZE → recover atlas texel size.
#define u_atlas_size        (64.0 / max(u_page_uv_size, 1e-6))

#define SCACHE_MAX_PROBE_CARDS 512
#define NEAR_SX 10
#define NEAR_SY 6
#define NEAR_SZ 10
#define FAR_SX 8
#define FAR_SY 4
#define FAR_SZ 8

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
    // Cap range — long gathers punch through walls and read as ambient fill.
    float max_dist = min(max(u_max_gather_dist, 0.5), 48.0);
    float max_dist_sq = max_dist * max_dist;
    if(dist_sq < 1e-4 || dist_sq > max_dist_sq)
    {
        return;
    }
    float dist = sqrt(dist_sq);
    float fade_start = max_dist * 0.35;
    float dist_fade = 1.0 - saturate((dist - fade_start) / max(max_dist - fade_start, 1e-3));
    dist_fade *= dist_fade;
    vec3 dir = to_card / dist;
    // Omnidirectional receive; one-sided emit (no backface through-wall light).
    float n_recv = abs(dot(world_normal, dir));
    float n_emit = saturate(dot(card_normal, -dir));
    if(n_recv < 0.02 || n_emit < 0.02)
    {
        return;
    }
    float occl = scache_visibility(world_pos,
                                   sample_pos + card_normal * 0.05,
                                   u_op_origin,
                                   u_op_voxel,
                                   u_op_dims,
                                   u_op_enabled);
    if(occl < 0.05)
    {
        return;
    }
    vec2 local_uv = vec2(u / (2.0 * half_u) + 0.5, v / (2.0 * half_v) + 0.5);
    local_uv = clamp(local_uv, vec2_splat(0.5 / 64.0), vec2_splat(1.0 - 0.5 / 64.0));
    ivec2 page_origin = ivec2(int(page_u0 * u_atlas_size + 1e-3), int(page_v0 * u_atlas_size + 1e-3));
    ivec2 ac = page_origin + ivec2(int(local_uv.x * 64.0), int(local_uv.y * 64.0));
    vec4 cached = imageLoad(i_atlas, ac);
    float cached_lum = dot(cached.rgb, vec3(0.2126, 0.7152, 0.0722));
    if(cached.a < 1e-3 || cached_lum < 1e-4)
    {
        return;
    }
    float form = n_recv * n_emit * tap_area / (dist_sq + tap_area * 0.55 + 0.65);
    form *= dist_fade * occl;
    form *= scache_emitter_form_boost(cached.rgb, cached_lum, card_normal);
    irradiance += cached.rgb * form * cached.a;
    weight += form * cached.a;
}

bool gather_from_card(vec3 world_pos, vec3 world_normal, int card_index, out vec3 irradiance, out float weight)
{
    irradiance = vec3_splat(0.0);
    weight = 0.0;
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
    // Only reject true self-plane hits — thickness*2 (~0.7m) skipped nearby floors.
    if(plane_dist < max(u_card_thickness * 0.15, 0.02))
    {
        return false;
    }
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
    return weight > 1e-6;
}

ivec2 probe_texel(uint probe_index)
{
    if(probe_index < uint(NEAR_SX * NEAR_SY * NEAR_SZ))
    {
        uint i = probe_index % uint(NEAR_SX);
        uint j = (probe_index / uint(NEAR_SX)) % uint(NEAR_SY);
        uint k = probe_index / uint(NEAR_SX * NEAR_SY);
        return ivec2(int(i + k * uint(NEAR_SX)), int(j));
    }
    uint far_index = probe_index - uint(NEAR_SX * NEAR_SY * NEAR_SZ);
    uint i = far_index % uint(FAR_SX);
    uint j = (far_index / uint(FAR_SX)) % uint(FAR_SY);
    uint k = far_index / uint(FAR_SX * FAR_SY);
    return ivec2(int(i + k * uint(FAR_SX)), int(j + uint(NEAR_SY)));
}

vec3 probe_world_pos(uint probe_index)
{
    if(probe_index < uint(NEAR_SX * NEAR_SY * NEAR_SZ))
    {
        uint i = probe_index % uint(NEAR_SX);
        uint j = (probe_index / uint(NEAR_SX)) % uint(NEAR_SY);
        uint k = probe_index / uint(NEAR_SX * NEAR_SY);
        return u_probe_origin_near.xyz + vec3(float(i), float(j), float(k)) * u_probe_spacing_near.xyz;
    }
    uint far_index = probe_index - uint(NEAR_SX * NEAR_SY * NEAR_SZ);
    uint i = far_index % uint(FAR_SX);
    uint j = (far_index / uint(FAR_SX)) % uint(FAR_SY);
    uint k = far_index / uint(FAR_SX * FAR_SY);
    return u_probe_origin_far.xyz + vec3(float(i), float(j), float(k)) * u_probe_spacing_far.xyz;
}

NUM_THREADS(64, 1, 1)
void main()
{
    uint local = gl_GlobalInvocationID.x;
    if(local >= uint(u_update_count))
    {
        return;
    }
    uint probe_index = (uint(u_update_start) + local) % max(uint(u_total_probes), 1u);
    vec3 world_pos = probe_world_pos(probe_index);
    vec3 world_normal = vec3(0.0, 1.0, 0.0);
    vec3 accum = vec3_splat(0.0);
    float weight_sum = 0.0;
    int count = min(int(u_card_count), SCACHE_MAX_PROBE_CARDS);
    // Three axes + abs cosine: floors below (+Y vs dir=-Y) contribute without a -Y pass.
    LOOP for(int axis = 0; axis < 3; ++axis)
    {
        if(axis == 0)
        {
            world_normal = vec3(0.0, 1.0, 0.0);
        }
        else if(axis == 1)
        {
            world_normal = vec3(1.0, 0.0, 0.0);
        }
        else
        {
            world_normal = vec3(0.0, 0.0, 1.0);
        }
        LOOP for(int card_i = 0; card_i < SCACHE_MAX_PROBE_CARDS; ++card_i)
        {
            if(card_i >= count)
            {
                break;
            }
            vec3 irr = vec3_splat(0.0);
            float w = 0.0;
            if(gather_from_card(world_pos, world_normal, card_i, irr, w))
            {
                accum += irr;
                weight_sum += w;
            }
        }
    }
    const float SCACHE_PROBE_EXPOSURE = 0.10;
    vec3 gathered = vec3_splat(0.0);
    if(weight_sum > 1e-5)
    {
        vec3 average = (accum / weight_sum) * SCACHE_PROBE_EXPOSURE;
        vec3 energy = accum * (SCACHE_PROBE_EXPOSURE * 1.2);
        gathered = min(mix(average, energy, 0.35), vec3_splat(1.75));
    }
    float gathered_lum = dot(gathered, vec3(0.2126, 0.7152, 0.0722));
    float lit_strength = saturate(weight_sum * 0.35) * saturate(gathered_lum * 8.0);
    // Updated this frame ⇒ known sample. Dark must KEEP confidence so compose
    // replaces skylight SH with black (not outdoor ambient in enclosed halls).
    float occupancy = saturate(u_cache_blend) * 0.92;
    float confidence = (lit_strength > 1e-4) ? saturate(lit_strength) * saturate(u_cache_blend)
                                             : occupancy;
    if(lit_strength < 1e-4)
    {
        gathered = vec3_splat(0.0);
    }
    ivec2 texel = probe_texel(probe_index);
    vec4 prev = imageLoad(i_probes, texel);
    float hist = saturate(u_probe_history);
    if(prev.a > 0.05 && lit_strength > 1e-4 && lit_strength < prev.a * 0.45)
    {
        hist = max(hist, 0.90);
    }
    if(prev.a < 1e-3)
    {
        hist = 0.0;
    }
    else if(lit_strength < 1e-4)
    {
        // Clear stale bright / sky-leaked probes toward known-dark.
        hist = min(hist, 0.35);
    }
    vec3 blended = mix(gathered, min(prev.rgb, vec3_splat(1.5)), hist);
    float conf = mix(confidence, prev.a, hist);
    if(lit_strength < 1e-4)
    {
        blended = mix(prev.rgb, vec3_splat(0.0), 0.65);
        conf = max(conf, occupancy * 0.85);
    }
    imageStore(i_probes, texel, vec4(min(blended, vec3_splat(1.5)), saturate(conf)));
}
