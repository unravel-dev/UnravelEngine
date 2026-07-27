/*
 * Card radiance = shadowed direct lights only (sun / points / emissive).
 * Outside CSM cascades: UNKNOWN — keep previous radiance (do not force black).
 * Soft PCF reduces cascade-edge binary flips when the sun rotates.
 */

#include "../bgfx_compute.sh"
#include "../common.sh"
#include "../lighting.sh"

SAMPLER2D(s_cards, 0);
SAMPLER2D(s_irradiance, 1);
SAMPLER2D(s_shadowMap0, 5);
SAMPLER2D(s_shadowMap1, 6);
SAMPLER2D(s_shadowMap2, 7);
SAMPLER2D(s_shadowMap3, 8);
IMAGE2D_RO(i_material, rgba16f, 2);
IMAGE2D_RO(i_emissive, rgba16f, 3);
IMAGE2D_RW(i_atlas, rgba16f, 4);

uniform vec4 u_card_lit_params0;
uniform vec4 u_card_lit_params1;
uniform vec4 u_card_lit_params2;
uniform vec4 u_sun_dir_intensity;
uniform vec4 u_sun_color;
uniform vec4 u_point_pos_range[8];
uniform vec4 u_point_color_intensity[8];
uniform vec4 u_card_batch[8];
uniform vec4 u_params0;
uniform vec4 u_params1;
uniform mat4 u_shadowMapMtx0;
uniform mat4 u_shadowMapMtx1;
uniform mat4 u_shadowMapMtx2;
uniform mat4 u_shadowMapMtx3;

#define u_page_uv_size   u_card_lit_params0.y
#define u_atlas_size     u_card_lit_params0.z
#define u_point_count    u_card_lit_params0.w
#define u_seed_sky       u_card_lit_params1.x
#define u_has_sun        u_card_lit_params1.y
#define u_albedo_boost   u_card_lit_params1.z
#define u_lit_history    u_card_lit_params1.w
#define u_direct_scale   u_card_lit_params2.x
#define u_batch_count    u_card_lit_params2.y
#define u_has_sun_shadow u_card_lit_params2.z
#define u_numSplits      u_params0.w
#define u_shadowMapBias  u_params1.x
#define u_shadowMapOffset u_params1.y

// Return codes for card_sun_visibility: >=0 visibility, <0 = unknown (keep prev).
#define SCACHE_VIS_UNKNOWN (-1.0)

float card_hard_shadow(sampler2D sm, vec4 shadow_coord, float bias)
{
    vec2 tex_coord = shadow_coord.xy / max(shadow_coord.w, 1e-6);
    if(any(greaterThan(tex_coord, vec2_splat(1.0))) || any(lessThan(tex_coord, vec2_splat(0.0))))
    {
        return 0.0;
    }
    float receiver = (shadow_coord.z - bias) / max(shadow_coord.w, 1e-6);
    float occluder = unpackRgbaToFloat(texture2DLod(sm, tex_coord, 0.0));
    return step(receiver, occluder);
}

float card_soft_shadow(sampler2D sm, vec4 shadow_coord, float bias)
{
    vec2 tex_coord = shadow_coord.xy / max(shadow_coord.w, 1e-6);
    if(any(greaterThan(tex_coord, vec2_splat(0.995))) || any(lessThan(tex_coord, vec2_splat(0.005))))
    {
        return 0.0;
    }
    vec2 texel = vec2_splat(1.0 / 1024.0);
    float sum = 0.0;
    sum += card_hard_shadow(sm, shadow_coord, bias);
    sum += card_hard_shadow(sm, shadow_coord + vec4(texel.x, 0.0, 0.0, 0.0) * shadow_coord.w, bias);
    sum += card_hard_shadow(sm, shadow_coord + vec4(-texel.x, 0.0, 0.0, 0.0) * shadow_coord.w, bias);
    sum += card_hard_shadow(sm, shadow_coord + vec4(0.0, texel.y, 0.0, 0.0) * shadow_coord.w, bias);
    sum += card_hard_shadow(sm, shadow_coord + vec4(0.0, -texel.y, 0.0, 0.0) * shadow_coord.w, bias);
    return sum * 0.2;
}

