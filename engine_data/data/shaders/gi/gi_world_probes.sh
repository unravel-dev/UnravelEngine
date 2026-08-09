#ifndef __GI_WORLD_PROBES_SH__
#define __GI_WORLD_PROBES_SH__

/*
 * World probe cascades (GI v2 plan 3.3, revised): octahedral radiance/irradiance probes on a
 * TOROIDAL world-anchored lattice per SDF cascade. They carry offscreen and distant energy,
 * complete shortened gather rays, and feed the light voxels' bounce term.
 *
 * LATTICE. Probes live on an absolute world grid of spacing = cascade voxel *
 * GI_WORLD_PROBE_DIVISOR (2 m at level 0). The window covers GI_WORLD_PROBE_AXIS^3 cells around
 * the cascade; a probe's storage slot is its world cell index mod GI_WORLD_PROBE_AXIS, so
 * camera motion never moves or copies a probe - cells enter and leave the window, and a slot
 * whose cell changed is detected by the cell-id buffer and refilled. Camera ROTATION touches
 * nothing (R1 by construction).
 *
 * UPDATE. Every probe, every frame, GI_WORLD_PROBE_RAYS_PER_FRAME rays at FIXED octahedral
 * texel centres: stratum s = frame mod GI_WORLD_PROBE_WINDOW covers texels where
 * (texel_index mod WINDOW) == s, so every direction refreshes exactly once per window and the
 * radiance atlas is a zero-variance windowed mean. Rays read the light voxels at hits and the
 * sky SH at miss.
 *
 * ATLASES (2D, cascades stacked vertically):
 *  - radiance: GI_WORLD_PROBE_AXIS^2 probe tiles of OCT_RADIANCE^2 texels, probe-major
 *    (tile x = slot.x + slot.z * AXIS, tile y = slot.y + level * AXIS). rgb radiance, a hitT
 *    (negative = miss/sky). Point-fetched only, so no gutter.
 *  - irradiance: same tile grid at (OCT_IRRADIANCE+2)^2 texels - 1-texel octahedral gutter for
 *    hardware bilinear. rgb = E/pi at the texel's normal direction, a = sky fraction.
 *  - depth: same gutter layout, RG16F = (mean, mean^2) of hitT under a
 *    cos^GI_WORLD_PROBE_DEPTH_SHARPNESS lobe, for the Chebyshev visibility test.
 */

#include "gi_constants.sh"
#include "gi_probe_common.sh"

/// Probes per axis of one cascade's window: cascade resolution / divisor + 1 (lattice includes
/// both endpoints). Runtime resolution 128 => 9.
#define GI_WORLD_PROBE_AXIS 9

/// x = probe spacing of level 0 in world units (doubles per level), y = frame index,
/// z = non-zero when the probe atlases are resident, w reserved.
uniform vec4 u_gi_world_probe_params;
#define u_world_probe_base_spacing u_gi_world_probe_params.x
#define u_world_probe_frame        uint(u_gi_world_probe_params.y)
#define u_world_probe_ready        (u_gi_world_probe_params.z > 0.0)

float GiWorldProbeSpacing(int level)
{
	return u_world_probe_base_spacing * float(1 << level);
}

/// Wraps a world cell index onto its storage slot.
ivec3 GiWorldProbeSlot(ivec3 cell)
{
	// True mathematical modulo for negative cells; HLSL/GLSL % is implementation-inconvenient
	// on negatives, so bias well into positives first (cells are bounded far below 1<<20).
	ivec3 biased = cell + ivec3(1048576, 1048576, 1048576);
	return ivec3(biased.x % GI_WORLD_PROBE_AXIS,
	             biased.y % GI_WORLD_PROBE_AXIS,
	             biased.z % GI_WORLD_PROBE_AXIS);
}

