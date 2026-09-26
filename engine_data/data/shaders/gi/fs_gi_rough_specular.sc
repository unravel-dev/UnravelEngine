$input v_texcoord0

/*
 * GI ROUGH SPECULAR: the screen-probe radiance integrated against each pixel's GGX lobe, for the
 * rough tier of the reflections (Lumen's rough specular, UE 5.8 LumenScreenProbeGather.usf
 * 1516-1561). Runs on this frame's probes, after the probe filter, at the gather's trace
 * resolution.
 *
 *  - GI_REFLECTION_ROUGH_SPECULAR_SAMPLES directions of the pixel's lobe, drawn by the reflection
 *    tier's bounded-cap VNDF sampler (gi_reflection_sampling.sh), each looked up in the final
 *    filtered radiance of the four probes bracketing the pixel - the integrate's bracket, with
 *    its bilinear x plane weights and its interpolation jitter - and averaged in the bounded
 *    range (gi_reflection_denoise.sh), so one bright texel cannot carry the mean.
 *  - A probe texel whose centre lies under the probe's tangent cap (GI_IMPORTANCE_MIN_COSINE) is
 *    never traced and holds no radiance: it is weighted out of the octahedral bilinear instead
 *    of being read as darkness.
 *  - A running mean reprojected with the surface and validated per tap against last frame's
 *    depth (the gather temporal's rule, gi_temporal_common.sh). It reads the same probes as the
 *    diffuse resolve and so needs the same window at rest, the gather's slow window; the camera's
 *    motion collapses it to GI_REFLECTION_ROUGH_SPECULAR_FRAMES, Lumen's, because a moving view
 *    changes what a lobe sees; moving hits and changed placements collapse it toward
 *    GI_TEMPORAL_MOVING_MIN_FRAMES.
 *
 * Output: rgb = the pre-exposed radiance averaged over the lobe, the reflection tier's
 * convention (the lighting applies the environment BRDF); a = the accumulated frame count, 0
 * where the pixel is outside the band or no probe serves it.
 */

#include "../common.sh"
#include "../lighting.sh"
// BUFFER_RO for the probe records (sdf_common.sh pulls it into the integrate the same way).
#include "../bgfx_compute.sh"
#include "gi/gi_constants.sh"
#include "gi/gi_probe_common.sh"
#include "gi/gi_noise.sh"
#include "gi/gi_reflection_sampling.sh"
#include "gi/gi_reflection_denoise.sh"
#include "gi/gi_pre_exposure.sh"

/// This frame's final filtered probe radiance, pre-exposed (the probe filter's last pass).
SAMPLER2D(s_rough_probe_radiance, 2);
/// Last frame's result: rgb under last frame's pre-exposure, a = its accumulated count.
SAMPLER2D(s_rough_history, 5);
/// Last frame's full-resolution depth, for the per-tap history validity.
SAMPLER2D(s_gi_prev_depth, 6);
BUFFER_RO(b_gi_probes, vec4, 7);
SAMPLER2D(s_gi_depth, 8);
SAMPLER2D(s_gi_normal, 9);
/// Velocity buffer (full resolution): RG = total uv delta, BA = the object-only component.
SAMPLER2D(s_gi_velocity, 14);

/// xyz = camera position, w = frame index.
uniform vec4 u_gi_camera;
/// xy = the frame's R2 offset on the gather's 8-frame cycle: the integrate's interpolation
/// jitter. zw = this frame's point of the unbounded R2 sequence, which starts its run of lobe
/// samples - never a short cycle: a repeating per-texel sample set converges to a fixed error
/// the interleaved gradient noise prints as a static diagonal hatch.
uniform vec4 u_gi_jitter;
uniform mat4 u_gi_prev_view_proj;
uniform mat4 u_gi_prev_inv_view_proj;
/// x = the window at rest in frames (the gather's slow window) when s_rough_history holds last
/// frame's result, 0 when it does not; y = 1 when the velocity buffer is bound; z = the GI
/// intensity (the diffuse resolve's artistic multiplier); w = the reprojection tolerance as a
/// fraction of view distance.
uniform vec4 u_gi_rough_specular;

