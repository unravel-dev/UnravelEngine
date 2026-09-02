#ifndef __GI_LIGHT_VOXELS_SH__
#define __GI_LIGHT_VOXELS_SH__

/*
 * Light voxels (gi_rewrite_plan.md 3.2): outgoing radiance per EXPOSED FACE of every surface voxel of
 * the SDF cascade, at attribute resolution. This is what a traced ray reads at a cascade hit -
 * the positional replacement for the surface-addressed radiance hash, with no writer/reader
 * agreement problem: the address is the voxel.
 *
 * LAYOUT: one 3D texture; Z stacks (level, face) slabs of attr_resolution each:
 *   slab z = ((level * 6 + face) * attr_resolution) + voxel_z
 * Face convention matches the cube-face encoding used across GI: face = axis * 2 + (negative),
 * i.e. +X, -X, +Y, -Y, +Z, -Z. rgb = outgoing radiance (albedo/pi * E + emissive), a = 1 where
 * the face is exposed and lit, 0 where it measured nothing.
 *
 * Two sides of a wall live in different face slabs of the same voxel - the positional analogue
 * of the old cache's normal-in-key leak defence.
 */

#include "gi_constants.sh"

#ifndef GI_FINITE_OR_ZERO_DEFINED
#define GI_FINITE_OR_ZERO_DEFINED
/// Zero when non-finite (or absurdly large): the probe<->voxel feedback loop has no decay for
/// a NaN - each side re-ingests the other every cycle, so one poisoned texel converges the
/// whole field to NaN within a window (measured on Linux/Vulkan, where never-written texels
/// read as garbage: a white flash, then GI collapsing to black). An ordered comparison is
/// used rather than isnan(), which relaxed-math shader compilation may fold away; NaN fails
/// every ordered comparison, so it cannot pass this one.
vec3 GiFiniteOrZero(vec3 v)
{
	return all(lessThan(abs(v), vec3_splat(1e30))) ? v : vec3_splat(0.0);
}
#endif // GI_FINITE_OR_ZERO_DEFINED

// See sdf_common.sh: modern GLSL removed the legacy entry point and bgfx never mapped the 3D
// variant; guarded here too so this header stands alone.
#if BGFX_SHADER_LANGUAGE_GLSL >= 130 && !defined(texture3DLod)
#	define texture3DLod(_sampler, _coord, _lod) textureLod(_sampler, _coord, _lod)
#endif // BGFX_SHADER_LANGUAGE_GLSL >= 130

/// x = attribute resolution (voxels per axis), y = telemetry mirror of the sun-tier debug
/// state (the kernel does NOT read it - the debug write is a compiled program variant, see
/// gi_light_voxels_kernel.sh; the lane exists so a GPU debugger can inspect whether uniforms
/// arrive), z = frame index, w = non-zero when the light volume is resident.
uniform vec4 u_gi_light_voxel_params;
#define u_light_voxel_resolution int(u_gi_light_voxel_params.x)
#define u_light_voxel_frame      uint(u_gi_light_voxel_params.z)
#define u_light_voxel_ready      (u_gi_light_voxel_params.w > 0.0)

/// The relight convergence statistic's texel: one slice past the last face slab of the
/// bounce vis-memo texture (allocated one slice deeper than the light volume for it); x =
/// level, y = quantity (0 = summed relative change x GI_QUIESCENCE_STATS_SCALE, 1 = relit
/// face count). Written by the group reduction in gi_light_voxels_kernel.sh, copied out
/// and zeroed by cs_gi_light_voxel_stats.sc.
ivec3 GiLightVoxelStatsTexel(int level, int quantity)
{
	return ivec3(level, quantity, u_light_voxel_resolution * SDF_CLIPMAP_LEVEL_COUNT * 6);
}

/// TOROIDAL world anchoring: a voxel's storage SLOT is its absolute world cell
/// (floor(world / attr_voxel)) wrapped by the resolution, exactly the world-probe scheme. A
/// level re-snap then changes which cells are in the window, never which slot a surviving cell
/// lives in - the light radiance a cell accumulated stays addressed and stays valid. The level
/// origin is snapped to attribute-voxel multiples (see global_sdf_clipmap::update), so the
/// window base is exact integer cells.
ivec3 GiLightVoxelSlot(ivec3 cell)
{
	int res = u_light_voxel_resolution;
	ivec3 biased = cell + ivec3(1048576, 1048576, 1048576);
	return ivec3(biased.x % res, biased.y % res, biased.z % res);
}

