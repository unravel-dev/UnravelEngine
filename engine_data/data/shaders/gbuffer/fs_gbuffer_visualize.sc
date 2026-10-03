$input v_texcoord0

#include "../common.sh"
#include "../lighting.sh"
#include "../pre_exposure.sh"
#include "../tonemapping/tonemapping.sh"

SAMPLER2D(s_tex0, 0);
SAMPLER2D(s_tex1, 1);
SAMPLER2D(s_tex2, 2);
SAMPLER2D(s_tex3, 3);
SAMPLER2D(s_tex4, 4);
SAMPLER2D(s_tex5, 5);
SAMPLER2D(s_tex6, 6);
SAMPLER2D(s_tex7, 7);
// Screen-space AO (GTAO, or ASSAO when GTAO is off): a = visibility; rgb = GTAO bent
// normal * 0.5 + 0.5.
SAMPLER2D(s_tex8, 8);
// PBUFFER, the untraced reflection layer; s_tex5 is RBUFFER, the traced layers.
SAMPLER2D(s_tex9, 9);
// The GTSO specular occlusion table (specular_occlusion_lut).
SAMPLER3D(s_tex10, 10);

uniform vec4 u_params;
/// x = screen-space AO intensity, z = multi-bounce of the screen term (0/1), w = 1 when the
/// texture is GTAO's (bent normal); y unused here.
uniform vec4 u_screen_ao;
/// The indirect diffuse view: x = the lit image's tone mapping operator (tonemapping.sh), y = the multi-bounce
/// albedo cap (0 = none), z = 1 when s_tex7 is SSIL (it resolved its own screen-space visibility).
uniform vec4 u_visualize_indirect;

#define u_mode int(u_params.x)
/// The roughness below which Lumen traces reflection rays (UE r.Lumen.Reflections.MaxRoughnessToTrace).
#define u_max_roughness_to_trace u_params.z

#define BASE_COLOR 0
#define DIFFUSE_COLOR 1
#define SPECULAR_COLOR 2
#define RADIANCE 3
#define IRRADIANCE 4
#define AMBIENT_OCCLUSION 5
#define WORLD_NORMAL 6
#define ROUGHNESS 7
#define METALNESS 8
#define EMISSIVE_COLOR 9
#define SUBSURFACE_COLOR 10
#define DEPTH 11
#define SSIL 12
#define RADIANCE_ALPHA 13
#define SPECULAR_OCCLUSION 14
#define AO_BENT_NORMALS 15
#define LUMEN_REFLECTION_RAYS 16

/// The grey albedo the indirect diffuse view lights (UE DiffuseIndirectComposite.usf:565).
#define VISUALIZE_DIFFUSE_ALBEDO 0.18

/// UE's indirect diffuse view (r.Lumen.Visualize.IndirectDiffuse, DiffuseIndirectComposite.usf:565): what the indirect
/// diffuse adds to an 18% grey surface - the GI resolve's (or SSIL's) E / pi times VISUALIZE_DIFFUSE_ALBEDO, the
/// diffuse occlusion pbr_indirect gives it and the energy the specular layer leaves - at the frame's exposure times
/// u_params.y (0 = 1), through the lit image's tone mapping operator. Black where neither ran.
vec3 indirect_diffuse_view(GBufferData data, vec2 texcoord0)
{
    vec4 indirect = texture2D(s_tex7, texcoord0);
    vec3 clip = clipTransform(vec3(texcoord0 * 2.0 - 1.0, data.depth));
    vec3 world_position = clipToWorld(u_invViewProj, clip);
    vec3 N = normalize(data.world_normal);
    vec3 V = normalize(mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz - world_position);
    float screen_ao = ScreenSpaceAO(texture2D(s_tex8, texcoord0).a, u_screen_ao.x);
    vec3 occlusion = IndirectDiffuseOcclusion(data.ambient_occlusion,
                                              screen_ao,
                                              u_screen_ao.z,
                                              u_visualize_indirect.z > 0.5,
                                              MultiBounceAlbedo(data.diffuse_color, u_visualize_indirect.y));
    float energy = IndirectDiffuseEnergyPreservation(data.specular_color, GeometricSpecularAA(N, data.roughness), V, N);
    float scale = u_params.y > 0.0 ? u_params.y : 1.0;
    vec3 radiance = VISUALIZE_DIFFUSE_ALBEDO * indirect.rgb * indirect.a * occlusion * energy * scale;
    return apply_tonemapping(radiance, int(u_visualize_indirect.x), 1.0);
}

/// UE's Dedicated Reflection Rays view (LumenVisualize.ush:49-74, r.Lumen.Visualize 7): the albedo's luminance
/// lit from one direction, and in red, brighter the smoother, every pixel whose roughness Lumen traces
/// reflections for (LumenCombineReflectionsAlpha: below the max roughness to trace, fading over 0.1).
vec3 lumen_reflection_rays(GBufferData data)
{
    vec3 light_direction = vec3(-0.707, 0.707, 0.0);
    float lighting = 0.5 * dot(light_direction, normalize(data.world_normal)) + 0.5;
    vec3 color = vec3_splat(sqrt(dot(data.diffuse_color, vec3(0.3, 0.59, 0.11)) * lighting * 1.5));
    if(saturate((u_max_roughness_to_trace - data.roughness) / 0.1) > 0.0)
    {
        color = vec3(1.0, 0.0, 0.0) * ((1.0 - data.roughness) * 0.8 + 0.2);
    }
    return color;
}