float card_sun_visibility(vec3 world_pos, vec3 normal)
{
    if(u_has_sun_shadow < 0.5)
    {
        return SCACHE_VIS_UNKNOWN;
    }
    float ndotl = saturate(dot(normal, normalize(-u_sun_dir_intensity.xyz)));
    float n_off = u_shadowMapOffset * (1.0 - ndotl);
    vec4 wpos = vec4(world_pos + normal * n_off, 1.0);
    vec4 c0 = mul(u_shadowMapMtx0, wpos);
    vec4 c1 = mul(u_shadowMapMtx1, wpos);
    vec4 c2 = mul(u_shadowMapMtx2, wpos);
    vec4 c3 = mul(u_shadowMapMtx3, wpos);
    vec2 uv0 = c0.xy / max(c0.w, 1e-6);
    vec2 uv1 = c1.xy / max(c1.w, 1e-6);
    vec2 uv2 = c2.xy / max(c2.w, 1e-6);
    vec2 uv3 = c3.xy / max(c3.w, 1e-6);
    bool s0 = all(lessThan(uv0, vec2_splat(0.99))) && all(greaterThan(uv0, vec2_splat(0.01)));
    bool s1 = all(lessThan(uv1, vec2_splat(0.99))) && all(greaterThan(uv1, vec2_splat(0.01)));
    bool s2 = all(lessThan(uv2, vec2_splat(0.99))) && all(greaterThan(uv2, vec2_splat(0.01)));
    bool s3 = all(lessThan(uv3, vec2_splat(0.99))) && all(greaterThan(uv3, vec2_splat(0.01)));
    float bias = u_shadowMapBias;
    if(s0)
    {
        return card_soft_shadow(s_shadowMap0, c0, bias);
    }
    if(s1 && u_numSplits > 1.5)
    {
        return card_soft_shadow(s_shadowMap1, c1, bias);
    }
    if(s2 && u_numSplits > 2.5)
    {
        return card_soft_shadow(s_shadowMap2, c2, bias);
    }
    if(s3 && u_numSplits > 3.5)
    {
        return card_soft_shadow(s_shadowMap3, c3, bias);
    }
    // Outside all cascades → unknown (keep previous), not hard black.
    return SCACHE_VIS_UNKNOWN;
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
    int card_index = int(u_card_batch[zi >> 2][zi & 3]);
    vec4 t0 = texelFetch(s_cards, ivec2(0, card_index), 0);
    vec4 t1 = texelFetch(s_cards, ivec2(1, card_index), 0);
    vec4 t2 = texelFetch(s_cards, ivec2(2, card_index), 0);
    vec4 t3 = texelFetch(s_cards, ivec2(3, card_index), 0);
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
    vec4 mat = imageLoad(i_material, mat_coord);
    vec3 albedo = mat.rgb;
    // Unseeded pages stay black — gray 0.35 invented fake bounce and washed chroma.
    bool has_albedo = mat.a > 1e-3 && dot(albedo, albedo) > 1e-6;
    if(!has_albedo)
    {
        albedo = vec3_splat(0.0);
    }
    albedo = saturate(albedo * max(u_albedo_boost, 0.1));
    vec4 prev = imageLoad(i_atlas, mat_coord);
    vec3 radiance = vec3_splat(0.0);
    bool sun_unknown = false;
    if(has_albedo && u_has_sun > 0.5 && u_has_sun_shadow > 0.5)
    {
        vec3 L = normalize(-u_sun_dir_intensity.xyz);
        float ndotl = saturate(dot(normal, L));
        float vis = card_sun_visibility(world_pos, normal);
        if(vis < 0.0)
        {
            sun_unknown = true;
        }
        else
        {
            float sun_i = max(u_sun_dir_intensity.w, 0.0);
            radiance += albedo * u_sun_color.rgb * sun_i * ndotl * vis * RECIP_PI * max(u_direct_scale, 0.0);
        }
    }
    int npoints = min(int(u_point_count), 8);
    LOOP for(int i = 0; i < 8; ++i)
    {
        if(i >= npoints || !has_albedo)
        {
            break;
        }
        vec3 lpos = u_point_pos_range[i].xyz;
        float range = max(u_point_pos_range[i].w, 1e-3);
        vec3 to_light = lpos - world_pos;
        float dist = length(to_light);
        if(dist >= range || dist < 1e-4)
        {
            continue;
        }
        float side = dot(to_light, normal);
        if(side < 0.02)
        {
            continue;
        }
        vec3 L = to_light / dist;
        float ndotl = saturate(dot(normal, L));
        float atten = saturate(1.0 - dist / range);
        atten *= atten;
        radiance += albedo * u_point_color_intensity[i].rgb * u_point_color_intensity[i].a * ndotl *
                    atten * RECIP_PI * max(u_direct_scale, 0.0);
    }
    // Neon emissives must not outshine sunlit floor bounce (cyan sphere vs red ground).
    vec3 emis = imageLoad(i_emissive, mat_coord).rgb;
    float emis_lum = dot(emis, vec3(0.2126, 0.7152, 0.0722));
    float emis_cap = 0.45;
    if(emis_lum > emis_cap)
    {
        emis *= emis_cap / max(emis_lum, 1e-4);
    }
    radiance += emis;
    radiance = min(radiance, vec3_splat(6.0));
    vec3 Lsun = normalize(-u_sun_dir_intensity.xyz);
    float ndotl_sun = saturate(dot(normal, Lsun));
    // Unknown CSM: keep prev only if still sun-facing. Facing-away / overhang
    // pages must decay or roof undersides leak forever.
    if(sun_unknown && prev.a > 1e-3 && ndotl_sun > 0.12)
    {
        imageStore(i_atlas, mat_coord, prev);
        return;
    }
    float lum = dot(radiance, vec3(0.2126, 0.7152, 0.0722));
    float conf = (lum > 1e-4) ? saturate(lum * 1.5) : 0.0;
    float hist = saturate(u_lit_history);
    if(prev.a < 1e-3)
    {
        hist = 0.0;
    }
    else if(conf < 1e-4)
    {
        // Keep bounce on shadowed walls; only ceilings hard-kill leak history.
        hist = (normal.y < -0.55) ? min(hist, 0.06) : max(hist, 0.72);
    }
    vec3 out_rgb = mix(radiance, prev.rgb, hist);
    float out_a = mix(conf, prev.a, hist);
    if(conf < 1e-4 && normal.y < -0.55)
    {
        out_rgb = mix(prev.rgb, vec3_splat(0.0), 0.90);
        out_a *= 0.10;
    }
    else if(conf < 1e-4)
    {
        // Preserve bounced irradiance on receivers between amortised bounce passes.
        out_a = max(out_a, prev.a * 0.85);
    }
    imageStore(i_atlas, mat_coord, vec4(out_rgb, saturate(out_a)));
}
