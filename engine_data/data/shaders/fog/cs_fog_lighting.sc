/*
 * Lights each froxel of the volumetric fog (fog_pass): one jittered sample per froxel per frame, its medium from the
 * height fog's density (fog.sh), lit by the sun through its shadow cascades and the cloud shadow (Henyey-Greenstein
 * phase) and by the sky SH convolved with the same phase. The result is blended with the froxel's own history,
 * reprojected with last frame's camera, so the noise of the jitter and of the single shadow tap averages out
 * without TAA.
 *
 * The point and spot lights (u_fog_lights) are lit at the same sample and enter the same history: each froxel
 * converges to a light's average over its extent, so neither the inverse-squared peak at a light nor a cone or shadow
 * edge flickers as the grid moves with the camera. A light that changed since the previous frame shortens the history
 * of the froxels it reaches (FOG_CHANGED_LIGHT_HISTORY), so a moving light leaves no lasting trail. Each froxel visits
 * only the lights that reach its cluster (b_fog_light_masks, cs_fog_cull.sc); a light whose map sits in the shadow
 * atlas shadows the sample with one tap.
 *
 * Writes rgb = scattered radiance per metre (sigma_s x in-scattered light, pre-exposed), a = extinction (1/m).
 */

#include "../bgfx_compute.sh"
#include "../common.sh"

// This frame's lit froxels: what the integration reads, and next frame's history.
IMAGE3D_WO(i_fog_lit, rgba16f, 0);
// Last frame's lit froxels (this pass's i_fog_lit then), read filtered at the reprojected froxel centre.
SAMPLER3D(s_fog_history, 2);
// The view's environment SH (IRRADIANCE_SH): the sky, and in texel 9 the GPU's share of the pre-exposure.
SAMPLER2D(s_fog_environment, 3);
SAMPLER2D(s_cloudShadow, 4);
// The sun's shadow cascades (shadowmap_generator::submit_uniforms at stage 5).
SAMPLER2D(s_shadowMap0, 5);
SAMPLER2D(s_shadowMap1, 6);
SAMPLER2D(s_shadowMap2, 7);
SAMPLER2D(s_shadowMap3, 8);
// The fog volumes' noise: Perlin-Worley in R, tiling (cloud_noise_textures::base_noise).
SAMPLER3D(s_fog_noise, 9);
// The shadow atlas the point and spot lights' maps sit in (shadow_atlas): stored depth, point sampled.
SAMPLER2D(s_fog_shadow_atlas, 10);
// Per light of u_fog_lights, FOG_LIGHT_SHADOW_VEC4S vec4s (fog_pass.cpp pack_light_shadows): (the map's rect in the
// atlas, uv), (kind: 0 unshadowed, 1 spot, 2 point; 1 for a linear-depth map; half a texel of the map in its own uv; 0),
// then the world -> map matrix of each map by columns: the spot light's one, or the point light's tetrahedron faces.
BUFFER_RO(b_fog_light_shadows, vec4, 11);
// Per cluster of froxels, the lights that reach it (cs_fog_cull.sc).
BUFFER_RO(b_fog_light_masks, uint, 12);
uniform mat4 u_shadowMapMtx0;
uniform mat4 u_shadowMapMtx1;
uniform mat4 u_shadowMapMtx2;
uniform mat4 u_shadowMapMtx3;
/// y: stored shadow depth per world unit.
uniform vec4 u_shadowBiasParams;

#define PRE_EXPOSURE_ENVIRONMENT s_fog_environment
#include "../pre_exposure.sh"
#include "../cloud_shadow.sh"
#include "../light_attenuation.sh"
#include "fog.sh"
#include "fog_volume.sh"
#include "fog_lights.sh"

/// xyz = this frame's sample position inside each froxel (0..1), w = the history's weight (0 = no history).
uniform vec4 u_fog_jitter;
/// Last frame's camera-relative, unjittered view-projection.
uniform mat4 u_fog_prev_view_proj;
/// xyz = camera position - last frame's camera position, w = 1 when the render targets' row 0 is the bottom (GL).
uniform vec4 u_fog_prev_offset;
/// x = the sun's shadow cascades (0 = unshadowed), y = depth a linear-depth cascade stores beyond its receiver.
uniform vec4 u_fog_shadow;