/// Top-left texel of a probe slot's tile in an atlas with @p tile_edge texels per tile.
ivec2 GiWorldProbeTileBase(ivec3 slot, int level, int tile_edge)
{
	return ivec2((slot.x + slot.z * GI_WORLD_PROBE_AXIS) * tile_edge,
	             (slot.y + level * GI_WORLD_PROBE_AXIS) * tile_edge);
}

/// World position of a probe cell's lattice point.
vec3 GiWorldProbeCellPosition(ivec3 cell, int level)
{
	return vec3(cell) * GiWorldProbeSpacing(level);
}

/// Linear index of a probe slot within one cascade, for the cell-id buffer.
int GiWorldProbeSlotIndex(ivec3 slot, int level)
{
	return ((level * GI_WORLD_PROBE_AXIS + slot.z) * GI_WORLD_PROBE_AXIS + slot.y) * GI_WORLD_PROBE_AXIS +
	       slot.x;
}

/// Packs a world cell for the cell-id buffer. 10 bits per axis (biased), 2 bits of level - the
/// same one-word identity trick the surface list uses.
uint GiWorldProbePackCell(ivec3 cell, int level)
{
	ivec3 biased = cell + ivec3(512, 512, 512);
	return uint(biased.x & 0x3FF) | (uint(biased.y & 0x3FF) << 10u) | (uint(biased.z & 0x3FF) << 20u) |
	       (uint(level) << 30u);
}

#if defined(GI_WORLD_PROBE_READ)

/// Irradiance + depth atlases for consumers. RESERVED STAGES 14 is the env SH; these use 11/15
/// unless the includer overrides beforehand. A consumer that only COMPLETES rays (radiance +
/// depth, never the irradiance cage) defines GI_WORLD_PROBE_SKIP_IRRADIANCE to leave stage 11
/// free - the screen probe trace hands that stage to its compacted probe list.
#ifndef GI_WORLD_PROBE_SKIP_IRRADIANCE
SAMPLER2D(s_world_probe_irradiance, 11);
#endif // GI_WORLD_PROBE_SKIP_IRRADIANCE
SAMPLER2D(s_world_probe_depth, 15);

/// xy = 1 / atlas size of the irradiance+depth atlases (they share a layout).
uniform vec4 u_gi_world_probe_atlas;

#ifndef GI_WORLD_PROBE_SKIP_IRRADIANCE
/**
 * Irradiance around @p normal at @p position from the 8-probe cage of @p level, with the full
 * DDGI weight chain: trilinear x wrap-shading backface x Chebyshev visibility (cubed, floored)
 * x perception crush, evaluated at the self-shadow-biased point. Returns false when the level's
 * window does not cover the position or every weight died.
 *
 * All the constants are the published DDGI/RTXGI values, owned by gi_constants
 * (tasks/research/research_probe_systems.md section 1.3 quotes the chain verbatim).
 */
