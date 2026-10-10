/*
 * Reflections, resolve with spatial reconstruction, per pixel. A pixel starts from its own ray at full
 * resolution; at the lowest reflection quality, where one pixel of each 2 x 2 block traces
 * (LumenReflectionTracePixel), from the four traces around it blended by the tent weight (2 - |dx|) (2 - |dy|) of
 * their pixels' distance, untraced ones left out, the hit distance and the ray of the heaviest standing for the
 * pixel's. Above LUMEN_REFLECTION_RECONSTRUCTION_MIN_ROUGHNESS it also reuses the rays of the quality's sample count
 * of neighbours (u_lumen_reflection_reconstruction_samples) on a disk of the downsample factor x
 * LUMEN_REFLECTION_RECONSTRUCTION_KERNEL_RADIUS x saturate(8 roughness) pixels: each neighbour's hit, pulled in to no
 * farther than the pixel's own hit (which keeps contacts and stops background hits from winning), is re-aimed from
 * this pixel, and its radiance weighs in by this pixel's GGX lobe over the density its ray was drawn with - a ratio
 * estimator of this pixel's lobe from its neighbours' rays. The sum runs in the denoiser space.
 *
 * Writes rgb = the resolved radiance (pre-exposed), a = the shortest hit distance used; a = -1 where the pixel
 * traces no ray.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#define LUMEN_REFLECTION_TILES_STAGE 13
#include "lumen/lumen_reflection_common.sh"
#include "gi/gi_reflection_sampling.sh"

IMAGE2D_WO(s_lumen_reflection_resolved_out, rgba16f, 0);
SAMPLER2D(s_lumen_reflection_radiance, 8);
SAMPLER2D(s_lumen_reflection_hit, 9);
SAMPLER2D(s_lumen_reflection_ray, 10);
SAMPLER2D(s_lumen_depth, 11);
/// G-buffer target 1: octahedral normal, metalness, roughness.
SAMPLER2D(s_lumen_normal, 12);

/// The radiance and hit distances of this pixel and its reused neighbours.
struct LumenResolveSum
{
	vec3 radiance;
	float weight;
	float min_hit;
};

/// What a pixel starts from: the sum (weight 1 when a trace serves the pixel, 0 when none does) and the trace texel
/// whose ray stands for the pixel's in the reconstruction.
struct LumenResolveCenter
{
	LumenResolveSum sum;
	ivec2 trace;
};

/// Adds the ray of trace texel @p trace, traced from its pixel and re-aimed from @p position, under this pixel's lobe
/// (normal, view, alpha).
LumenResolveSum LumenResolveNeighbour(LumenResolveSum sum, ivec2 trace, vec3 position, vec3 normal, vec3 view,
                                      float alpha, float center_hit)
{
	vec4 ray = LumenReflectionTraceRay(s_lumen_reflection_ray, trace);
	if(ray.w <= 0.0)
	{
		return sum;
	}
	ivec2 q = LumenReflectionTracePixel(trace);
	vec3 q_position = LumenWorldFromDepth(LumenPixelUv(q), texelFetch(s_lumen_depth, q, 0).x);
	float q_hit = min(abs(texelFetch(s_lumen_reflection_hit, trace, 0).x), center_hit);
	vec3 to_hit = q_position + ray.xyz * q_hit - position;
	float distance = length(to_hit);
	vec3 direction = distance > 0.0 ? to_hit / distance : ray.xyz;
	float weight = GiReflectionSampleWeight(normal, view, alpha, direction, 1.0 / ray.w);
	if(weight > 1e-6)
	{
		sum.radiance += LumenReflectionToDenoiserSpace(texelFetch(s_lumen_reflection_radiance, trace, 0).xyz) * weight;
		sum.weight += weight;
		sum.min_hit = min(sum.min_hit, q_hit);
	}
	return sum;
}

/// The pixel's own trace (full resolution).
LumenResolveCenter LumenResolveOwnTrace(ivec2 pixel)
{
	LumenResolveCenter center;
	center.trace = pixel;
	center.sum.radiance = LumenReflectionToDenoiserSpace(texelFetch(s_lumen_reflection_radiance, pixel, 0).xyz);
	center.sum.weight = 1.0;
	center.sum.min_hit = abs(texelFetch(s_lumen_reflection_hit, pixel, 0).x);
	return center;
}

/// The four traces around @p pixel at the 2 x 2 downsample: the trace texels whose blocks surround it, each weighed
/// by the tent (2 - |dx|) (2 - |dy|) of its traced pixel's offset from @p pixel and left out when untraced or outside
/// the traces. Serves the pixel when a weight passes 0.01.
LumenResolveCenter LumenResolveUpsampledTraces(ivec2 pixel)
{
	ivec2 shifted = max(pixel, ivec2(1, 1));
	ivec2 base = (shifted - ivec2(1, 1)) / 2;
	ivec2 offset_in_block = shifted - base * 2;
	ivec2 trace_size = LumenReflectionTraceSize();
	LumenResolveCenter center;
	center.trace = base;
	center.sum.radiance = vec3_splat(0.0);
	center.sum.weight = 0.0;
	center.sum.min_hit = u_lumen_max_trace_distance;
	vec3 radiance = vec3_splat(0.0);
	float weight_sum = 0.0;
	float best_weight = 0.0;
	for(int corner = 0; corner < 4; ++corner)
	{
		ivec2 corner_offset = ivec2(corner - (corner / 2) * 2, corner / 2);
		ivec2 trace = base + corner_offset;
		if(any(greaterThanEqual(trace, trace_size)) || LumenReflectionTraceRay(s_lumen_reflection_ray, trace).w <= 0.0)
		{
			continue;
		}
		ivec2 to_sample = LumenReflectionTraceJitter(trace) + corner_offset * 2 - offset_in_block;
		float weight = (2.0 - abs(float(to_sample.x))) * (2.0 - abs(float(to_sample.y)));
		radiance += LumenReflectionToDenoiserSpace(texelFetch(s_lumen_reflection_radiance, trace, 0).xyz) * weight;
		weight_sum += weight;
		// The heaviest trace, the first on a tie.
		if(weight > best_weight)
		{
			best_weight = weight;
			center.trace = trace;
		}
	}
	if(best_weight > 0.01)
	{
		center.sum.radiance = radiance / max(weight_sum, 0.001);
		center.sum.weight = 1.0;
		center.sum.min_hit = abs(texelFetch(s_lumen_reflection_hit, center.trace, 0).x);
	}
	return center;
}

/// x = the minimum weight of the neighbouring rays over the pixel's own (lumen_pass::get_reflection_reconstruction_min_weight).
uniform vec4 u_lumen_reflection_resolve;

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	ivec2 view_size = ivec2(u_lumen_view_size);
	// A tile that traces nothing resolves nothing (the group is the tile at full resolution).
	if(pixel.x >= view_size.x || pixel.y >= view_size.y || !LumenReflectionTileTraces(pixel))
	{
		return;
	}
	float depth01 = texelFetch(s_lumen_depth, pixel, 0).x;
	vec4 gbuffer1 = texelFetch(s_lumen_normal, pixel, 0);
	float roughness = gbuffer1.w;
	// The pixels that trace (cs_lumen_reflection_screen.sc), or at the downsample read the traces around them.
	if(depth01 >= 1.0 || LumenReflectionFadeAlpha(roughness) <= 0.0)
	{
		imageStore(s_lumen_reflection_resolved_out, pixel, vec4(0.0, 0.0, 0.0, -1.0));
		return;
	}
	int downsample = u_lumen_reflection_downsample;
	LumenResolveCenter center;
	BRANCH
	if(downsample > 1)
	{
		center = LumenResolveUpsampledTraces(pixel);
	}
	else
	{
		center = LumenResolveOwnTrace(pixel);
	}
	LumenResolveSum sum = center.sum;
	float radius = roughness > LUMEN_REFLECTION_RECONSTRUCTION_MIN_ROUGHNESS
	                   ? float(downsample) * LUMEN_REFLECTION_RECONSTRUCTION_KERNEL_RADIUS * saturate(roughness * 8.0)
	                   : 0.0;
	// The reconstruction is skipped for kernels no wider than a traced block.
	BRANCH
	if(radius > float(downsample))
	{
		vec3 position = LumenWorldFromDepth(LumenPixelUv(pixel), depth01);
		vec3 normal = normalize(decodeNormalOctahedron(gbuffer1.xy));
		vec3 view = normalize(LumenReflectionCamera() - position);
		float alpha = roughness * roughness;
		float center_hit = sum.min_hit;
		// Without a serving trace the neighbours alone resolve the pixel.
		if(sum.weight > 0.0)
		{
			vec4 ray = texelFetch(s_lumen_reflection_ray, center.trace, 0);
			float center_weight = max(GiReflectionSampleWeight(normal, view, alpha, ray.xyz, 1.0 / ray.w), 0.001);
			sum.radiance *= center_weight;
			sum.weight = center_weight;
		}
		LumenResolveSum neighbours;
		neighbours.radiance = vec3_splat(0.0);
		neighbours.weight = 0.0;
		neighbours.min_hit = sum.min_hit;
		uvec2 seed = Rand3DPCG16(ivec3(pixel, int(u_lumen_frame_mod))).xy;
		int samples = u_lumen_reflection_reconstruction_samples;
		for(int i = 0; i < samples; ++i)
		{
			vec2 offset = LumenUniformSampleDiskConcentric(Hammersley16(uint(i), uint(samples), seed)) * radius;
			ivec2 q = ivec2(vec2(pixel) + offset + 0.5);
			if(all(greaterThanEqual(q, ivec2(0, 0))) && all(lessThan(q, view_size)))
			{
				neighbours = LumenResolveNeighbour(neighbours, q / downsample, position, normal, view, alpha, center_hit);
			}
		}
		// The neighbours weigh at least the tier's minimum share of the pixel's own ray (0 at the epic tier,
		// 1 at high).
		float min_weight = u_lumen_reflection_resolve.x * sum.weight;
		if(neighbours.weight > 1e-6 && min_weight > neighbours.weight)
		{
			neighbours.radiance *= min_weight / neighbours.weight;
			neighbours.weight = min_weight;
		}
		sum.radiance += neighbours.radiance;
		sum.weight += neighbours.weight;
		sum.min_hit = neighbours.min_hit;
	}
	vec3 resolved = LumenReflectionFromDenoiserSpace(sum.radiance / max(sum.weight, 1e-6));
	imageStore(s_lumen_reflection_resolved_out, pixel, vec4(resolved, sum.min_hit));
}