// u_gi_temporal_dirty: x/y the dirty regions (GiDirtyRegionFactorRaw), z the camera's motion this
// frame in [0, 1], w the world units one unit of screen uv spans at unit view distance (negative
// for an orthographic projection).
#include "gi/gi_dirty_regions.sh"
#include "gi/gi_temporal_common.sh"

/// One probe of a pixel's bracket: its radiance tile, the normal its trace culled directions
/// against, its bracket weight (0 = it does not serve the pixel) and the share of its rays that
/// hit moving geometry.
struct GiRoughProbe
{
	ivec2 tile;
	vec3 normal;
	float weight;
	float moving_share;
};

/// Probe (base + (i, j)) of the bracket, weighted as the integrate weights it: bilinear
/// position (floored at 0.01) x plane agreement with the pixel.
GiRoughProbe GiRoughSpecularProbe(vec2 base, vec2 fraction, int i, int j, vec3 world_position,
                                  vec3 world_normal, float plane_tolerance)
{
	GiRoughProbe probe;
	probe.tile = ivec2(0, 0);
	probe.normal = world_normal;
	probe.weight = 0.0;
	probe.moving_share = 0.0;
	int px = int(clamp(base.x + float(i), 0.0, float(u_gi_probe_count_x - 1)));
	int py = int(clamp(base.y + float(j), 0.0, float(u_gi_probe_count_y - 1)));
	uint record = (GiProbeRecord(px, py, 0) + u_gi_probe_write_offset) * uint(GI_PROBE_STRIDE);
	vec4 meta = b_gi_probes[record + uint(GI_PROBE_META)];
	if(meta.w < 0.5)
	{
		return probe;
	}
	float plane = abs(dot(meta.xyz - world_position, world_normal));
	float plane_weight = saturate(1.0 - plane / plane_tolerance);
	float bilinear = (i == 0 ? 1.0 - fraction.x : fraction.x) * (j == 0 ? 1.0 - fraction.y : fraction.y);
	probe.weight = max(bilinear, 0.01) * plane_weight;
	probe.tile = GiProbeAtlasBase(px, py, 0);
	probe.normal = b_gi_probes[record + uint(GI_PROBE_META2)].xyz;
	probe.moving_share = saturate(b_gi_probes[record + uint(GI_PROBE_SCREEN_SHARE)].y);
	return probe;
}

/// @p probe's filtered radiance over the four octahedral taps of one direction, renormalised
/// over the taps its trace fired. w = 1 when any tap was traced.
vec4 GiRoughProbeRadiance(GiRoughProbe probe, ivec2 tap0, ivec2 tap1, ivec2 tap2, ivec2 tap3,
                          vec3 dir0, vec3 dir1, vec3 dir2, vec3 dir3, vec4 bilinear)
{
	if(probe.weight <= 0.0)
	{
		return vec4_splat(0.0);
	}
	vec4 traced = vec4(dot(dir0, probe.normal) >= GI_IMPORTANCE_MIN_COSINE ? 1.0 : 0.0,
	                   dot(dir1, probe.normal) >= GI_IMPORTANCE_MIN_COSINE ? 1.0 : 0.0,
	                   dot(dir2, probe.normal) >= GI_IMPORTANCE_MIN_COSINE ? 1.0 : 0.0,
	                   dot(dir3, probe.normal) >= GI_IMPORTANCE_MIN_COSINE ? 1.0 : 0.0);
	vec4 weights = bilinear * traced;
	float weight_sum = weights.x + weights.y + weights.z + weights.w;
	if(weight_sum <= 1e-4)
	{
		return vec4_splat(0.0);
	}
	vec3 radiance = texelFetch(s_rough_probe_radiance, probe.tile + tap0, 0).xyz * weights.x +
	                texelFetch(s_rough_probe_radiance, probe.tile + tap1, 0).xyz * weights.y +
	                texelFetch(s_rough_probe_radiance, probe.tile + tap2, 0).xyz * weights.z +
	                texelFetch(s_rough_probe_radiance, probe.tile + tap3, 0).xyz * weights.w;
	return vec4(max(radiance, vec3_splat(0.0)) / weight_sum, 1.0);
}

