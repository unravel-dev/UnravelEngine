$input v_texcoord0

#include "../common.sh"
#include "../lighting.sh"
#include "../pre_exposure.sh"

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

uniform vec4 u_params;
/// x = screen-space AO intensity, w = 1 when the texture is GTAO's (bent normal); yz unused.
uniform vec4 u_screen_ao;

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
#define AO_BENT_NORMALS 15

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
        // RBUFFER carries the view's pre-exposure; the view shows absolute radiance.
        color = texture2D(s_tex5, texcoord0).xyz * u_pre_exposure_inverse;
    }
    else if(u_mode == RADIANCE_ALPHA)
    {
        color = vec3_splat(texture2D(s_tex5, texcoord0).a);
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
        vec4 ssil = texture2D(s_tex7, texcoord0);
		color = ssil.rgb * PI * ssil.a * u_pre_exposure_inverse;
        // Linear readback scale (the scene panel's debug-view scale, u_params.y; 0 = 1): the
        // target is the LDR frame, so a scale lets a capture read the gather's radiance at
        // any magnitude to 8-bit precision.
        color *= u_params.y > 0.0 ? u_params.y : 1.0;
    }
    else if(u_mode == SPECULAR_OCCLUSION)
    {
        vec3 clip = vec3(texcoord0 * 2.0 - 1.0, data.depth);
        clip = clipTransform(clip);
        vec3 world_position = clipToWorld(u_invViewProj, clip);
        vec3 N = normalize(data.world_normal);
        vec3 V = normalize(mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz - world_position);
        float ambient_occlusion = data.ambient_occlusion * ScreenSpaceAO(texture2D(s_tex8, texcoord0).a, u_screen_ao.x);
        color = vec3_splat(ComputeSpecularOcclusion(N, V, GeometricSpecularAA(N, data.roughness), ambient_occlusion));
    }
    else if(u_mode == AO_BENT_NORMALS)
    {
        // White when the screen-space AO carries no bent normal (ASSAO, or no pass).
        color = u_screen_ao.w > 0.5 ? texture2D(s_tex8, texcoord0).rgb : vec3_splat(1.0);
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