/// Absolute world cell of a position at a level's attribute granularity.
ivec3 GiLightVoxelCell(vec3 world_position, float attr_voxel_size)
{
	return ivec3(floor(world_position / attr_voxel_size));
}

/// Packs an absolute cell + level for the attribute cell-id buffer: 10 bits per biased axis,
/// 2 bits of level (the world-probe trick; attribute cells within +/-512 of the origin at every
/// level's granularity, which the window sizes guarantee).
uint GiLightVoxelPackCell(ivec3 cell, int level)
{
	ivec3 biased = cell + ivec3(512, 512, 512);
	return uint(biased.x & 0x3FF) | (uint(biased.y & 0x3FF) << 10u) | (uint(biased.z & 0x3FF) << 20u) |
	       (uint(level) << 30u);
}

/// Unit direction of a cube face: face = axis * 2 + (negative).
vec3 GiLightVoxelFaceDirection(int face)
{
	int axis = face / 2;
	float face_sign = (face & 1) != 0 ? -1.0 : 1.0;
	vec3 direction = vec3_splat(0.0);
	if(axis == 0)
	{
		direction.x = face_sign;
	}
	else if(axis == 1)
	{
		direction.y = face_sign;
	}
	else
	{
		direction.z = face_sign;
	}
	return direction;
}

/// Texel of a voxel's face slab in the stacked volume.
ivec3 GiLightVoxelTexel(ivec3 voxel, int level, int face)
{
	return ivec3(voxel.x, voxel.y, ((level * 6 + face) * u_light_voxel_resolution) + voxel.z);
}

#if defined(GI_LIGHT_VOXEL_READ)

/// The light volume, for consumers. Include sdf_common.sh first (level lookup); the includer
/// owns keeping stage 10 free.
SAMPLER3D(s_light_voxels, 10);

/// One cascade level's light-voxel read. False when that level has no measured face here.
bool GiLightVoxelReadLevel(vec3 position, vec3 normal, int level, out vec3 out_radiance)
{
	out_radiance = vec3_splat(0.0);
	vec4 level_data = u_sdf_clipmap_levels[level];
	if(!(level_data.w > 0.0))
	{
		return false;
	}
	int res = u_light_voxel_resolution;
	float attr_voxel_size = level_data.w * 2.0;
	// TRILINEAR over the 2x2x2 cell neighbourhood: a nearest read hands neighbouring rays
	// voxel-quantised radiance, which the gather turns into probe-tile blotches around any
	// strong local emitter. The volume stores premultiplied measurements (rgb = 0 wherever
	// a = 0), so interpolating (rgb, a) and normalising by the filtered alpha afterwards is
	// the weight-correct mean over measured cells - empty neighbours cost weight, never
	// energy. Hardware REPEAT in xy lands the continuous cell coordinate on the toroidal
	// slot space (slot = cell mod res) including across the wrap seam; z is packed in face
	// slabs, so its two taps are fetched at texel centres and lerped manually. The
	// window-edge clamp keeps the neighbourhood inside this level's resident cells - past
	// it the wrap would answer with cells from the far side of the window. The level origin
	// is snapped to attribute-voxel multiples, so base_cell is exact.
	vec3 base_cell = floor(level_data.xyz / attr_voxel_size + vec3_splat(0.5));
	vec3 cell = clamp(position / attr_voxel_size,
	                  base_cell + vec3_splat(0.5),
	                  base_cell + vec3_splat(float(res) - 0.5));
	vec2 uv = cell.xy / float(res);
	float z_cell = cell.z - 0.5;
	float z_base = floor(z_cell);
	float z_frac = z_cell - z_base;
	int z_biased = int(z_base) + 1048576;
	int z0 = z_biased % res;
	int z1 = (z_biased + 1) % res;
	float depth_texels = float(res * SDF_CLIPMAP_LEVEL_COUNT * 6);
	vec3 radiance = vec3_splat(0.0);
	float weight_sum = 0.0;
	// Only the sign-matching face of each axis can face the normal (its facing weight is
	// |n[axis]|, the opposite face's is zero), so index the three candidates directly
	// instead of testing all six.
	for(int axis = 0; axis < 3; ++axis)
	{
		float component = axis == 0 ? normal.x : (axis == 1 ? normal.y : normal.z);
		float facing = abs(component);
		if(facing <= 0.0)
		{
			continue;
		}
		int face = axis * 2 + (component < 0.0 ? 1 : 0);
		int slab = (level * 6 + face) * res;
		vec4 s0 =
		    texture3DLod(s_light_voxels, vec3(uv, (float(slab + z0) + 0.5) / depth_texels), 0.0);
		vec4 s1 =
		    texture3DLod(s_light_voxels, vec3(uv, (float(slab + z1) + 0.5) / depth_texels), 0.0);
		vec4 filtered = mix(s0, s1, z_frac);
		radiance += filtered.xyz * facing;
		weight_sum += filtered.a * facing;
	}
	if(weight_sum <= 1e-4)
	{
		return false;
	}
	out_radiance = GiFiniteOrZero(radiance / weight_sum);
	return true;
}

