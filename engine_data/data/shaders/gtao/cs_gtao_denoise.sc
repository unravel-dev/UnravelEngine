/*
 * GTAO spatial denoise: a 5x5 blur of the visibility and bent normal at the AO resolution,
 * run SEPARABLY - each pass takes 5 taps along one axis (u_gtao_denoise_axis: x, then y),
 * a fifth of the fetches of the square kernel for a near-identical result - weighted by
 * XeGTAO's depth edges (slope-adjusted relative depth differences to the centre) so the
 * noise of the stochastic slices averages out within a surface and never across a
 * silhouette. When the shading normal is the receiver normal the G-buffer normal joins the
 * weights: the bump-scale response the normal map produced must survive the blur (with the
 * geometric source there is none to keep, and stopping at every bump would only preserve
 * noise). Run one to three times (ping-pong, alternating axes); the values stay at the
 * stored occlusion-term scale.
 */

#include "../bgfx_compute.sh"
#include "../common.sh"
#include "gtao_common.sh"

SAMPLER2D(s_gtao_input, 0);
SAMPLER2D(s_gtao_depth_mips, 1);
SAMPLER2D(s_gtao_normal, 2);
IMAGE2D_WO(i_gtao_out, rgba8, 3);

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = ivec2(u_gtao_size.xy);
	if(any(greaterThanEqual(texel, size)))
	{
		return;
	}
	vec2 uv = (vec2(texel) + vec2_splat(0.5)) * u_gtao_size.zw;
	vec4 center = texelFetch(s_gtao_input, texel, 0);
	float center_depth = texelFetch(s_gtao_depth_mips, texel, 0).x;
	if(center_depth >= GTAO_SKY_DEPTH * 0.5)
	{
		imageStore(i_gtao_out, texel, center);
		return;
	}
	// XeGTAO's edge scale: a relative depth difference of 1.1% of the centre depth is a
	// half edge, 1.375% a full edge.
	float edge_scale = center_depth * 0.011;
	bool normal_weights = u_gtao_normal_source < 0.5;
	vec3 center_normal = GtaoWorldNormal(s_gtao_normal, texel);
	vec3 bent_sum = vec3_splat(0.0);
	float ao_sum = 0.0;
	float w_sum = 0.0;
	ivec2 axis = u_gtao_denoise_axis > 0.5 ? ivec2(0, 1) : ivec2(1, 0);
	LOOP
	for(int d = -2; d <= 2; ++d)
	{
		ivec2 tap = clamp(texel + axis * d, ivec2(0, 0), size - ivec2(1, 1));
		float tap_depth = texelFetch(s_gtao_depth_mips, tap, 0).x;
		if(tap_depth < GTAO_SKY_DEPTH * 0.5)
		{
			vec4 tap_value = texelFetch(s_gtao_input, tap, 0);
			float spatial = exp(-float(d * d) * 0.25);
			float edge_w = saturate(1.25 - abs(tap_depth - center_depth) / edge_scale);
			float w = spatial * edge_w;
			if(normal_weights)
			{
				vec3 tap_normal = GtaoWorldNormal(s_gtao_normal, tap);
				w *= pow(saturate(dot(tap_normal, center_normal)), u_gtao_normal_power);
			}
			ao_sum += tap_value.a * w;
			bent_sum += (tap_value.xyz * 2.0 - vec3_splat(1.0)) * w;
			w_sum += w;
		}
	}
	if(w_sum <= 1e-5)
	{
		imageStore(i_gtao_out, texel, center);
		return;
	}
	vec3 bent = bent_sum / w_sum;
	vec3 bent_out = dot(bent, bent) > 1e-8 ? normalize(bent) : GtaoDecodeNormal(center);
	imageStore(i_gtao_out, texel, GtaoEncode(bent_out, ao_sum / w_sum));
}
