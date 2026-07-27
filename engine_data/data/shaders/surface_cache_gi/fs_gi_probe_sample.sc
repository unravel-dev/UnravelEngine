$input v_texcoord0

/*
 * Final gather: local card form-factor (color bleed + variation) + probe fill.
 * Fully normalizing the card gather erased Lumen-like near-field gradients.
 */

#include "../common.sh"
#include "../lighting.sh"

SAMPLER2D(s_gbuffer0, 0);
SAMPLER2D(s_gbuffer1, 1);
SAMPLER2D(s_gbuffer4, 2);
SAMPLER2D(s_probes, 3);
SAMPLER2D(s_irradiance, 4);
SAMPLER2D(s_atlas, 5);
SAMPLER2D(s_cards, 6);

#include "scache_gather_weight.sh"

uniform vec4 u_probe_params0;
uniform vec4 u_probe_params1;
uniform vec4 u_probe_params2;
uniform vec4 u_probe_origin_near;
uniform vec4 u_probe_spacing_near;
uniform vec4 u_probe_origin_far;
uniform vec4 u_probe_spacing_far;
uniform vec4 u_camera_position;

#define u_cache_blend       u_probe_params0.x
#define u_seed_sky          u_probe_params0.y
#define u_near_extent       u_probe_params0.z
#define u_far_extent        u_probe_params0.w
#define u_tex_w             u_probe_params1.x
#define u_tex_h             u_probe_params1.y
#define u_card_count        u_probe_params1.z
#define u_page_uv_size      u_probe_params1.w
#define u_card_thickness    u_probe_params2.x
#define u_max_gather_dist   u_probe_params2.y
#define u_gather_intensity  u_probe_params2.z

#define NEAR_SX 10.0
#define NEAR_SY 6.0
#define NEAR_SZ 10.0
#define FAR_SX 8.0
#define FAR_SY 4.0
#define FAR_SZ 8.0
#define SCACHE_MAX_SAMPLE_CARDS 64