bool GiWorldProbeIrradiance(vec3 position, vec3 normal, vec3 view_direction, int level,
                            out vec3 out_irradiance, out float out_sky_fraction)
{
	out_irradiance = vec3_splat(0.0);
	out_sky_fraction = 0.0;
	float spacing = GiWorldProbeSpacing(level);
	// Self-shadow bias [DDGI21 Eq.2]: move the query toward the surface's clear side before any
	// visibility test. CAPPED at field-voxel scale: the spacing-proportional magnitude DDGI
	// publishes (0.225 x spacing = 0.45 m at the 2 m lattice) TUNNELS THROUGH any wall thinner
	// than it - the biased point lands outside, Chebyshev sees the exterior probe unoccluded,
	// and a sunlit exterior floods a closed room (measured on the thick-walled test room; the
	// documented DDGI thin-wall failure). Clearing the query surface's own field shadow is a
	// VOXEL-scale need, so two voxels of the level's field is enough - and stays below any
	// wall the field itself can resolve.
	float bias_magnitude = min(GI_SELF_SHADOW_BIAS_SCALE * GI_SELF_SHADOW_BIAS_K * spacing,
	                           GI_SELF_SHADOW_BIAS_MAX_VOXELS * spacing / float(GI_WORLD_PROBE_DIVISOR));
	vec3 biased = position + (normal * GI_SELF_SHADOW_BIAS_NORMAL + view_direction * GI_SELF_SHADOW_BIAS_VIEW) *
	                             bias_magnitude;
	vec3 grid = biased / spacing;
	ivec3 base_cell = ivec3(floor(grid));
	vec3 frac = grid - vec3(base_cell);
	int tile_edge = GI_WORLD_PROBE_OCT_IRRADIANCE + 2;
	vec2 oct_uv = GiOctEncode(normal);
	vec3 sum = vec3_splat(0.0);
	float sky_sum = 0.0;
	float weight_sum = 0.0;
	for(int corner = 0; corner < 8; ++corner)
	{
		ivec3 offset = ivec3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
		ivec3 cell = base_cell + offset;
		vec3 probe_position = GiWorldProbeCellPosition(cell, level);
		// Trilinear, floored so a query exactly on a probe plane cannot zero the whole cage.
		vec3 tri = mix(vec3_splat(1.0) - frac, frac, vec3(offset));
		float weight = max(tri.x, 0.001) * max(tri.y, 0.001) * max(tri.z, 0.001);
		// Wrap-shading backface term [RTXGI]: soft, so detail geometry does not reject the
		// whole cage.
		vec3 to_probe = probe_position - position;
		float to_probe_length = max(length(to_probe), 1e-4);
		vec3 dir_to_probe = to_probe / to_probe_length;
		float wrap = (dot(dir_to_probe, normal) + 1.0) * 0.5;
		weight *= wrap * wrap + 0.2;
		// Chebyshev visibility from the depth moments, tested from the BIASED point.
		vec3 biased_to_probe = probe_position - biased;
		float distance_to_probe = max(length(biased_to_probe), 1e-4);
		ivec3 slot = GiWorldProbeSlot(cell);
		ivec2 tile = GiWorldProbeTileBase(slot, level, tile_edge);
		vec2 depth_oct = GiOctEncode(-biased_to_probe / distance_to_probe);
		vec2 depth_uv =
		    (vec2(tile) + vec2_splat(1.0) + depth_oct * float(GI_WORLD_PROBE_OCT_DEPTH)) *
		    u_gi_world_probe_atlas.xy;
		vec2 moments = texture2DLod(s_world_probe_depth, depth_uv, 0.0).xy;
		if(distance_to_probe > moments.x)
		{
			float variance = abs(moments.y - moments.x * moments.x);
			float difference = distance_to_probe - moments.x;
			float chebyshev = variance / (variance + difference * difference);
			chebyshev = chebyshev * chebyshev * chebyshev;
			weight *= max(chebyshev, GI_CHEBYSHEV_WEIGHT_FLOOR);
		}
		// Perception crush [DDGI19]: fade dim contributions faster than linear, which is what
		// keeps barely-weighted leaks below visibility.
		weight = max(weight, 1e-6);
		if(weight < GI_PERCEPTION_CRUSH_THRESHOLD)
		{
			weight *= (weight * weight) / (GI_PERCEPTION_CRUSH_THRESHOLD * GI_PERCEPTION_CRUSH_THRESHOLD);
		}
		vec2 irradiance_uv =
		    (vec2(tile) + vec2_splat(1.0) + oct_uv * float(GI_WORLD_PROBE_OCT_IRRADIANCE)) *
		    u_gi_world_probe_atlas.xy;
		vec4 texel = texture2DLod(s_world_probe_irradiance, irradiance_uv, 0.0);
		sum += texel.xyz * weight;
		sky_sum += texel.w * weight;
		weight_sum += weight;
	}
	if(weight_sum <= 1e-5)
	{
		return false;
	}
	out_irradiance = sum / weight_sum;
	out_sky_fraction = sky_sum / weight_sum;
	return true;
}