/**
 * Outgoing radiance the light voxels hold at a surface point: three face slabs facing
 * @p normal, blended by facing x exposure and renormalised over what was measured.
 *
 * FALLS BACK one level at a time from the finest covering cascade. A traced hit follows the
 * cascade's BLENDED isosurface, which inside a cross-fade band sits up to a coarse voxel off
 * the finest level's own isosurface - so the finest level's surface band legitimately missed
 * that voxel while the next level's, twice as wide, covers it. Same reasoning (and the same
 * fix) as the old cache's level cross-fade: within a band the surface is represented at both
 * levels, and a reader that cannot step down goes dark in a camera-following shell - which is
 * exactly the distance fade the Attr Albedo debug view showed as yellow banding.
 *
 * Returns false only when NO level holds a measured voxel there - the caller decides its own
 * fallback (never fabricate energy here).
 */
bool GiLightVoxelRead(vec3 position, vec3 normal, out vec3 out_radiance)
{
	out_radiance = vec3_splat(0.0);
	float blend;
	float answered_voxel;
	int finest = SdfFindClipmapLevel(position, blend, answered_voxel);
	if(finest >= SDF_CLIPMAP_LEVEL_COUNT)
	{
		return false;
	}
	for(int level = finest; level < SDF_CLIPMAP_LEVEL_COUNT; ++level)
	{
		if(GiLightVoxelReadLevel(position, normal, level, out_radiance))
		{
			return true;
		}
	}
	return false;
}

/**
 * Reflection-safe cascade read: cross-fades the finer and coarser light voxels over
 * @p fade_voxels of the finer level. The gather's first-success walk is correct for
 * irradiance (a hole must not leak), but an IMAGE shows that walk as a knife-edge seam
 * and as dark spots the moment the finer occupancy misses the blended isosurface.
 *
 * When only the coarser level is measured, the result fades in from @p fallback rather
 * than slamming to the coarse voxel - that is the occupancy-hole pop.
 */
bool GiLightVoxelReadFade(vec3 position, vec3 normal, vec3 fallback, float fade_voxels,
                          out vec3 out_radiance)
{
	out_radiance = vec3_splat(0.0);
	float field_blend;
	float answered_voxel;
	int finest = SdfFindClipmapLevel(position, field_blend, answered_voxel);
	if(finest >= SDF_CLIPMAP_LEVEL_COUNT)
	{
		return false;
	}
	float fade = SdfClipmapEdgeBlend(finest, position, fade_voxels);
	fade = fade * fade * (3.0 - 2.0 * fade);
	vec3 fine_radiance;
	bool ok_fine = GiLightVoxelReadLevel(position, normal, finest, fine_radiance);
	vec3 coarse_radiance;
	bool ok_coarse = false;
	if(fade > 0.0 && (finest + 1) < SDF_CLIPMAP_LEVEL_COUNT)
	{
		ok_coarse = GiLightVoxelReadLevel(position, normal, finest + 1, coarse_radiance);
	}
	if(ok_fine && ok_coarse)
	{
		out_radiance = mix(fine_radiance, coarse_radiance, fade);
		return true;
	}
	if(ok_fine)
	{
		out_radiance = fine_radiance;
		return true;
	}
	if(ok_coarse)
	{
		out_radiance = mix(fallback, coarse_radiance, fade);
		return true;
	}
	for(int level = finest + 2; level < SDF_CLIPMAP_LEVEL_COUNT; ++level)
	{
		if(GiLightVoxelReadLevel(position, normal, level, out_radiance))
		{
			return true;
		}
	}
	return false;
}

