/*
 * GTAO upsample: writes the full-resolution "GTAO" texture from the AO-resolution result.
 * At full resolution this is a copy; at reduced resolution each pixel takes the bilinear
 * footprint's four AO texels weighted by how close their view depth is to the pixel's own,
 * so silhouettes stay sharp (the same joint-bilateral idea the GI upsample uses).
 */

#include "../bgfx_compute.sh"
#include "../common.sh"
#include "gtao_common.sh"

SAMPLER2D(s_gtao_input, 0);
SAMPLER2D(s_gtao_depth_mips, 1);
/// Full-resolution G-buffer depth.
SAMPLER2D(s_gtao_depth, 2);
IMAGE2D_WO(i_gtao_out, rgba8, 3);

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
	ivec2 full_size = ivec2(u_gtao_full_size.xy);
	if(any(greaterThanEqual(texel, full_size)))
	{
		return;
	}
	ivec2 ao_size = ivec2(u_gtao_size.xy);
	if(all(equal(ao_size, full_size)))
	{
		vec4 same = texelFetch(s_gtao_input, texel, 0);
		imageStore(i_gtao_out, texel, vec4(same.xyz, saturate(same.a * GTAO_OCCLUSION_TERM_SCALE)));
		return;
	}
	float device_depth = texelFetch(s_gtao_depth, texel, 0).x;
	float view_depth = GtaoViewDepthFromDevice(device_depth);
	// The pixel centre in AO texel space; the four surrounding AO texels and their bilinear weights.
	vec2 uv = (vec2(texel) + vec2_splat(0.5)) * u_gtao_full_size.zw;
	vec2 ao_pos = uv * u_gtao_size.xy - vec2_splat(0.5);
	vec2 base = floor(ao_pos);
	vec2 frac_part = ao_pos - base;
	ivec2 base_texel = ivec2(base);
	float depth_sigma = max(view_depth * u_gtao_depth_sigma, 1e-4);
	vec4 sum = vec4_splat(0.0);
	float w_sum = 0.0;
	vec4 nearest = vec4_splat(0.0);
	float nearest_w = -1.0;
	LOOP
	for(int i = 0; i < 4; ++i)
	{
		ivec2 offset = ivec2(i & 1, (i >> 1) & 1);
		ivec2 tap = clamp(base_texel + offset, ivec2(0, 0), ao_size - ivec2(1, 1));
		vec2 bilinear = mix(vec2_splat(1.0) - frac_part, frac_part, vec2(offset));
		float w_bilinear = bilinear.x * bilinear.y;
		float tap_depth = texelFetch(s_gtao_depth_mips, tap, 0).x;
		float w_depth = exp(-abs(tap_depth - view_depth) / depth_sigma);
		float w = w_bilinear * w_depth;
		vec4 value = texelFetch(s_gtao_input, tap, 0);
		sum += value * w;
		w_sum += w;
		if(w_bilinear > nearest_w)
		{
			nearest_w = w_bilinear;
			nearest = value;
		}
	}
	vec4 result = w_sum > 1e-4 ? sum / w_sum : nearest;
	vec3 bent = result.xyz * 2.0 - vec3_splat(1.0);
	vec3 bent_out = dot(bent, bent) > 1e-8 ? normalize(bent) : GtaoDecodeNormal(nearest);
	// Back from the stored occlusion-term scale to the [0, 1] the lighting consumes.
	imageStore(i_gtao_out, texel, GtaoEncode(bent_out, saturate(result.a * GTAO_OCCLUSION_TERM_SCALE)));
}