/**
 * As @ref GiWorldProbeIrradiance, choosing the finest level whose window covers the position
 * and cross-fading into the next over the outermost probe cell - the DDGI 2021 cascade blend,
 * tightened by one cell so a scrolled plane cannot pop.
 */
bool GiWorldProbeIrradianceCascade(vec3 position, vec3 normal, vec3 view_direction,
                                   vec3 window_center,
                                   out vec3 out_irradiance, out float out_sky_fraction)
{
	for(int level = 0; level < SDF_CLIPMAP_LEVEL_COUNT; ++level)
	{
		float spacing = GiWorldProbeSpacing(level);
		// The window covers AXIS cells centred on the camera's cell; usable extent excludes the
		// outermost cell on each side (the blend band, and the cells that may be mid-refill).
		float half_extent = (float(GI_WORLD_PROBE_AXIS - 1) * 0.5 - 1.0) * spacing;
		vec3 delta = abs(position - window_center);
		float largest = max(delta.x, max(delta.y, delta.z));
		if(largest > half_extent)
		{
			continue;
		}
		vec3 near_irradiance;
		float near_sky;
		if(!GiWorldProbeIrradiance(position, normal, view_direction, level, near_irradiance, near_sky))
		{
			continue;
		}
		// Blend toward the next level over the outer half of the last usable cell.
		float band = 0.5 * spacing;
		float blend = saturate((largest - (half_extent - band)) / band);
		if(blend > 0.0 && level + 1 < SDF_CLIPMAP_LEVEL_COUNT)
		{
			vec3 far_irradiance;
			float far_sky;
			if(GiWorldProbeIrradiance(position, normal, view_direction, level + 1, far_irradiance, far_sky))
			{
				near_irradiance = mix(near_irradiance, far_irradiance, blend);
				near_sky = mix(near_sky, far_sky, blend);
			}
		}
		out_irradiance = near_irradiance;
		out_sky_fraction = near_sky;
		return true;
	}
	out_irradiance = vec3_splat(0.0);
	out_sky_fraction = 0.0;
	return false;
}
#endif // GI_WORLD_PROBE_SKIP_IRRADIANCE

#ifdef GI_WORLD_PROBE_READ_RADIANCE

/// The raw radiance atlas, for ray COMPLETION - a consumer that reads it binds stage 6.
SAMPLER2D(s_world_probe_radiance_read, 6);

/// xy = 1 / radiance atlas size.
uniform vec4 u_gi_world_probe_radiance_atlas;

/**
 * Radiance arriving from @p direction at @p position, read from the probe cage of the finest
 * covering cascade - what a SHORTENED gather ray completes into after establishing its own
 * near-field visibility [S21 s69-74].
 *
 * Each cage probe is read at its SPHERE-PARALLAX-corrected direction [S21 s73]: the query ray
 * is intersected with a sphere of the probe's stored hit distance around the probe, and the
 * texel toward that intersection is read - removing the positional gap between the ray's origin
 * and the probe's at the price of directional error, which is the trade Lumen ships. Cage
 * weights are trilinear x Chebyshev (no facing terms: radiance has no receiver normal).
 */