#if defined(GI_LIGHT_VOXEL_READ_ALBEDO)

/// The attribute-albedo volume (cs_gi_clipmap_attributes.sc): rgb = winning instance albedo
/// (base colour factor x texture mean), a = 1 where surface - premultiplied like the light
/// volume, so trilinear (rgb, a) normalised by the filtered alpha is the weight-correct mean
/// over surface cells. Z stacks LEVEL slabs (no faces). The includer owns keeping stage 11
/// free; the CPU bind must override the texture's clamp flags with xy REPEAT (toroidal slots,
/// same contract as the light volume) and W clamp.
SAMPLER3D(s_gi_attr_albedo, 11);

/**
 * One cascade level's radiance AND matched-weight mean albedo, for the remodulation ratio.
 *
 * THE WEIGHT SETS MUST BE IDENTICAL. The ratio's correctness rests on
 * radiance_mean / albedo_mean cancelling to the (albedo-weighted) irradiance: with the same
 * weights, sum(w * a * albedo * E) / sum(w * a * albedo) is exact under locally uniform
 * lighting at EVERY material boundary. The previous split readers weighted radiance by
 * face-alpha x facing but albedo by plain cell-trilinear, and wherever face culling thinned
 * one set (crevices, rims - any silhouette a reflection ray grazes) the ratio over/undershot
 * to its clamp: a standing bright outline on the lighter material and a dark edging on the
 * darker one, stamped along every reflected junction - and a x4 amplifier window that kept
 * otherwise-invisible residual radiance (a departed emitter's tail) glowing as a line.
 *
 * Explicit 2x2x2 corner walk instead of hardware trilinear, because the albedo texels must be
 * weighted by the RADIANCE texels' face alphas, which live in a different texture. Corners
 * with zero trilinear weight are skipped (they may wrap toroidally; their weight is zero by
 * the window-edge clamp, same contract as the filtered readers).
 */
bool GiLightVoxelReadLevelRemod(vec3 position,
                                vec3 normal,
                                int level,
                                out vec3 out_radiance,
                                out vec3 out_albedo)
{
	out_radiance = vec3_splat(0.0);
	out_albedo = vec3_splat(0.0);
	vec4 level_data = u_sdf_clipmap_levels[level];
	if(!(level_data.w > 0.0))
	{
		return false;
	}
	int res = u_light_voxel_resolution;
	float attr_voxel_size = level_data.w * 2.0;
	vec3 base_cell = floor(level_data.xyz / attr_voxel_size + vec3_splat(0.5));
	vec3 cell = clamp(position / attr_voxel_size,
	                  base_cell + vec3_splat(0.5),
	                  base_cell + vec3_splat(float(res) - 0.5));
	vec3 corner_pos = cell - vec3_splat(0.5);
	vec3 corner_base = floor(corner_pos);
	vec3 frac = corner_pos - corner_base;
	ivec3 c0 = ivec3(corner_base);
	vec3 radiance_sum = vec3_splat(0.0);
	vec3 albedo_sum = vec3_splat(0.0);
	float weight_sum = 0.0;
	LOOP
	for(int corner = 0; corner < 8; ++corner)
	{
		ivec3 offset = ivec3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
		vec3 lerp_w = mix(vec3_splat(1.0) - frac, frac, vec3(offset));
		float w = lerp_w.x * lerp_w.y * lerp_w.z;
		if(w <= 1e-6)
		{
			continue;
		}
		ivec3 slot = GiLightVoxelSlot(c0 + offset);
		// Premultiplied by the cell's surface alpha; a face with alpha > 0 implies a listed
		// (surface) cell, so no divide is needed to recover the true albedo.
		vec3 cell_albedo =
		    texelFetch(s_gi_attr_albedo, ivec3(slot.x, slot.y, level * res + slot.z), 0).xyz;
		LOOP
		for(int axis = 0; axis < 3; ++axis)
		{
			float component = axis == 0 ? normal.x : (axis == 1 ? normal.y : normal.z);
			float facing = abs(component);
			if(facing <= 0.0)
			{
				continue;
			}
			int face = axis * 2 + (component < 0.0 ? 1 : 0);
			vec4 face_texel = texelFetch(s_light_voxels, GiLightVoxelTexel(slot, level, face), 0);
			float face_weight = w * facing;
			// Radiance texels are premultiplied (rgb = 0 wherever a = 0).
			radiance_sum += face_texel.xyz * face_weight;
			albedo_sum += cell_albedo * (face_texel.a * face_weight);
			weight_sum += face_texel.a * face_weight;
		}
	}
	if(weight_sum <= 1e-4)
	{
		return false;
	}
	out_radiance = GiFiniteOrZero(radiance_sum / weight_sum);
	out_albedo = albedo_sum / weight_sum;
	return true;
}