/// The indirect specular of pbr_indirect, ahead of the environment BRDF: the two reflection
/// buffers under their occlusion, the probe layer completed with the environment. Returns the
/// untraced layer's occlusion through @p untraced_occlusion.
vec3 indirect_specular_radiance(GBufferData data, vec2 texcoord0, out vec3 untraced_occlusion)
{
    vec3 clip = clipTransform(vec3(texcoord0 * 2.0 - 1.0, data.depth));
    vec3 world_position = clipToWorld(u_invViewProj, clip);
    vec3 N = normalize(data.world_normal);
    vec3 V = normalize(mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz - world_position);
    vec4 screen_ao_sample = texture2D(s_tex8, texcoord0);
    float screen_ao = ScreenSpaceAO(screen_ao_sample.a, u_screen_ao.x);
    vec3 axis = ScreenSpaceOcclusionAxis(screen_ao_sample, u_screen_ao.w, screen_ao, N);
    float roughness = GeometricSpecularAA(N, data.roughness);
    vec3 dominant_dir = normalize(GetSpecularDominantDir(N, reflect(-V, N), roughness));
    IndirectSpecularOcclusion occlusion =
        ComputeIndirectSpecularOcclusion(s_tex10, N, dominant_dir, roughness, data.specular_color,
                                         data.ambient_occlusion, data.ambient_occlusion * screen_ao, axis, u_screen_ao.z);
    untraced_occlusion = occlusion.untraced;
    vec3 environment = eval_radiance_sh_lobe(s_tex6, dominant_dir, roughness) * u_pre_exposure_value;
    vec3 probe_layer = CompleteProbeLayer(texture2D(s_tex9, texcoord0), environment);
    return ComposeIndirectSpecular(texture2D(s_tex5, texcoord0), probe_layer, occlusion);
}

vec4 gbuffer_visualize(vec2 texcoord0)
{
    GBufferData data = DecodeGBuffer(texcoord0, s_tex0, s_tex1, s_tex2, s_tex3, s_tex4);
	vec3 color = vec3(0.0f, 0.0f, 0.0f);

    if(u_mode == BASE_COLOR)
    {
        color = data.base_color;
    }
    else if(u_mode == DIFFUSE_COLOR)
    {
        color = data.diffuse_color;
    }
    else if(u_mode == SPECULAR_COLOR)
    {
        color = data.specular_color;
    }
    else if(u_mode == RADIANCE)
    {
        // The indirect specular radiance the lighting uses, ahead of the environment BRDF. The
        // reflection buffers carry the view's pre-exposure; the view shows absolute radiance.
        vec3 untraced_occlusion;
        color = indirect_specular_radiance(data, texcoord0, untraced_occlusion) * u_pre_exposure_inverse;
    }
    else if(u_mode == RADIANCE_ALPHA)
    {
        // RBUFFER alpha is the share the traced layers leave to the probe layer.
        color = vec3_splat(1.0 - texture2D(s_tex5, texcoord0).a);
    }
    else if(u_mode == AMBIENT_OCCLUSION)
    {
        color = vec3_splat(data.ambient_occlusion * ScreenSpaceAO(texture2D(s_tex8, texcoord0).a, u_screen_ao.x));
    }
    else if(u_mode == WORLD_NORMAL)
    {
        color = data.world_normal;
    }
    else if(u_mode == ROUGHNESS)
    {
        color = vec3_splat(data.roughness);
    }
    else if(u_mode == METALNESS)
    {
        color = vec3_splat(data.metalness);
    }
    else if(u_mode == EMISSIVE_COLOR)
    {
        color = data.emissive_color;
    }
    else if(u_mode == SUBSURFACE_COLOR)
    {
        color = data.subsurface_color;
    }
    else if(u_mode == DEPTH)
    {
        color = vec3_splat(data.depth);
    }
    else if(u_mode == IRRADIANCE)
    {
        color = eval_irradiance_sh(s_tex6, data.world_normal);
    }
    else if(u_mode == SSIL)
    {
        color = indirect_diffuse_view(data, texcoord0);
    }
    else if(u_mode == SPECULAR_OCCLUSION)
    {
        // The probe layer's occlusion, with the specular multi-bounce (tinted by F0 on metals);
        // the traced layers take only the material AO's.
        vec3 untraced_occlusion;
        indirect_specular_radiance(data, texcoord0, untraced_occlusion);
        color = untraced_occlusion;
    }
    else if(u_mode == AO_BENT_NORMALS)
    {
        // White when the screen-space AO carries no bent normal (ASSAO, or no pass).
        color = u_screen_ao.w > 0.5 ? texture2D(s_tex8, texcoord0).rgb : vec3_splat(1.0);
    }
    else if(u_mode == LUMEN_REFLECTION_RAYS)
    {
        color = lumen_reflection_rays(data);
    }

    // The decode helpers now return LINEAR base color (and colors derived from
    // it); this pass writes straight to the display-encoded output, so encode
    // the color-space views back for correct on-screen reading.
    if(u_mode == BASE_COLOR || u_mode == DIFFUSE_COLOR || u_mode == SPECULAR_COLOR || u_mode == SUBSURFACE_COLOR)
    {
        color = linear_to_srgb(saturate(color));
    }

    return vec4(color, 1.0f);
}

void main()
{
    gl_FragColor = gbuffer_visualize(v_texcoord0);
}
