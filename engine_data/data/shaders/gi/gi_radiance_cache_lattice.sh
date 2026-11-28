#ifndef __GI_RADIANCE_CACHE_LATTICE_SH__
#define __GI_RADIANCE_CACHE_LATTICE_SH__
/*
 * The lattice probes a radiance cache lookup interpolates: the eight probes around a point in the clipmap it reads,
 * with their trilinear weights. Shared by the screen-probe hand-off (gi_radiance_cache_sample.sh) and what is lit
 * outside the G-buffer (gi_radiance_cache_surface.sh: the translucency pass's surfaces and the lit particles).
 *
 * The includer declares b_gi_rc_indirection (uint, indexable) and includes gi_radiance_cache_common.sh.
 */

/// One lattice probe of a lookup: its cell, its probe index (GI_RC_INVALID when the cell holds none) and its weight.
struct GiRcLatticeProbe
{
	ivec3 cell;
	uint probe;
	float weight;
};

/// Corner @p corner (0-7) of the lattice cube around @p origin in @p clipmap, which must cover it (GiRcClipmapOf).
GiRcLatticeProbe GiRcInterpolationProbe(vec3 origin, int clipmap, int corner)
{
	GiRcLatticeProbe result;
	vec4 clip = u_gi_rc_clipmaps[clipmap];
	vec3 lattice = (origin - clip.xyz) / clip.w - 0.5;
	vec3 base_float = floor(lattice);
	vec3 fraction = lattice - base_float;
	ivec3 step_xyz = ivec3(corner & 1, (corner >> 1) & 1, corner >> 2);
	vec3 axis_weight = mix(vec3_splat(1.0) - fraction, fraction, vec3(step_xyz));
	result.weight = axis_weight.x * axis_weight.y * axis_weight.z;
	result.cell = ivec3(base_float) + step_xyz;
	uint probe = b_gi_rc_indirection[GiRcIndirectionIndex(result.cell, clipmap)];
	result.probe = probe == GI_RC_USED ? GI_RC_INVALID : probe;
	return result;
}

#endif // __GI_RADIANCE_CACHE_LATTICE_SH__
