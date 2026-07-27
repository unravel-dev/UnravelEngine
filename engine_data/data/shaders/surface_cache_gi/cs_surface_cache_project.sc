/*
 * Project G-buffer into surface-cache atlases:
 * - material (albedo + coverage)
 * - emissive (sticky seed for look-away GI, like SSIL sees LBUFFER emissive)
 * - optional radiance (debug screen project)
 */

#include "../bgfx_compute.sh"
#include "../common.sh"
#include "../lighting.sh"

SAMPLER2D(s_gbuffer0, 0);
SAMPLER2D(s_gbuffer1, 1);
SAMPLER2D(s_gbuffer4, 2);
SAMPLER2D(s_direct, 3);
SAMPLER2D(s_cards, 4);
SAMPLER2D(s_gbuffer2, 5);
IMAGE2D_RW(i_atlas, rgba16f, 6);
IMAGE2D_RW(i_material, rgba16f, 7);
IMAGE2D_RW(i_emissive, rgba16f, 8);

uniform vec4 u_scache_params;
uniform vec4 u_scache_params2;
uniform vec4 u_camera_position;

#define u_card_count       u_scache_params.x
#define u_page_uv_size     u_scache_params.y
#define u_card_thickness   u_scache_params.z
#define u_project_history  u_scache_params.w
#define u_atlas_size       u_scache_params2.x
#define u_write_radiance   u_scache_params2.y

#define SCACHE_MAX_PROJECT_CARDS 512

bool project_to_card(vec3 world_pos, vec3 world_normal, int card_index, out vec2 atlas_uv, out float weight)
{
    vec4 t0 = texelFetch(s_cards, ivec2(0, card_index), 0);
    vec4 t1 = texelFetch(s_cards, ivec2(1, card_index), 0);
    vec4 t2 = texelFetch(s_cards, ivec2(2, card_index), 0);
    vec4 t3 = texelFetch(s_cards, ivec2(3, card_index), 0);
    vec3 origin = t0.xyz;
    float half_u = t0.w;
    vec3 normal = t1.xyz;
    float half_v = t1.w;
    vec3 tangent = t2.xyz;
    float page_u0 = t2.w;
    vec3 bitangent = t3.xyz;
    float page_v0 = t3.w;
    float ndot = dot(world_normal, normal);
    if(ndot < 0.35)
    {
        return false;
    }
    vec3 delta = world_pos - origin;
    float plane_dist = abs(dot(delta, normal));
    if(plane_dist > u_card_thickness)
    {
        return false;
    }
    float u = dot(delta, tangent);
    float v = dot(delta, bitangent);
    if(abs(u) > half_u || abs(v) > half_v || half_u < 1e-4 || half_v < 1e-4)
    {
        return false;
    }
    vec2 local_uv = vec2(u / (2.0 * half_u) + 0.5, v / (2.0 * half_v) + 0.5);
    local_uv = clamp(local_uv, vec2_splat(0.02), vec2_splat(0.98));
    atlas_uv = vec2(page_u0, page_v0) + local_uv * u_page_uv_size;
    weight = saturate(ndot) * saturate(1.0 - plane_dist / max(u_card_thickness, 1e-4));
    return true;
}

NUM_THREADS(8, 8, 1)
void main()
{
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 screen_size = textureSize(s_direct, 0);
    if(any(greaterThanEqual(coord, screen_size)))
    {
        return;
    }
    vec2 uv = (vec2(coord) + 0.5) / vec2(screen_size);
    float depth01 = DecodeGBufferDepthLod(uv, s_gbuffer4, 0.0).depth01;
    if(depth01 >= 0.9999)
    {
        return;
    }
    vec3 world_normal = DecodeGBufferNormalMetalRoughnessLod(uv, s_gbuffer1, 0.0).world_normal;
    vec3 clip = vec3(uv * 2.0 - 1.0, depth01);
    clip = clipTransform(clip);
    vec3 world_pos = clipToWorld(u_invViewProj, clip);
    vec3 albedo = DecodeGBufferColorAndAOLod(uv, s_gbuffer0, 0.0).base_color;
    vec3 emissive = DecodeGBufferEmissiveLod(uv, s_gbuffer2, 0.0).emissive_color;
    vec3 direct = texture2DLod(s_direct, uv, 0.0).rgb;
    vec3 radiance = max(direct, albedo * 0.01);
    int count = min(int(u_card_count), SCACHE_MAX_PROJECT_CARDS);
    LOOP for(int i = 0; i < SCACHE_MAX_PROJECT_CARDS; ++i)
    {
        if(i >= count)
        {
            break;
        }
        vec2 atlas_uv;
        float weight;
        if(!project_to_card(world_pos, world_normal, i, atlas_uv, weight))
        {
            continue;
        }
        ivec2 atlas_coord = ivec2(atlas_uv * u_atlas_size);
        atlas_coord = clamp(atlas_coord, ivec2(0, 0), ivec2(int(u_atlas_size) - 1, int(u_atlas_size) - 1));
        float hist = u_project_history;
        if(u_write_radiance > 0.5)
        {
            vec4 prev = imageLoad(i_atlas, atlas_coord);
            vec3 blended = mix(radiance, prev.rgb, hist * prev.a);
            float conf = max(prev.a * hist, weight);
            imageStore(i_atlas, atlas_coord, vec4(min(blended, vec3_splat(8.0)), saturate(conf)));
        }
        // Textured G-buffer albedo must win over mesh tint seed (white tint + red map).
        // Strong history on seed.a≈0.85 locked pages white and killed red bounce.
        vec4 prev_mat = imageLoad(i_material, atlas_coord);
        float gbuf_w = saturate(weight * 1.75);
        vec3 mat_rgb = mix(prev_mat.rgb, albedo, gbuf_w);
        float mat_a = max(prev_mat.a * hist * 0.35, weight);
        imageStore(i_material, atlas_coord, vec4(mat_rgb, saturate(mat_a)));
        // Only seed emissive when the G-buffer pixel is clearly emissive — weak
        // bleed from nearby neon painted whole wall pages cyan.
        float emis_w = saturate(dot(emissive, vec3(0.2126, 0.7152, 0.0722)) * 4.0);
        if(emis_w > 0.05)
        {
            vec4 prev_em = imageLoad(i_emissive, atlas_coord);
            vec3 em = mix(prev_em.rgb * hist, emissive, saturate(weight * emis_w));
            float em_a = max(prev_em.a * hist, weight * emis_w);
            imageStore(i_emissive, atlas_coord, vec4(em, saturate(em_a)));
        }
    }
}
