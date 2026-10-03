/*
 * Lumen reflections, resolve (UE 5.8 LumenReflectionResolveCS, LumenReflectionResolve.usf:75-658, full resolution,
 * spatial reconstruction on). Every traced pixel starts from its own ray. Above
 * LUMEN_REFLECTION_RECONSTRUCTION_MIN_ROUGHNESS it also reuses the rays of LUMEN_REFLECTION_RECONSTRUCTION_SAMPLES
 * neighbours on a disk of LUMEN_REFLECTION_RECONSTRUCTION_KERNEL_RADIUS x saturate(8 roughness) pixels: each
 * neighbour's hit, pulled in to no farther than the pixel's own hit (which keeps contacts and stops background
 * hits from winning), is re-aimed from this pixel, and its radiance weighs in by this pixel's GGX lobe over the
 * density its ray was drawn with - a ratio estimator of this pixel's lobe from its neighbours' rays. The sum runs
 * in the denoiser space.
 *
 * Writes rgb = the resolved radiance (pre-exposed), a = the shortest hit distance used; a = -1 where the pixel
 * traces no ray.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
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

/// Adds the ray of neighbour @p q, re-aimed from @p position, under this pixel's lobe (normal, view, alpha).
LumenResolveSum LumenResolveNeighbour(LumenResolveSum sum, ivec2 q, vec3 position, vec3 normal, vec3 view, float alpha,
                                      float center_hit)
{
	vec4 ray = texelFetch(s_lumen_reflection_ray, q, 0);
	if(ray.w <= 0.0)
	{
		return sum;
	}
	vec3 q_position = LumenWorldFromDepth(LumenPixelUv(q), texelFetch(s_lumen_depth, q, 0).x);
	float q_hit = min(abs(texelFetch(s_lumen_reflection_hit, q, 0).x), center_hit);
	vec3 to_hit = q_position + ray.xyz * q_hit - position;
	float distance = length(to_hit);
	vec3 direction = distance > 0.0 ? to_hit / distance : ray.xyz;
	float weight = GiReflectionSampleWeight(normal, view, alpha, direction, 1.0 / ray.w);
	if(weight > 1e-6)
	{
		sum.radiance += LumenReflectionToDenoiserSpace(texelFetch(s_lumen_reflection_radiance, q, 0).xyz) * weight;
		sum.weight += weight;
		sum.min_hit = min(sum.min_hit, q_hit);
	}
	return sum;
}

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	ivec2 view_size = ivec2(u_lumen_view_size);
	if(pixel.x >= view_size.x || pixel.y >= view_size.y)
	{
		return;
	}
	vec4 ray = texelFetch(s_lumen_reflection_ray, pixel, 0);
	if(ray.w <= 0.0)
	{
		imageStore(s_lumen_reflection_resolved_out, pixel, vec4(0.0, 0.0, 0.0, -1.0));
		return;
	}
	LumenResolveSum center;
	center.radiance = LumenReflectionToDenoiserSpace(texelFetch(s_lumen_reflection_radiance, pixel, 0).xyz);
	center.weight = 1.0;
	center.min_hit = abs(texelFetch(s_lumen_reflection_hit, pixel, 0).x);
	vec4 gbuffer1 = texelFetch(s_lumen_normal, pixel, 0);
	float roughness = gbuffer1.w;
	float radius = roughness > LUMEN_REFLECTION_RECONSTRUCTION_MIN_ROUGHNESS
	                   ? LUMEN_REFLECTION_RECONSTRUCTION_KERNEL_RADIUS * saturate(roughness * 8.0)
	                   : 0.0;
	BRANCH
	if(radius > 1.0)
	{
		vec3 position = LumenWorldFromDepth(LumenPixelUv(pixel), texelFetch(s_lumen_depth, pixel, 0).x);
		vec3 normal = normalize(decodeNormalOctahedron(gbuffer1.xy));
		vec3 view = normalize(LumenReflectionCamera() - position);
		float alpha = roughness * roughness;
		float center_hit = center.min_hit;
		float center_weight = max(GiReflectionSampleWeight(normal, view, alpha, ray.xyz, 1.0 / ray.w), 0.001);
		center.radiance *= center_weight;
		center.weight = center_weight;
		LumenResolveSum neighbours;
		neighbours.radiance = vec3_splat(0.0);
		neighbours.weight = 0.0;
		neighbours.min_hit = center.min_hit;
		uvec2 seed = Rand3DPCG16(ivec3(pixel, int(u_lumen_frame_mod))).xy;
		for(int i = 0; i < LUMEN_REFLECTION_RECONSTRUCTION_SAMPLES; ++i)
		{
			vec2 offset = LumenUniformSampleDiskConcentric(
			                  Hammersley16(uint(i), uint(LUMEN_REFLECTION_RECONSTRUCTION_SAMPLES), seed)) * radius;
			ivec2 q = ivec2(vec2(pixel) + offset + 0.5);
			if(all(greaterThanEqual(q, ivec2(0, 0))) && all(lessThan(q, view_size)))
			{
				neighbours = LumenResolveNeighbour(neighbours, q, position, normal, view, alpha, center_hit);
			}
		}
		center.radiance += neighbours.radiance;
		center.weight += neighbours.weight;
		center.min_hit = neighbours.min_hit;
	}
	vec3 resolved = LumenReflectionFromDenoiserSpace(center.radiance / max(center.weight, 1e-6));
	imageStore(s_lumen_reflection_resolved_out, pixel, vec4(resolved, center.min_hit));
}
