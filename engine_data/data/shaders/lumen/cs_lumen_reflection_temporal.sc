/*
 * Lumen reflections, temporal accumulation (UE 5.8 LumenReflectionDenoiserTemporalCS,
 * LumenReflectionDenoiserTemporal.usf:132-585), full resolution.
 *
 * Two histories compete for each resolved pixel, both read with 2x2 bilinear taps that skip history texels the
 * pass left invalid:
 *  - the reflection-hit history (narrow lobes only, LumenSpecularDominantDirFactor > 0.5): the virtual image
 *    of the hit, placed the resolved hit distance behind the surface along the view ray and reprojected - a
 *    planar mirror's image follows the camera exactly; no depth test;
 *  - the surface history: the surface point reprojected, each tap kept only when last frame's depth there is
 *    within LUMEN_REFLECTION_TEMPORAL_DISTANCE_THRESHOLD x U(0.5, 1.5) of the reprojected depth, relaxed by
 *    1 / clamp(NoV, 0.1, 1) against TAA jitter at grazing angles.
 * The one closer to the mean of the 5x5 neighbourhood (corners skipped) of this frame's resolve wins, keeping
 * 20% of the hit history; both are clamped to the neighbourhood mean +- LUMEN_REFLECTION_NEIGHBORHOOD_CLAMP_SCALE
 * standard deviations in YCoCg, and the clamp's distance lowers the confidence, which shortens the frame count:
 * N = min(N x (0.75 confidence + 0.25) + 1, max), max = LUMEN_REFLECTION_TEMPORAL_MAX_FRAMES (mirrors
 * LUMEN_REFLECTION_TEMPORAL_MIRROR_FRAMES, ramping in to roughness 0.05). Everything averages in the denoiser
 * space; the second moment of the luminance accumulates alongside for the spatial filter.
 *
 * Writes rgb = the accumulated radiance (pre-exposed), a = its luminance second moment, and the frame count
 * (-1 marks a pixel without reflections, which later reads treat as invalid history).
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "../pre_exposure.sh"
#include "lumen/lumen_reflection_common.sh"

IMAGE2D_WO(s_lumen_reflection_history_out, rgba16f, 0);
IMAGE2D_WO(s_lumen_reflection_frames_out, r32f, 1);
/// This frame's resolve: rgb = radiance, a = the hit distance, -1 = no reflection.
SAMPLER2D(s_lumen_reflection_resolved, 8);
SAMPLER2D(s_lumen_reflection_history, 9);
SAMPLER2D(s_lumen_reflection_frames_history, 10);
/// Last frame's device depth.
SAMPLER2D(s_lumen_prev_depth, 11);
SAMPLER2D(s_lumen_depth, 12);
/// G-buffer target 1: octahedral normal, metalness, roughness.
SAMPLER2D(s_lumen_normal, 13);

/// Last frame's TAA-unjittered view projection.
uniform mat4 u_lumen_prev_view_proj;

#define LUMEN_REFLECTION_TILE_BORDER 2
#define LUMEN_REFLECTION_TILE_SIZE 12

/// The resolve of the group's 8x8 pixels and a 2 pixel border: xyz = YCoCg in the denoiser space, w = 1 when
/// valid.
SHARED vec4 s_lumen_reflection_tile[LUMEN_REFLECTION_TILE_SIZE * LUMEN_REFLECTION_TILE_SIZE];

struct LumenReflectionHistory
{
	bool valid;
	/// Denoiser space, this frame's pre-exposure.
	vec3 specular;
	float second_moment;
	float frames;
};

/// Last frame's accumulation at the reprojection of @p world_point (UE GetLightingHistory).
LumenReflectionHistory LumenReadReflectionHistory(vec3 world_point, bool depth_test, vec3 position, vec3 normal,
                                                  float noise)
{
	LumenReflectionHistory history;
	history.valid = false;
	history.specular = vec3_splat(0.0);
	history.second_moment = 0.0;
	history.frames = 0.0;
	vec4 prev_clip = mul(u_lumen_prev_view_proj, vec4(world_point, 1.0));
	if(prev_clip.w <= 0.0)
	{
		return history;
	}
	vec2 history_uv = clipToUv((prev_clip.xy / prev_clip.w) * 0.5 + 0.5);
	if(any(lessThan(history_uv, vec2_splat(0.0))) || any(greaterThan(history_uv, vec2_splat(1.0))))
	{
		return history;
	}
	vec2 history_size = vec2(textureSize(s_lumen_reflection_history, 0));
	// Inset by 0.51 texel: the bilinear footprint stays inside the view (UE HistoryGatherUVMinMax).
	vec2 guard = vec2_splat(0.51) / history_size;
	history_uv = clamp(history_uv, guard, vec2_splat(1.0) - guard);
	vec2 coord = history_uv * history_size - 0.5;
	ivec2 origin = ivec2(floor(coord));
	vec2 f = coord - floor(coord);
	vec4 weights = vec4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);
	float reprojected_depth = prev_clip.w;
	vec3 view = normalize(LumenReflectionCamera() - position);
	float threshold = LUMEN_REFLECTION_TEMPORAL_DISTANCE_THRESHOLD * mix(0.5, 1.5, noise) /
	                  clamp(saturate(dot(view, normal)), 0.1, 1.0);
	vec4 specular_sum = vec4_splat(0.0);
	float frames_sum = 0.0;
	float weight_sum = 0.0;
	for(int tap = 0; tap < 4; ++tap)
	{
		ivec2 texel = clamp(origin + ivec2(tap & 1, tap >> 1), ivec2(0, 0), ivec2(history_size) - ivec2(1, 1));
		float weight = tap == 0 ? weights.x : (tap == 1 ? weights.y : (tap == 2 ? weights.z : weights.w));
		if(depth_test)
		{
			float tap_depth = LumenLinearDepth(texelFetch(s_lumen_prev_depth, texel, 0).x);
			weight = abs(tap_depth - reprojected_depth) >= reprojected_depth * threshold ? 0.0 : weight;
		}
		float tap_frames = texelFetch(s_lumen_reflection_frames_history, texel, 0).x;
		weight = tap_frames < 0.0 ? 0.0 : weight;
		if(weight > 0.0)
		{
			vec4 tap_value = texelFetch(s_lumen_reflection_history, texel, 0);
			specular_sum += weight * vec4(LumenReflectionToDenoiserSpace(tap_value.xyz), tap_value.w);
			frames_sum += weight * tap_frames;
			weight_sum += weight;
		}
		history.valid = history.valid || weight > 0.01;
	}
	if(!history.valid)
	{
		return history;
	}
	specular_sum /= weight_sum;
	float correction = u_history_pre_exposure_correction;
	history.specular = specular_sum.xyz * correction;
	history.second_moment = specular_sum.w * correction * correction;
	history.frames = frames_sum / weight_sum;
	return history;
}

/// One tile entry: this frame's resolve at @p coord in the denoiser space, as YCoCg (w = 1 when valid).
vec4 LumenLoadTileEntry(ivec2 coord)
{
	if(any(lessThan(coord, ivec2(0, 0))) || any(greaterThanEqual(coord, ivec2(u_lumen_view_size))))
	{
		return vec4_splat(0.0);
	}
	vec4 resolved = texelFetch(s_lumen_reflection_resolved, coord, 0);
	if(resolved.w < 0.0)
	{
		return vec4_splat(0.0);
	}
	return vec4(LumenRGBToYCoCg(LumenReflectionToDenoiserSpace(resolved.xyz)), 1.0);
}

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 group_origin = ivec2(gl_WorkGroupID.xy) * 8;
	ivec2 local = ivec2(gl_LocalInvocationID.xy);
	for(int y = local.y; y < LUMEN_REFLECTION_TILE_SIZE; y += 8)
	{
		for(int x = local.x; x < LUMEN_REFLECTION_TILE_SIZE; x += 8)
		{
			bool corner = (x == 0 || x == LUMEN_REFLECTION_TILE_SIZE - 1) && (y == 0 || y == LUMEN_REFLECTION_TILE_SIZE - 1);
			vec4 entry = vec4_splat(0.0);
			if(!corner)
			{
				entry = LumenLoadTileEntry(group_origin + ivec2(x, y) - LUMEN_REFLECTION_TILE_BORDER);
			}
			s_lumen_reflection_tile[y * LUMEN_REFLECTION_TILE_SIZE + x] = entry;
		}
	}
	barrier();
	ivec2 pixel = group_origin + local;
	if(pixel.x >= int(u_lumen_view_size.x) || pixel.y >= int(u_lumen_view_size.y))
	{
		return;
	}
	ivec2 tile_center = local + LUMEN_REFLECTION_TILE_BORDER;
	vec4 center = s_lumen_reflection_tile[tile_center.y * LUMEN_REFLECTION_TILE_SIZE + tile_center.x];
	if(center.w < 0.5)
	{
		imageStore(s_lumen_reflection_history_out, pixel, vec4_splat(0.0));
		imageStore(s_lumen_reflection_frames_out, pixel, vec4(-1.0, 0.0, 0.0, 0.0));
		return;
	}
	vec3 specular = LumenYCoCgToRGB(center.xyz);
	float luminance = LumenReflectionLuminance(specular);
	float second_moment = luminance * luminance;
	float frames = 0.0;
	BRANCH
	if(u_lumen_reflection_has_history)
	{
		float depth01 = texelFetch(s_lumen_depth, pixel, 0).x;
		vec4 gbuffer1 = texelFetch(s_lumen_normal, pixel, 0);
		float roughness = gbuffer1.w;
		vec3 normal = normalize(decodeNormalOctahedron(gbuffer1.xy));
		vec3 position = LumenWorldFromDepth(LumenPixelUv(pixel), depth01);
		float noise = InterleavedGradientNoise(vec2(pixel), u_lumen_frame_mod);
		LumenReflectionHistory from_hit;
		from_hit.valid = false;
		from_hit.specular = vec3_splat(0.0);
		from_hit.second_moment = 0.0;
		from_hit.frames = 0.0;
		BRANCH
		if(LumenSpecularDominantDirFactor(roughness) > 0.5)
		{
			// The hit's virtual image: the resolved hit distance behind the surface along the view ray.
			float scene_depth = LumenLinearDepth(depth01);
			float hit_distance = texelFetch(s_lumen_reflection_resolved, pixel, 0).w;
			vec3 camera = LumenReflectionCamera();
			vec3 virtual_point = camera + (position - camera) * ((scene_depth + hit_distance) / max(scene_depth, 1e-5));
			from_hit = LumenReadReflectionHistory(virtual_point, false, position, normal, noise);
		}
		LumenReflectionHistory from_surface = LumenReadReflectionHistory(position, true, position, normal, noise);
		float max_frames = mix(LUMEN_REFLECTION_TEMPORAL_MIRROR_FRAMES, LUMEN_REFLECTION_TEMPORAL_MAX_FRAMES,
		                       saturate(roughness / 0.05));
		BRANCH
		if(from_surface.valid || from_hit.valid)
		{
			vec3 sum = center.xyz;
			vec3 square_sum = center.xyz * center.xyz;
			float weight_sum = 1.0;
			for(int dy = -2; dy <= 2; ++dy)
			{
				for(int dx = -2; dx <= 2; ++dx)
				{
					bool skip = (dx == 0 && dy == 0) || (abs(dx) == 2 && abs(dy) == 2);
					if(!skip)
					{
						ivec2 t = tile_center + ivec2(dx, dy);
						vec4 entry = s_lumen_reflection_tile[t.y * LUMEN_REFLECTION_TILE_SIZE + t.x];
						sum += entry.xyz;
						square_sum += entry.xyz * entry.xyz;
						weight_sum += entry.w;
					}
				}
			}
			vec3 mean = sum / weight_sum;
			vec3 deviation = sqrt(max(square_sum / weight_sum - mean * mean, vec3_splat(0.0)));
			vec3 extent = LUMEN_REFLECTION_NEIGHBORHOOD_CLAMP_SCALE * deviation;
			vec3 hit_ycocg = LumenRGBToYCoCg(from_hit.specular);
			vec3 surface_ycocg = LumenRGBToYCoCg(from_surface.specular);
			vec3 clamped_hit = clamp(hit_ycocg, mean - extent, mean + extent);
			vec3 clamped_surface = clamp(surface_ycocg, mean - extent, mean + extent);
			// The history closer to this frame's neighbourhood wins; the hit history keeps a fifth of the blend,
			// so one bad neighbourhood cannot drop it outright.
			float surface_alpha = length(mean - hit_ycocg) > length(mean - surface_ycocg) + 0.01 ? 0.8 : 0.0;
			surface_alpha = from_hit.valid ? surface_alpha : 1.0;
			vec3 history_ycocg = mix(hit_ycocg, surface_ycocg, surface_alpha);
			vec3 clamped_ycocg = mix(clamped_hit, clamped_surface, surface_alpha);
			float confidence = saturate(1.0 - length(abs(clamped_ycocg - history_ycocg) / max(extent, vec3_splat(0.1))));
			confidence = 0.75 * confidence + 0.25;
			frames = surface_alpha > 0.5 ? from_surface.frames : from_hit.frames;
			frames = min(frames * confidence + 1.0, max_frames);
			float alpha = 1.0 / frames;
			specular = mix(LumenYCoCgToRGB(clamped_ycocg), specular, alpha);
			second_moment = mix(mix(from_hit.second_moment, from_surface.second_moment, surface_alpha), second_moment, alpha);
		}
	}
	specular = max(specular, vec3_splat(0.0));
	imageStore(s_lumen_reflection_history_out, pixel,
	           vec4(LumenReflectionFromDenoiserSpace(specular), max(second_moment, 0.0)));
	imageStore(s_lumen_reflection_frames_out, pixel, vec4(frames, 0.0, 0.0, 0.0));
}