/**
 * GiLightVoxelReadFade's cascade walk with the matched-weight remodulation pair. KEEP THE
 * LEVEL LOGIC IN STEP with GiLightVoxelReadFade. @p out_albedo_valid is false exactly when
 * the radiance answer mixed in the caller's @p fallback (coarse-only inside the cross-fade
 * band): the fallback is not a lattice product, so no albedo can match it and the consumer
 * must skip the ratio rather than remodulate a partially foreign value.
 */
bool GiLightVoxelReadFadeRemod(vec3 position,
                               vec3 normal,
                               vec3 fallback,
                               float fade_voxels,
                               out vec3 out_radiance,
                               out vec3 out_albedo,
                               out bool out_albedo_valid)
{
	out_radiance = vec3_splat(0.0);
	out_albedo = vec3_splat(0.0);
	out_albedo_valid = false;
	float field_blend;
	float answered_voxel;
	int finest = SdfFindClipmapLevel(position, field_blend, answered_voxel);
	if(finest >= SDF_CLIPMAP_LEVEL_COUNT)
	{
		return false;
	}
	float fade = SdfClipmapEdgeBlend(finest, position, fade_voxels);
	fade = fade * fade * (3.0 - 2.0 * fade);
	vec3 fine_radiance;
	vec3 fine_albedo;
	bool ok_fine = GiLightVoxelReadLevelRemod(position, normal, finest, fine_radiance, fine_albedo);
	vec3 coarse_radiance;
	vec3 coarse_albedo;
	bool ok_coarse = false;
	if(fade > 0.0 && (finest + 1) < SDF_CLIPMAP_LEVEL_COUNT)
	{
		ok_coarse =
		    GiLightVoxelReadLevelRemod(position, normal, finest + 1, coarse_radiance, coarse_albedo);
	}
	if(ok_fine && ok_coarse)
	{
		out_radiance = mix(fine_radiance, coarse_radiance, fade);
		out_albedo = mix(fine_albedo, coarse_albedo, fade);
		out_albedo_valid = true;
		return true;
	}
	if(ok_fine)
	{
		out_radiance = fine_radiance;
		out_albedo = fine_albedo;
		out_albedo_valid = true;
		return true;
	}
	if(ok_coarse)
	{
		out_radiance = mix(fallback, coarse_radiance, fade);
		out_albedo = coarse_albedo;
		out_albedo_valid = false;
		return true;
	}
	for(int level = finest + 2; level < SDF_CLIPMAP_LEVEL_COUNT; ++level)
	{
		if(GiLightVoxelReadLevelRemod(position, normal, level, out_radiance, out_albedo))
		{
			out_albedo_valid = true;
			return true;
		}
	}
	return false;
}

#endif // GI_LIGHT_VOXEL_READ_ALBEDO

#endif // GI_LIGHT_VOXEL_READ

#endif // __GI_LIGHT_VOXELS_SH__