/// Metres a froxel is pulled toward the sun before its shadow test (the medium has no surface to bias by).
#define FOG_SHADOW_BIAS 0.1
/// vec4s per light in b_fog_light_shadows, and where its matrices start.
#define FOG_LIGHT_SHADOW_VEC4S 18
#define FOG_LIGHT_SHADOW_MATRICES 2
/// Metres each local light's shadow tap is pulled toward the light.
#define FOG_LOCAL_SHADOW_BIAS 0.1
/// The most history a froxel keeps while a light that reaches it changes: follows the light within a few frames.
#define FOG_CHANGED_LIGHT_HISTORY 0.5

/// The sun's visibility through one cascade at @p position, one tap; -1 when the cascade does not cover it.
float fog_cascade_visibility(sampler2D shadow_map, mat4 shadow_matrix, vec3 position)
{
	vec4 coord = mul(shadow_matrix, vec4(position, 1.0));
	vec2 uv = coord.xy / coord.w;
	float depth = (coord.z + u_fog_shadow.y) / coord.w;
	if(any(lessThan(uv, vec2_splat(0.0))) || any(greaterThan(uv, vec2_splat(1.0))) || depth <= 0.0 || depth >= 1.0)
	{
		return -1.0;
	}
	float receiver = depth - FOG_SHADOW_BIAS * u_shadowBiasParams.y;
	return step(receiver, texture2DLod(shadow_map, uv, 0.0).x);
}

/// The sun's visibility at @p position: the nearest cascade that covers it (lit outside them all).
float fog_sun_visibility(vec3 position)
{
	float cascades = u_fog_shadow.x;
	float visibility = -1.0;
	if(cascades > 0.5)
	{
		visibility = fog_cascade_visibility(s_shadowMap0, u_shadowMapMtx0, position);
	}
	if(visibility < 0.0 && cascades > 1.5)
	{
		visibility = fog_cascade_visibility(s_shadowMap1, u_shadowMapMtx1, position);
	}
	if(visibility < 0.0 && cascades > 2.5)
	{
		visibility = fog_cascade_visibility(s_shadowMap2, u_shadowMapMtx2, position);
	}
	if(visibility < 0.0 && cascades > 3.5)
	{
		visibility = fog_cascade_visibility(s_shadowMap3, u_shadowMapMtx3, position);
	}
	return visibility < 0.0 ? 1.0 : visibility;
}

/**
 * The medium at the camera-relative @p offset: rgb = scattering coefficient (1/m) per channel, a = extinction (1/m).
 * The height fog plus the adding fog volumes (each with its own albedo and noise); the subtracting ones carve the
 * whole medium by their profile. Nothing before the start distance.
 */
vec4 fog_medium_at(vec3 offset)
{
	vec3 position = u_fog_camera.xyz + offset;
	if(length(offset) < u_fog_range.x)
	{
		return vec4_splat(0.0);
	}
	float extinction = fog_extinction_at(position.y);
	vec3 scattering = u_fog_albedo.rgb * extinction;
	float carve = 0.0;
	int count = int(u_fog_volume_info.x);
	for(int i = 0; i < FOG_MAX_VOLUMES; ++i)
	{
		if(i >= count)
		{
			break;
		}
		vec4 params = u_fog_volumes[i * FOG_VOLUME_VEC4S + 3];
		float profile = fog_volume_profile(params, fog_volume_unit_position(i, offset));
		if(profile <= 0.0)
		{
			continue;
		}
		if(fog_volume_subtracts(params))
		{
			carve = max(carve, profile);
			continue;
		}
		vec4 color = u_fog_volumes[i * FOG_VOLUME_VEC4S + 4];
		// Noise around 1 on average: twice the Perlin-Worley value, blended in by the noise strength.
		float noise = texture3DLod(s_fog_noise, position * params.w, 0.0).r * 2.0;
		float volume_extinction = params.x * profile * mix(1.0, noise, color.w);
		extinction += volume_extinction;
		scattering += color.rgb * volume_extinction;
	}
	return vec4(scattering, extinction) * (1.0 - carve);
}

/// The face of a point light's tetrahedron map that the light-relative @p offset falls in: 0 green, 1 yellow, 2 blue,
/// 3 red, picked as fs_pbr_lighting.sh picks it.
int fog_point_shadow_face(vec3 offset)
{
	vec4 selection = vec4(dot(vec3(0.0, -0.57735026, 0.81649661), offset),
	                      dot(vec3(0.0, -0.57735026, -0.81649661), offset),
	                      dot(vec3(-0.81649661, 0.57735026, 0.0), offset),
	                      dot(vec3(0.81649661, 0.57735026, 0.0), offset));
	float best = max(max(selection.x, selection.y), max(selection.z, selection.w));
	if(selection.y == best)
	{
		return 1;
	}
	if(selection.z == best)
	{
		return 2;
	}
	if(selection.w == best)
	{
		return 3;
	}
	return 0;
}