/// The bracket's filtered radiance along @p direction. w = the bracket weight that found
/// radiance there, 0 when no probe traced this direction.
vec4 GiRoughBracketRadiance(GiRoughProbe p0, GiRoughProbe p1, GiRoughProbe p2, GiRoughProbe p3,
                            vec3 direction)
{
	vec2 oct = GiOctEncode(direction) * float(GI_PROBE_DIR_EDGE) - vec2_splat(0.5);
	vec2 oct_base = floor(oct);
	vec2 oct_frac = oct - oct_base;
	ivec2 base_texel = ivec2(oct_base);
	// The four taps and their centre directions are the same for every probe of the bracket.
	ivec2 tap0 = GiOctWrapTexel(base_texel);
	ivec2 tap1 = GiOctWrapTexel(base_texel + ivec2(1, 0));
	ivec2 tap2 = GiOctWrapTexel(base_texel + ivec2(0, 1));
	ivec2 tap3 = GiOctWrapTexel(base_texel + ivec2(1, 1));
	float inv_edge = 1.0 / float(GI_PROBE_DIR_EDGE);
	vec3 dir0 = GiOctDecode((vec2(tap0) + vec2_splat(0.5)) * inv_edge);
	vec3 dir1 = GiOctDecode((vec2(tap1) + vec2_splat(0.5)) * inv_edge);
	vec3 dir2 = GiOctDecode((vec2(tap2) + vec2_splat(0.5)) * inv_edge);
	vec3 dir3 = GiOctDecode((vec2(tap3) + vec2_splat(0.5)) * inv_edge);
	vec4 bilinear = vec4((1.0 - oct_frac.x) * (1.0 - oct_frac.y),
	                     oct_frac.x * (1.0 - oct_frac.y),
	                     (1.0 - oct_frac.x) * oct_frac.y,
	                     oct_frac.x * oct_frac.y);
	vec4 r0 = GiRoughProbeRadiance(p0, tap0, tap1, tap2, tap3, dir0, dir1, dir2, dir3, bilinear);
	vec4 r1 = GiRoughProbeRadiance(p1, tap0, tap1, tap2, tap3, dir0, dir1, dir2, dir3, bilinear);
	vec4 r2 = GiRoughProbeRadiance(p2, tap0, tap1, tap2, tap3, dir0, dir1, dir2, dir3, bilinear);
	vec4 r3 = GiRoughProbeRadiance(p3, tap0, tap1, tap2, tap3, dir0, dir1, dir2, dir3, bilinear);
	float w0 = p0.weight * r0.w;
	float w1 = p1.weight * r1.w;
	float w2 = p2.weight * r2.w;
	float w3 = p3.weight * r3.w;
	float weight_sum = w0 + w1 + w2 + w3;
	if(weight_sum <= 1e-4)
	{
		return vec4_splat(0.0);
	}
	return vec4((r0.xyz * w0 + r1.xyz * w1 + r2.xyz * w2 + r3.xyz * w3) / weight_sum, weight_sum);
}