bool GiWorldProbeRadiance(vec3 position, vec3 direction, vec3 window_center, out vec3 out_radiance)
{
	out_radiance = vec3_splat(0.0);
	for(int level = 0; level < SDF_CLIPMAP_LEVEL_COUNT; ++level)
	{
		float spacing = GiWorldProbeSpacing(level);
		float half_extent = (float(GI_WORLD_PROBE_AXIS - 1) * 0.5 - 1.0) * spacing;
		vec3 delta = abs(position - window_center);
		if(max(delta.x, max(delta.y, delta.z)) > half_extent)
		{
			continue;
		}
		vec3 grid = position / spacing;
		ivec3 base_cell = ivec3(floor(grid));
		vec3 frac = grid - vec3(base_cell);
		vec3 sum = vec3_splat(0.0);
		float weight_sum = 0.0;
		for(int corner = 0; corner < 8; ++corner)
		{
			ivec3 offset = ivec3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
			ivec3 cell = base_cell + offset;
			vec3 probe_position = GiWorldProbeCellPosition(cell, level);
			vec3 tri = mix(vec3_splat(1.0) - frac, frac, vec3(offset));
			float weight = max(tri.x, 0.001) * max(tri.y, 0.001) * max(tri.z, 0.001);
			ivec3 slot = GiWorldProbeSlot(cell);
			// Chebyshev visibility of the QUERY POINT from the probe, exactly as the
			// irradiance read tests it - a probe behind a wall must not complete rays through
			// it.
			vec3 to_query = position - probe_position;
			float query_distance = max(length(to_query), 1e-4);
			ivec2 depth_tile = GiWorldProbeTileBase(slot, level, GI_WORLD_PROBE_OCT_IRRADIANCE + 2);
			vec2 depth_uv = (vec2(depth_tile) + vec2_splat(1.0) +
			                 GiOctEncode(to_query / query_distance) * float(GI_WORLD_PROBE_OCT_DEPTH)) *
			                u_gi_world_probe_atlas.xy;
			vec2 moments = texture2DLod(s_world_probe_depth, depth_uv, 0.0).xy;
			if(query_distance > moments.x)
			{
				float variance = abs(moments.y - moments.x * moments.x);
				float difference = query_distance - moments.x;
				float chebyshev = variance / (variance + difference * difference);
				chebyshev = chebyshev * chebyshev * chebyshev;
				weight *= max(chebyshev, GI_CHEBYSHEV_WEIGHT_FLOOR);
			}
			if(weight <= 1e-5)
			{
				continue;
			}
			// Sphere parallax: read toward where the ray meets the probe's visibility sphere.
			// The stored mean depth toward the RAY direction is the sphere radius estimate.
			ivec2 tile = GiWorldProbeTileBase(slot, level, GI_WORLD_PROBE_OCT_RADIANCE);
			vec2 radius_uv = (vec2(depth_tile) + vec2_splat(1.0) +
			                  GiOctEncode(direction) * float(GI_WORLD_PROBE_OCT_DEPTH)) *
			                 u_gi_world_probe_atlas.xy;
			float radius = max(texture2DLod(s_world_probe_depth, radius_uv, 0.0).x, 0.25 * spacing);
			vec3 corrected = normalize(position + direction * radius - probe_position);
			vec2 radiance_uv = (vec2(tile) + (GiOctEncode(corrected) * float(GI_WORLD_PROBE_OCT_RADIANCE))) *
			                   u_gi_world_probe_radiance_atlas.xy;
			// Manual clamp inside the tile: the radiance atlas has no gutter, and a bilinear tap
			// crossing into a neighbour probe's tile would mix unrelated probes.
			vec2 tile_min = (vec2(tile) + vec2_splat(0.5)) * u_gi_world_probe_radiance_atlas.xy;
			vec2 tile_max = (vec2(tile) + vec2_splat(float(GI_WORLD_PROBE_OCT_RADIANCE) - 0.5)) *
			                u_gi_world_probe_radiance_atlas.xy;
			radiance_uv = clamp(radiance_uv, tile_min, tile_max);
			vec3 sample_radiance = texture2DLod(s_world_probe_radiance_read, radiance_uv, 0.0).xyz;
			sum += sample_radiance * weight;
			weight_sum += weight;
		}
		if(weight_sum <= 1e-5)
		{
			return false;
		}
		out_radiance = sum / weight_sum;
		return true;
	}
	return false;
}

#endif // GI_WORLD_PROBE_READ_RADIANCE

#endif // GI_WORLD_PROBE_READ

#endif // __GI_WORLD_PROBES_SH__