vec4 sample_cascade(vec3 world_pos,
                    vec3 origin,
                    vec3 spacing,
                    float size_x,
                    float size_y,
                    float size_z,
                    float row_offset,
                    out float inside)
{
    vec3 local = (world_pos - origin) / max(spacing, vec3_splat(1e-4));
    vec3 max_i = vec3(size_x - 1.0, size_y - 1.0, size_z - 1.0);
    vec3 edge = min(local + vec3_splat(0.5), max_i + vec3_splat(0.5) - local);
    float edge_min = min(edge.x, min(edge.y, edge.z));
    // Wider cascade fade — *6 made vertical knife bands at volume edges.
    inside = saturate(edge_min * 2.0);
    if(inside < 1e-4)
    {
        return vec4_splat(0.0);
    }
    local = clamp(local, vec3_splat(0.0), max_i);
    vec3 base = floor(local);
    vec3 f = fract(local);
    vec4 accum = vec4_splat(0.0);
    float wsum = 0.0;
    for(int iz = 0; iz <= 1; ++iz)
    {
        for(int iy = 0; iy <= 1; ++iy)
        {
            for(int ix = 0; ix <= 1; ++ix)
            {
                vec3 idx = base + vec3(float(ix), float(iy), float(iz));
                idx = clamp(idx, vec3_splat(0.0), max_i);
                float wx = (ix == 0) ? (1.0 - f.x) : f.x;
                float wy = (iy == 0) ? (1.0 - f.y) : f.y;
                float wz = (iz == 0) ? (1.0 - f.z) : f.z;
                float w = wx * wy * wz;
                float px = idx.x + idx.z * size_x + 0.5;
                float py = idx.y + row_offset + 0.5;
                vec2 uv = vec2(px / max(u_tex_w, 1.0), py / max(u_tex_h, 1.0));
                vec4 s = texture2D(s_probes, uv);
                accum += s * w;
                wsum += w;
            }
        }
    }
    if(wsum < 1e-5)
    {
        return vec4_splat(0.0);
    }
    return accum / wsum;
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
    float same_facing = saturate(dot(world_normal, card_normal));
    if(plane_dist < max(u_card_thickness * 0.2, 0.02) && same_facing > 0.9)
    {
        return false;
    }
    float u = clamp(dot(delta, tangent), -half_u, half_u);
    float v = clamp(dot(delta, bitangent), -half_v, half_v);
    vec3 sample_pos = origin + tangent * u + bitangent * v;
    vec3 to_card = sample_pos - world_pos;
    float dist_sq = dot(to_card, to_card);
    float max_dist = min(max(u_max_gather_dist, 0.5), 40.0);
    if(dist_sq < 1e-4 || dist_sq > max_dist * max_dist)
    {
        return false;
    }
    float dist = sqrt(dist_sq);
    vec3 dir = to_card / dist;
    float n_recv = saturate(dot(world_normal, dir));
    float n_emit = saturate(dot(card_normal, -dir));
    if(n_recv < 0.02 || n_emit < 0.02)
    {
        return false;
    }
    vec3 mid = 0.5 * (world_pos + sample_pos);
    // Emitter hemisphere: mid in front of card (not sample in front of mid).
    float vis = saturate(dot(mid - world_pos, world_normal) * 4.0) *
                saturate(dot(mid - sample_pos, card_normal) * 4.0);
    if(vis < 0.05)
    {
        return false;
    }
    float area = 4.0 * half_u * half_v;
    float tap_area = area * 0.2;
    vec2 local_uv = vec2(u / (2.0 * half_u) + 0.5, v / (2.0 * half_v) + 0.5);
    local_uv = clamp(local_uv, vec2_splat(0.5 / 64.0), vec2_splat(1.0 - 0.5 / 64.0));
    vec2 atlas_uv = vec2(page_u0, page_v0) + local_uv * u_page_uv_size;
    vec4 cached = texture2D(s_atlas, atlas_uv);
    float cached_lum = dot(cached.rgb, vec3(0.2126, 0.7152, 0.0722));
    if(cached.a < 1e-3 || cached_lum < 1e-4)
    {
        return false;
    }
    // Strong distance floor — tiny dist_sq made 1/r^2 firefly "bright spots".
    float form = n_recv * n_emit * tap_area / (dist_sq + tap_area * 0.55 + 0.65);
    form *= vis;
    form *= scache_emitter_form_boost(cached.rgb, cached_lum, card_normal);
    // Soft distance falloff near gather radius (hard cut looked knife-edged).
    float fade = 1.0 - saturate((dist - max_dist * 0.55) / max(max_dist * 0.45, 1e-3));
    form *= fade * fade;
    irradiance = min(cached.rgb * form * cached.a, vec3_splat(1.0));
    weight = form * cached.a;
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

    vec3 card_accum = vec3_splat(0.0);
    float card_wsum = 0.0;
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
            card_accum += irr;
            card_wsum += w;
        }
    }
    // Intensity scales RGB only. Confidence must NOT depend on intensity —
    // otherwise low intensity falls back to skylight SH (ambient) and high
    // intensity washes the whole frame.
    // Exposure must leave saturated floor bounce visible on white walls.
    const float SCACHE_PROBE_EXPOSURE = 0.12;
    float intensity = max(u_gather_intensity, 0.0);
    vec3 card_gi = vec3_splat(0.0);
    if(card_wsum > 1e-5)
    {
        // Average only — energy mix amplified cyan neon over floor bounce.
        card_gi = min((card_accum / card_wsum) * SCACHE_PROBE_EXPOSURE * intensity, vec3_splat(1.5));
    }
    // Soft card confidence so card↔probe handoff is not a hard edge.
    float card_conf = saturate(card_wsum * 1.25) * u_cache_blend;

    float near_inside = 0.0;
    float far_inside = 0.0;
    vec4 near_s = sample_cascade(world_pos,
                                 u_probe_origin_near.xyz,
                                 u_probe_spacing_near.xyz,
                                 NEAR_SX,
                                 NEAR_SY,
                                 NEAR_SZ,
                                 0.0,
                                 near_inside);
    vec4 far_s = sample_cascade(world_pos,
                                u_probe_origin_far.xyz,
                                u_probe_spacing_far.xyz,
                                FAR_SX,
                                FAR_SY,
                                FAR_SZ,
                                NEAR_SY,
                                far_inside);
    // Probe.a is occupancy (including known-dark). Keep that for SH occlusion.
    float near_q = saturate(near_s.a * 2.0) * near_inside;
    float far_q = saturate(far_s.a * 2.0) * far_inside;
    float near_w = near_q;
    float far_w = far_q * (1.0 - near_w * 0.75);
    float probe_wsum = near_w + far_w;
    vec3 probe_gi = vec3_splat(0.0);
    float probe_conf = 0.0;
    if(probe_wsum > 1e-4)
    {
        probe_gi = min(((near_s.rgb * near_w + far_s.rgb * far_w) / probe_wsum) * intensity,
                       vec3_splat(1.5));
        float coverage = max(far_inside, near_inside * 0.85);
        probe_conf = coverage * saturate(max(near_q, far_q)) * u_cache_blend;
    }

    // Prefer probes for spatial smoothness; cards only tint locally (less knife-cut).
    float card_w = saturate(card_conf) * 0.55;
    float probe_w = saturate(probe_conf) * (1.0 - card_w * 0.35);
    float wsum = card_w + probe_w;
    vec3 gathered = vec3_splat(0.0);
    float confidence = 0.0;
    if(wsum > 1e-4)
    {
        gathered = (card_gi * card_w + probe_gi * probe_w) / wsum;
        confidence = saturate(card_w + probe_w * 0.95);
    }
    // Miss (outside probe volume / never updated) → alpha 0 → skylight SH.
    // Known-dark (alpha high, rgb ~0) → replaces SH with black in enclosed space.
    gathered = min(gathered, vec3_splat(1.25));
    gl_FragColor = vec4(gathered, saturate(confidence));
}
