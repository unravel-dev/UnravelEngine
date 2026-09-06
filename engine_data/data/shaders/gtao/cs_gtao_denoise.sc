/*
 * GTAO spatial denoise: a 5x5 blur of the visibility and bent normal at the AO resolution,
 * weighted by XeGTAO's depth edges (slope-adjusted relative depth differences to the
 * centre) so the noise of the stochastic slices averages out within a surface and never
 * across a silhouette. No G-buffer normal in the weights: it carries the normal map, and
 * stopping the blur at every bump would keep the noise the blur exists to remove. Run one
 * to three times (ping-pong); the values stay at the stored occlusion-term scale.
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
	vec3 bent_sum = vec3_splat(0.0);
	float ao_sum = 0.0;
	float w_sum = 0.0;
	LOOP
	for(int dy = -2; dy <= 2; ++dy)
	{
		LOOP
		for(int dx = -2; dx <= 2; ++dx)
		{
			ivec2 tap = clamp(texel + ivec2(dx, dy), ivec2(0, 0), size - ivec2(1, 1));
			float tap_depth = texelFetch(s_gtao_depth_mips, tap, 0).x;
			if(tap_depth >= GTAO_SKY_DEPTH * 0.5)
			{
				continue;
			}
			vec4 tap_value = texelFetch(s_gtao_input, tap, 0);
			float spatial = exp(-float(dx * dx + dy * dy) * 0.25);
			float edge_w = saturate(1.25 - abs(tap_depth - center_depth) / edge_scale);
			float w = spatial * edge_w;
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
