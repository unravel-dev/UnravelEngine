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

/// x = attribute resolution (voxels per axis), y = attribute voxel size of level 0 (doubles per
/// level), z = frame index, w = non-zero when the light volume is resident.
uniform vec4 u_gi_light_voxel_params;
#define u_light_voxel_resolution int(u_gi_light_voxel_params.x)
#define u_light_voxel_base_size  u_gi_light_voxel_params.y
#define u_light_voxel_frame      uint(u_gi_light_voxel_params.z)
#define u_light_voxel_ready      (u_gi_light_voxel_params.w > 0.0)

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
	int res = u_light_voxel_resolution;
	float depth_texels = float(res * SDF_CLIPMAP_LEVEL_COUNT * 6);
	for(int level = finest; level < SDF_CLIPMAP_LEVEL_COUNT; ++level)
	{
		vec4 level_data = u_sdf_clipmap_levels[level];
		if(!(level_data.w > 0.0))
		{
			continue;
		}
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
		if(weight_sum > 1e-4)
		{
			out_radiance = GiFiniteOrZero(radiance / weight_sum);
			return true;
		}
	}
	return false;
}

#endif // GI_LIGHT_VOXEL_READ

#endif // __GI_LIGHT_VOXELS_SH__
