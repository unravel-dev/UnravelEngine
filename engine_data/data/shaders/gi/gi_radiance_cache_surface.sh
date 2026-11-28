#ifndef __GI_RADIANCE_CACHE_SURFACE_SH__
#define __GI_RADIANCE_CACHE_SURFACE_SH__
/*
 * What a lit surface outside the G-buffer reads of the radiance cache: the L1 SH of the probes around a point, which
 * the cache keeps for the points such surfaces mark (gi_radiance_cache::frame_inputs::surface_points). Shared by the
 * translucency pass's surfaces (material_graph/mg_forward.sh FORWARD_GI) and the lit particles
 * (particles/particle_lighting.sh PARTICLE_GI).
 *
 * The includer includes shader_buffer.sh and gi_radiance_cache_common.sh and declares the two buffers at its stages:
 * b_gi_rc_indirection (uint) and b_gi_rc_surface_sh (float4): per probe four vec4, the red, green and blue
 * coefficients (Y00, Y1-1, Y10, Y11), absolute (GI_CACHED_LIGHTING_PRE_EXPOSURE is 1), and its cell.
 */

#include "gi/gi_radiance_cache_lattice.sh"

/// The GI's light at a point: its L1 SH per colour, and how much of the lattice around the point has probes.
struct GiRcSurfaceSh
{
	vec4 red;
	vec4 green;
	vec4 blue;
	float coverage;
};

/**
 * The GI's L1 SH at the world @p position: the eight lattice probes around it in the finest clipmap that covers it,
 * weighted trilinearly over the ones that exist and renormalised, times @p intensity (the GI's diffuse intensity), with
 * their share of the weights as the coverage (0 outside the clipmaps or where no probe is traced yet). The probes' rays
 * start a cell diagonal out, so a surface right beside a wall can take some of the light from its other side.
 */
GiRcSurfaceSh GiRcSurfaceShAt(vec3 position, float intensity)
{
	GiRcSurfaceSh sh;
	sh.red = vec4_splat(0.0);
	sh.green = vec4_splat(0.0);
	sh.blue = vec4_splat(0.0);
	sh.coverage = 0.0;
	int clipmap = GiRcClipmapOf(position);
	if(clipmap >= GI_RADIANCE_CACHE_CLIPMAPS)
	{
		return sh;
	}
	for(int corner = 0; corner < 8; ++corner)
	{
		GiRcLatticeProbe lattice = GiRcInterpolationProbe(position, clipmap, corner);
		if(lattice.probe == GI_RC_INVALID)
		{
			continue;
		}
		uint probe = lattice.probe;
		// The SH must be this probe's: projected at its position, or by the full pass that knows none. A probe not traced
		// since it took a slot holds another place's light, or none.
		vec4 key = shader_buffer_float4(b_gi_rc_surface_sh, 4u * probe + 3u);
		vec3 offset = abs(key.xyz - GiRcProbePosition(lattice.cell, clipmap));
		bool is_projected = key.w > 0.75 ? all(lessThan(offset, vec3_splat(0.01 * u_gi_rc_clipmaps[clipmap].w))) : key.w > 0.25;
		if(!is_projected)
		{
			continue;
		}
		sh.red += lattice.weight * shader_buffer_float4(b_gi_rc_surface_sh, 4u * probe);
		sh.green += lattice.weight * shader_buffer_float4(b_gi_rc_surface_sh, 4u * probe + 1u);
		sh.blue += lattice.weight * shader_buffer_float4(b_gi_rc_surface_sh, 4u * probe + 2u);
		sh.coverage += lattice.weight;
	}
	if(sh.coverage > 1e-4)
	{
		float normalise = max(intensity, 0.0) / sh.coverage;
		sh.red *= normalise;
		sh.green *= normalise;
		sh.blue *= normalise;
	}
	return sh;
}

/// @p sh along @p direction with the cosine lobe's band constants times @p band_scale (eval_irradiance_sh_scaled's
/// first two bands): the irradiance with (1, 1), the average over every direction with (1, 0), a rough reflection's
/// lobe with sh_lobe_band_scale.
vec3 GiRcSurfaceShEval(GiRcSurfaceSh sh, vec3 direction, vec2 band_scale)
{
	vec4 lobe = vec4(PI * band_scale.x * 0.282095,
	                 (2.0 * PI / 3.0) * band_scale.y * 0.488603 * direction.y,
	                 (2.0 * PI / 3.0) * band_scale.y * 0.488603 * direction.z,
	                 (2.0 * PI / 3.0) * band_scale.y * 0.488603 * direction.x);
	return max(vec3(dot(sh.red, lobe), dot(sh.green, lobe), dot(sh.blue, lobe)), vec3_splat(0.0));
}

/// How much of a surface's indirect light @p sh gives, the sky giving the rest: all of it from half the lattice's
/// weight on, so a point at the edge of the marked cells fades into the sky's light.
float GiRcSurfaceShShare(GiRcSurfaceSh sh)
{
	return saturate(sh.coverage * 2.0);
}

#endif // __GI_RADIANCE_CACHE_SURFACE_SH__