/// Last frame's result reprojected with the surface: rgb under last frame's pre-exposure, a =
/// the accumulated count; zero when no tap of the 2x2 footprint held this surface with an
/// estimate. The gather temporal's reprojection: the camera's matrices for static pixels, the
/// velocity buffer's object lane for moving ones, and a tolerance that is a dithered fraction of
/// view distance widened by a moving receiver's own world displacement.
vec4 GiRoughSpecularHistory(vec2 uv, vec3 world_position, float view_distance)
{
	float object_w = 0.0;
	vec2 prev_uv_object = vec2_splat(0.0);
	float object_uv_motion = 0.0;
	if(u_gi_rough_specular.y > 0.5)
	{
		vec4 velocity = texture2DLod(s_gi_velocity, uv, 0.0);
		prev_uv_object = uv - velocity.xy;
		vec2 velocity_size = vec2(textureSize(s_gi_velocity, 0));
		object_uv_motion = length(velocity.zw);
		object_w = smoothstep(0.5, 1.5, length(velocity.zw * velocity_size));
	}
	vec4 prev_clip4 = mul(u_gi_prev_view_proj, vec4(world_position, 1.0));
	if(prev_clip4.w <= 0.0 && object_w < 0.5)
	{
		return vec4_splat(0.0);
	}
	vec3 prev_clip = clipTransform(prev_clip4.xyz / max(prev_clip4.w, 1e-6));
	vec2 prev_uv = mix(prev_clip.xy * 0.5 + 0.5, prev_uv_object, object_w);
	if(any(lessThan(prev_uv, vec2_splat(0.0))) || any(greaterThan(prev_uv, vec2_splat(1.0))))
	{
		return vec4_splat(0.0);
	}
	vec2 history_size = u_gi_probe_screen.xy;
	vec2 history_pos = prev_uv * history_size - vec2_splat(0.5);
	vec2 history_base = floor(history_pos);
	vec2 history_frac = history_pos - history_base;
	float dither =
	    1.0 + (GiIgnNoise(ivec2(uv * history_size)).x * 2.0 - 1.0) * GI_TEMPORAL_VALIDITY_DITHER;
	float uv_world_scale = u_gi_temporal_dirty.w;
	float uv_world_span = uv_world_scale >= 0.0 ? uv_world_scale * view_distance : -uv_world_scale;
	float tolerance = u_gi_rough_specular.w * dither * view_distance +
	                  GI_TEMPORAL_OBJECT_MOTION_SLACK * object_uv_motion * uv_world_span;
	vec4 sum = vec4_splat(0.0);
	float weight_sum = 0.0;
	for(int tap = 0; tap < 4; ++tap)
	{
		vec2 offset = vec2(float(tap % 2), float(tap / 2));
		float bilinear = (offset.x < 0.5 ? 1.0 - history_frac.x : history_frac.x) *
		                 (offset.y < 0.5 ? 1.0 - history_frac.y : history_frac.y);
		vec2 tap_uv = (history_base + offset + vec2_splat(0.5)) / history_size;
		vec4 tap_value = texture2DLod(s_rough_history, tap_uv, 0.0);
		// A tap that held no estimate last frame (outside the band, or no probe served it) is
		// not history, whatever its depth says.
		if(bilinear <= 0.0 || tap_value.w < 0.5)
		{
			continue;
		}
		float weight = bilinear * GiHistoryTapValid(tap_uv, world_position, tolerance);
		sum += tap_value * weight;
		weight_sum += weight;
	}
	return weight_sum > 1e-4 ? sum / weight_sum : vec4_splat(0.0);
}

