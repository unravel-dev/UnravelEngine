/*
 * Card-space bounce: one-sided emitters, opacity-traced visibility, average-only.
 */

#include "../bgfx_compute.sh"
#include "../common.sh"
#include "../lighting.sh"

SAMPLER2D(s_cards, 0);
IMAGE2D_RO(i_material, rgba16f, 1);
IMAGE2D_RW(i_atlas, rgba16f, 2);
IMAGE3D_RO(i_opacity, rgba16f, 3);
#include "scache_opacity_trace.sh"
#include "scache_gather_weight.sh"

uniform vec4 u_bounce_params0;
uniform vec4 u_bounce_params1;
uniform vec4 u_opacity_params0;
uniform vec4 u_opacity_params1;
uniform vec4 u_card_batch[8];

#define u_card_count       u_bounce_params0.y
#define u_page_uv_size     u_bounce_params0.z
#define u_atlas_size       u_bounce_params0.w
#define u_max_gather_dist  u_bounce_params1.x
#define u_gather_intensity u_bounce_params1.y
#define u_bounce_strength  u_bounce_params1.z
#define u_card_thickness   u_bounce_params1.w
#define u_batch_count      u_bounce_params0.x
#define u_op_origin        u_opacity_params0.xyz
#define u_op_voxel         u_opacity_params0.w
#define u_op_dims          u_opacity_params1.xyz
#define u_op_enabled       u_opacity_params1.w

#define SCACHE_MAX_BOUNCE_CARDS 96

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
    float max_dist = min(max(u_max_gather_dist, 0.5), 32.0);
    if(dist_sq < 1e-4 || dist_sq > max_dist * max_dist)
    {
        return;
    }
    float dist = sqrt(dist_sq);
    vec3 dir = to_card / dist;
    float n_recv = saturate(dot(world_normal, dir));
    // One-sided emit — backfaces must not light through walls/curtains.
    float n_emit = saturate(dot(card_normal, -dir));
    if(n_recv < 0.02 || n_emit < 0.02)
    {
        return;
    }
    // Midpoint must lie in front of BOTH hemispheres. Emitter term uses
    // (mid - sample); the opposite sign rejects all +Y floors (no red bounce).
    vec3 mid = 0.5 * (world_pos + sample_pos);
    float vis_geom = saturate(dot(mid - world_pos, world_normal) * 4.0) *
                     saturate(dot(mid - sample_pos, card_normal) * 4.0);
    if(vis_geom < 0.05)
    {
        return;
    }
    float occl = scache_visibility(world_pos + world_normal * 0.05,
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
    vec4 rad = imageLoad(i_atlas, ac);
    float rad_lum = dot(rad.rgb, vec3(0.2126, 0.7152, 0.0722));
    if(rad.a < 1e-3 || rad_lum < 1e-4)
    {
        return;
    }
    float form = n_recv * n_emit * tap_area / (dist_sq + tap_area * 0.55 + 0.65);
    form *= vis_geom * occl;
    form *= scache_emitter_form_boost(rad.rgb, rad_lum, card_normal);
    vec3 tap = min(rad.rgb * form * rad.a, vec3_splat(1.0));
    irradiance += tap;
    weight += form * rad.a;
}

bool gather_from_card(vec3 world_pos, vec3 world_normal, int card_index, int self_index, out vec3 irradiance, out float weight)
{
    irradiance = vec3_splat(0.0);
    weight = 0.0;
    if(card_index == self_index)
    {
        return false;
    }
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
    if(plane_dist < max(u_card_thickness * 0.25, 0.03) && same_facing > 0.9)
    {
        return false;
    }
    float area = 4.0 * half_u * half_v;
    float tap_area = area * 0.2;
    float u0 = clamp(dot(delta, tangent), -half_u, half_u);
    float v0 = clamp(dot(delta, bitangent), -half_v, half_v);
    accumulate_tap(world_pos, world_normal, origin, card_normal, tangent, bitangent, half_u, half_v,
                   page_u0, page_v0, u0, v0, tap_area, irradiance, weight);
    return weight > 1e-6;
}

NUM_THREADS(8, 8, 1)
void main()
{
    int zi = int(gl_WorkGroupID.z);
    if(zi >= int(u_batch_count))
    {
        return;
    }
    ivec2 local = ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(local, ivec2(64, 64))))
    {
        return;
    }
    int self_index = int(u_card_batch[zi >> 2][zi & 3]);
    vec4 t0 = texelFetch(s_cards, ivec2(0, self_index), 0);
    vec4 t1 = texelFetch(s_cards, ivec2(1, self_index), 0);
    vec4 t2 = texelFetch(s_cards, ivec2(2, self_index), 0);
    vec4 t3 = texelFetch(s_cards, ivec2(3, self_index), 0);
    vec3 origin = t0.xyz;
    float half_u = max(t0.w, 1e-3);
    vec3 normal = normalize(t1.xyz);
    float half_v = max(t1.w, 1e-3);
    vec3 tangent = t2.xyz;
    float page_u0 = t2.w;
    vec3 bitangent = t3.xyz;
    float page_v0 = t3.w;
    float fu = (float(local.x) + 0.5) / 64.0;
    float fv = (float(local.y) + 0.5) / 64.0;
    float u = (fu - 0.5) * 2.0 * half_u;
    float v = (fv - 0.5) * 2.0 * half_v;
    vec3 world_pos = origin + tangent * u + bitangent * v;
    ivec2 page_origin = ivec2(int(page_u0 * u_atlas_size + 1e-3), int(page_v0 * u_atlas_size + 1e-3));
    ivec2 mat_coord = page_origin + local;
    vec3 albedo = imageLoad(i_material, mat_coord).rgb;
    if(dot(albedo, albedo) < 1e-6)
    {
        return;
    }
    vec3 accum = vec3_splat(0.0);
    float weight_sum = 0.0;
    int count = min(int(u_card_count), SCACHE_MAX_BOUNCE_CARDS);
    LOOP for(int i = 0; i < SCACHE_MAX_BOUNCE_CARDS; ++i)
    {
        if(i >= count)
        {
            break;
        }
        vec3 irr;
        float w;
        if(gather_from_card(world_pos, normal, i, self_index, irr, w))
        {
            accum += irr;
            weight_sum += w;
        }
    }
    float intensity = max(u_bounce_strength, 0.0);
    vec3 bounce_irr = vec3_splat(0.0);
    if(weight_sum > 1e-5)
    {
        // Normalized average only — energy mix kept cyan fireflies alive.
        bounce_irr = accum / weight_sum;
    }
    vec3 bounce = bounce_irr * albedo * intensity * RECIP_PI;
    bounce = min(bounce, vec3_splat(1.5));
    vec4 prev = imageLoad(i_atlas, mat_coord);
    vec3 out_rgb = mix(prev.rgb, min(prev.rgb + bounce, vec3_splat(4.0)), 0.65);
    float out_lum = dot(out_rgb, vec3(0.2126, 0.7152, 0.0722));
    float out_a = max(prev.a, saturate(out_lum * 1.25));
    imageStore(i_atlas, mat_coord, vec4(out_rgb, out_a));
}