/**
 * One tap of the map of the light whose record starts at @p record, at the world @p position @p light_offset from the
 * light: 1 lit, 0 shadowed. The lighting pass's hardShadowLod without its receiver plane, which the medium has not.
 */
float fog_light_shadow_tap(int record, vec4 params, vec3 position, vec3 light_offset)
{
	int map = params.x > 1.5 ? fog_point_shadow_face(light_offset) : 0;
	int column = record + FOG_LIGHT_SHADOW_MATRICES + map * 4;
	vec4 coord = b_fog_light_shadows[column] * position.x + b_fog_light_shadows[column + 1] * position.y +
	             b_fog_light_shadows[column + 2] * position.z + b_fog_light_shadows[column + 3];
	if(coord.w <= 0.0)
	{
		return 1.0;
	}
	vec2 uv = coord.xy / coord.w;
	if(any(lessThan(uv, vec2_splat(0.0))) || any(greaterThan(uv, vec2_splat(1.0))))
	{
		return 1.0;
	}
	float receiver = params.y > 0.5 ? coord.z + 0.5 : coord.z / coord.w;
	vec4 rect = b_fog_light_shadows[record];
	vec2 atlas_uv = rect.xy + clamp(uv, vec2_splat(params.z), vec2_splat(1.0 - params.z)) * rect.zw;
	return step(receiver, texture2DLod(s_fog_shadow_atlas, atlas_uv, 0.0).x);
}

/**
 * Light @p light's visibility at the camera-relative @p offset: one tap of its map in the atlas, 1 for a light without
 * one. @p light_position is camera-relative.
 */
float fog_light_visibility(int light, vec3 light_position, vec3 offset)
{
	int record = light * FOG_LIGHT_SHADOW_VEC4S;
	vec4 params = b_fog_light_shadows[record + 1];
	if(params.x < 0.5)
	{
		return 1.0;
	}
	return fog_light_shadow_tap(record, params, u_fog_camera.xyz + offset, offset - light_position);
}

/**
 * The light the point and spot lights of the mask @p lights scatter toward the camera per unit of scattering
 * coefficient, at the camera-relative @p offset seen along the unit @p view_direction. Their inverse-squared falloff is
 * biased by @p bias_squared, the extent of the froxel the sample stands for.
 */
vec3 fog_local_inscatter(vec3 offset, vec3 view_direction, uint lights, float bias_squared)
{
	int count = int(u_fog_light_info.x);
	vec3 total = vec3_splat(0.0);
	for(int i = 0; i < FOG_MAX_LOCAL_LIGHTS; ++i)
	{
		if(i >= count)
		{
			break;
		}
		if(((lights >> uint(i)) & 1u) == 0u)
		{
			continue;
		}
		vec4 placement = u_fog_lights[i * FOG_LOCAL_LIGHT_VEC4S];
		vec4 cone = u_fog_lights[i * FOG_LOCAL_LIGHT_VEC4S + 1];
		vec4 intensity = u_fog_lights[i * FOG_LOCAL_LIGHT_VEC4S + 2];
		vec3 to_light = placement.xyz - offset;
		float distance_squared = dot(to_light, to_light);
		vec3 light_direction = to_light * inversesqrt(max(distance_squared, 1e-8));
		float mask = LocalLightRangeMask(to_light, placement.w) * LocalLightSpotMask(light_direction, cone.xyz, vec2(cone.w, intensity.w));
		if(mask <= 0.0)
		{
			continue;
		}
		mask *= fog_light_visibility(i, placement.xyz, offset + light_direction * FOG_LOCAL_SHADOW_BIAS);
		float phase = fog_phase_hg(dot(view_direction, light_direction), u_fog_range.w);
		total += intensity.rgb * (mask * phase / (distance_squared + bias_squared));
	}
	return total;
}

/**
 * The medium and the light it scatters toward the camera at the camera-relative @p offset: the sun and the sky, and the
 * point and spot lights of the mask @p lights with their falloff biased by @p bias_squared.
 */