void main()
{
	vec2 uv = v_texcoord0;
	float depth = texture2DLod(s_gi_depth, uv, 0.0).x;
	GBufferDataNormalMetalRoughness nd = DecodeGBufferNormalMetalRoughnessLod(uv, s_gi_normal, 0.0);
	// RAW authored roughness, as the reflection tiers read it: the band starts where the rough
	// tier takes a share of the reflection and ends where the diffuse resolve answers alone.
	float roughness = nd.roughness;
	if(depth >= 1.0 || dot(nd.world_normal, nd.world_normal) < 0.5 ||
	   roughness < GI_REFLECTION_GATHER_FADE_START || roughness >= GI_REFLECTION_ROUGH_SPECULAR_MAX)
	{
		gl_FragColor = vec4_splat(0.0);
		return;
	}
	vec3 normal = normalize(nd.world_normal);
	vec3 clip = clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(depth)));
	vec3 world_position = clipToWorld(u_invViewProj, clip);
	float view_distance = max(length(world_position - u_gi_camera.xyz), 1e-3);
	vec3 view = (u_gi_camera.xyz - world_position) / view_distance;
	// THE BRACKET, exactly the integrate's: the jittered 2x2, and the unjittered one when every
	// jittered probe fails the plane test.
	float plane_tolerance = GI_INTEGRATE_PLANE_TOLERANCE * view_distance;
	ivec2 pixel = ivec2(uv * u_gi_probe_screen.xy);
	vec2 grid = uv * u_gi_probe_screen.xy / u_gi_probe_spacing - vec2_splat(0.5);
	vec2 pixel_noise = GiIgnNoise(pixel);
	vec2 jitter = (fract(pixel_noise + u_gi_jitter.xy) - vec2_splat(0.5)) * GI_INTERPOLATION_JITTER_TILES;
	vec2 base = floor(grid + jitter);
	vec2 bracket_fraction = grid + jitter - base;
	GiRoughProbe p0 = GiRoughSpecularProbe(base, bracket_fraction, 0, 0, world_position, normal, plane_tolerance);
	GiRoughProbe p1 = GiRoughSpecularProbe(base, bracket_fraction, 1, 0, world_position, normal, plane_tolerance);
	GiRoughProbe p2 = GiRoughSpecularProbe(base, bracket_fraction, 0, 1, world_position, normal, plane_tolerance);
	GiRoughProbe p3 = GiRoughSpecularProbe(base, bracket_fraction, 1, 1, world_position, normal, plane_tolerance);
	if(p0.weight + p1.weight + p2.weight + p3.weight <= 1e-4)
	{
		base = floor(grid);
		bracket_fraction = grid - base;
		p0 = GiRoughSpecularProbe(base, bracket_fraction, 0, 0, world_position, normal, plane_tolerance);
		p1 = GiRoughSpecularProbe(base, bracket_fraction, 1, 0, world_position, normal, plane_tolerance);
		p2 = GiRoughSpecularProbe(base, bracket_fraction, 0, 1, world_position, normal, plane_tolerance);
		p3 = GiRoughSpecularProbe(base, bracket_fraction, 1, 1, world_position, normal, plane_tolerance);
	}
	float bracket_weight = p0.weight + p1.weight + p2.weight + p3.weight;
	if(bracket_weight <= 1e-4)
	{
		gl_FragColor = vec4_splat(0.0);
		return;
	}
	// THE LOBE SAMPLES: R2 points 4n .. 4n + 3 of the unbounded sequence at frame n, rotated
	// per pixel by the IGN, whose 3x3 stratification the rough tier's upsample averages over.
	vec2 sample_start = pixel_noise + 4.0 * u_gi_jitter.zw;
	vec3 bounded_sum = vec3_splat(0.0);
	float sample_count = 0.0;
	for(int s = 0; s < GI_REFLECTION_ROUGH_SPECULAR_SAMPLES; ++s)
	{
		vec2 xi = fract(sample_start + GI_R2_ADVANCE * float(s));
		vec3 direction = GiReflectionMakeRay(normal, view, roughness, xi).direction;
		vec4 radiance = GiRoughBracketRadiance(p0, p1, p2, p3, direction);
		if(radiance.w > 0.0)
		{
			bounded_sum += GiReflToBounded(radiance.xyz);
			sample_count += 1.0;
		}
	}
	if(sample_count < 0.5)
	{
		gl_FragColor = vec4_splat(0.0);
		return;
	}
	vec3 current = GiReflFromBounded(bounded_sum / sample_count) * u_gi_rough_specular.z;
	vec4 result = vec4(current, 1.0);
	BRANCH
	if(u_gi_rough_specular.x > 0.5)
	{
		vec4 history = GiRoughSpecularHistory(uv, world_position, view_distance);
		if(history.w > 0.0)
		{
			float moving_share = (p0.moving_share * p0.weight + p1.moving_share * p1.weight +
			                      p2.moving_share * p2.weight + p3.moving_share * p3.weight) /
			                     bracket_weight;
			float motion_cap = mix(max(u_gi_rough_specular.x, GI_REFLECTION_ROUGH_SPECULAR_FRAMES),
			                       GI_REFLECTION_ROUGH_SPECULAR_FRAMES,
			                       saturate(u_gi_temporal_dirty.z));
			float collapse = max(GiDirtyRegionFactorRaw(world_position), GiTemporalMovingAmount(moving_share));
			float cap = mix(motion_cap, GI_TEMPORAL_MOVING_MIN_FRAMES, collapse);
			float count = min(history.w + 1.0, max(cap, 1.0));
			result = vec4(mix(history.xyz * u_history_pre_exposure_correction, current, 1.0 / count), count);
		}
	}
	gl_FragColor = result;
}
