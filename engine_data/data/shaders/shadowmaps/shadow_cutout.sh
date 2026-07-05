#ifndef SHADOW_CUTOUT_SH
#define SHADOW_CUTOUT_SH

SAMPLER2D(s_tex_color, 0);
uniform vec4 u_base_color;
uniform vec4 u_surface_data;
uniform vec4 u_tiling;

#define u_surface_alpha_test_value u_surface_data.w

void shadowCutoutClip(vec2 texcoord0)
{
	vec2 tc = texcoord0.xy * u_tiling.xy;
	float a = texture2D(s_tex_color, tc).a * u_base_color.a;


	float cutoff = u_surface_alpha_test_value;
	if (a < cutoff)
	{
		discard;
	}
}

#endif // SHADOW_CUTOUT_SH