vec4 fog_light_froxel(vec3 offset, uint lights, float bias_squared)
{
	vec4 medium = fog_medium_at(offset);
	if(medium.a <= 0.0)
	{
		return vec4_splat(0.0);
	}
	vec3 view_direction = offset / max(length(offset), 1e-6);
	vec3 position = u_fog_camera.xyz + offset;
	vec3 sun_direction = u_fog_sun_direction.xyz;
	float sun_visibility = fog_sun_visibility(position) * CloudShadow(position, sun_direction);
	float sun_phase = fog_phase_hg(dot(view_direction, sun_direction), u_fog_range.w);
	vec3 light = u_fog_light.rgb * (sun_phase * sun_visibility) + u_fog_light.w * fog_sky_inscatter(s_fog_environment, view_direction);
	if(lights != 0u)
	{
		light += fog_local_inscatter(offset, view_direction, lights, bias_squared);
	}
	return vec4(medium.rgb * light * u_pre_exposure_value, medium.a);
}

/// The lights of u_fog_lights that changed since the previous frame (fog_pass::find_changed_lights), one bit each.
uint fog_changed_lights()
{
	return uint(u_fog_light_info.y) | (uint(u_fog_light_info.z) << 16u);
}

/// The froxel at the camera-relative @p offset in last frame's grid; outside 0..1 when it was not in view.
vec3 fog_previous_coordinate(vec3 offset)
{
	vec4 previous_clip = mul(u_fog_prev_view_proj, vec4(offset + u_fog_prev_offset.xyz, 1.0));
	if(previous_clip.w <= 1e-4)
	{
		return vec3_splat(-1.0);
	}
	vec2 ndc = previous_clip.xy / previous_clip.w;
	float row = u_fog_prev_offset.w > 0.5 ? ndc.y * 0.5 + 0.5 : 0.5 - ndc.y * 0.5;
	return vec3(ndc.x * 0.5 + 0.5, row, fog_slice_coordinate(previous_clip.w));
}

NUM_THREADS(4, 4, 4)
void main()
{
	ivec3 cell = ivec3(gl_GlobalInvocationID.xyz);
	vec3 grid = u_fog_grid.xyz;
	if(float(cell.x) >= grid.x || float(cell.y) >= grid.y || float(cell.z) >= grid.z)
	{
		return;
	}
	// The lights that reach the froxel's cluster.
	uint lights = 0u;
	if(u_fog_light_info.x > 0.5)
	{
		lights = b_fog_light_masks[fog_cluster_index(fog_cell_cluster(cell))];
	}
	vec3 sample_coord = (vec3(cell) + u_fog_jitter.xyz) / grid;
	vec3 sample_offset = fog_grid_ray(sample_coord.xy) * fog_slice_depth(sample_coord.z);
	// A light's falloff is biased by the froxel's diagonal, depth included (as Unreal's volumetric fog does): the
	// froxels are far deeper than wide, and a sample that lands next to a light must not spike its froxel.
	float bias_squared = LOCAL_LIGHT_DISTANCE_BIAS_SQR;
	if(lights != 0u)
	{
		vec3 next_coord = sample_coord + 1.0 / grid;
		vec3 diagonal = fog_grid_ray(next_coord.xy) * fog_slice_depth(next_coord.z) - sample_offset;
		bias_squared = max(dot(diagonal, diagonal), LOCAL_LIGHT_DISTANCE_BIAS_SQR);
	}
	vec4 lit = fog_light_froxel(sample_offset, lights, bias_squared);
	vec3 center_coord = (vec3(cell) + 0.5) / grid;
	vec3 center = fog_grid_ray(center_coord.xy) * fog_slice_depth(center_coord.z);
	float history_weight = u_fog_jitter.w;
	// A light that changed since the previous frame (moved, turned, dimmed) left a stale history where it reaches: a
	// shorter one follows it closely, with a little more noise while it changes.
	if((lights & fog_changed_lights()) != 0u)
	{
		history_weight = min(history_weight, FOG_CHANGED_LIGHT_HISTORY);
	}
	if(history_weight > 0.0)
	{
		vec3 previous = fog_previous_coordinate(center);
		if(all(greaterThanEqual(previous, vec3_splat(0.0))) && all(lessThanEqual(previous, vec3_splat(1.0))))
		{
			vec4 history = texture3DLod(s_fog_history, previous, 0.0);
			history.rgb *= u_history_pre_exposure_correction;
			// NaN and Inf fail the comparison: a bad texel never propagates.
			if(all(lessThan(abs(history), vec4_splat(FOG_HALF_MAX))))
			{
				lit = mix(lit, history, history_weight);
			}
		}
	}
	lit = min(lit, vec4_splat(FOG_HALF_MAX));
	imageStore(i_fog_lit, cell, lit);
}
