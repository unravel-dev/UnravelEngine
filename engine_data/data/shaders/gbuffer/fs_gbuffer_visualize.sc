$input v_texcoord0

#include "../common.sh"
#include "../lighting.sh"

SAMPLER2D(s_tex0, 0);
SAMPLER2D(s_tex1, 1);
SAMPLER2D(s_tex2, 2);
SAMPLER2D(s_tex3, 3);
SAMPLER2D(s_tex4, 4);
SAMPLER2D(s_tex5, 5);
SAMPLER2D(s_tex6, 6);
SAMPLER2D(s_tex7, 7);
// GTAO output: rgb = world bent normal * 0.5 + 0.5, a = visibility.
SAMPLER2D(s_tex8, 8);

uniform vec4 u_params;

#define u_mode int(u_params.x)

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
#define GTAO 15
#define GTAO_BENT_NORMAL 16

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
        color = texture2D(s_tex5, texcoord0).xyz;
    }
    else if(u_mode == RADIANCE_ALPHA)
    {
        color = vec3_splat(texture2D(s_tex5, texcoord0).a);
    }
    else if(u_mode == AMBIENT_OCCLUSION)
    {
        color = vec3_splat(data.ambient_occlusion);
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
        vec4 ssil = texture2D(s_tex7, texcoord0);
		color = ssil.rgb * PI * ssil.a;
    }
    else if(u_mode == SPECULAR_OCCLUSION)
    {
        vec3 clip = vec3(texcoord0 * 2.0 - 1.0, data.depth);
        clip = clipTransform(clip);
        vec3 world_position = clipToWorld(u_invViewProj, clip);
        vec3 view_position = mul(u_view, vec4(world_position, 1.0)).xyz;
        vec3 view_normal = normalize(mul(u_view, vec4(data.world_normal, 0.0)).xyz);
        float NoV = max(saturate(dot(view_normal, normalize(-view_position))), 1e-5);
        float lighting_visibility = saturate(sqrt(Luminance(eval_irradiance_sh(s_tex6, data.world_normal))));
        float occlusion = ComputeSpecularOcclusion(NoV, data.roughness, data.ambient_occlusion, lighting_visibility);
        // Times the GTSO cone term when GTAO is bound (a white fallback opens the cone to
        // the hemisphere, so the term is 1 without it).
        vec4 gtao = texture2D(s_tex8, texcoord0);
        vec3 bent_normal = gtao.xyz * 2.0 - vec3_splat(1.0);
        if(dot(bent_normal, bent_normal) > 1e-4)
        {
            vec3 view_vec = normalize(mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz - world_position);
            occlusion *= ConeConeSpecularOcclusion(view_vec, normalize(data.world_normal), normalize(bent_normal), gtao.a, data.roughness);
        }
        color = vec3_splat(occlusion);
    }
    else if(u_mode == GTAO)
    {
        color = vec3_splat(texture2D(s_tex8, texcoord0).a);
    }
    else if(u_mode == GTAO_BENT_NORMAL)
    {
        color = texture2D(s_tex8, texcoord0).rgb;
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
