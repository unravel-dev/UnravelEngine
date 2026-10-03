/*
 * Lumen screen probe gather, integrate + temporal, fused per full-resolution pixel (UE 5.8
 * ScreenProbeIntegrateCS, LumenScreenProbeGather.usf:1127-1583, and ScreenProbeTemporalReprojectionCS,
 * LumenScreenProbeGatherTemporal.usf:288-583).
 *
 * Integrate: the pixel interpolates the SH3 irradiance of the 2x2 uniform probes around it with an
 * expanded bilinear (never fully on one probe), each corner weighted by how far its probe lies from the
 * pixel's tangent plane relative to the pixel's depth: exp2(-LUMEN_INTERP_DEPTH_WEIGHT r^2). A corner whose
 * uniform probe weighs below LUMEN_INTERP_MIN_WEIGHT takes the best adaptive probe of its tile instead
 * (lumen_adaptive_probes.sh), and the looser LUMEN_INTERP_FALLBACK_DEPTH_WEIGHT serves when nothing passes. The
 * interpolation position is first jittered by up to one probe tile when the jittered pixel lies on the pixel's
 * plane, which turns the probe lattice into noise the temporal averages away.
 *
 * Rough specular (LumenScreenProbeGather.usf:1502-1570): below LUMEN_MAX_ROUGHNESS_TO_EVALUATE_ROUGH_SPECULAR,
 * LUMEN_ROUGH_SPECULAR_SAMPLES GGX visible-normal samples of the pixel's lobe (roughness floored at
 * LUMEN_ROUGH_SPECULAR_MIN_ROUGHNESS, E.y squeezed by LUMEN_SPECULAR_SAMPLE_BIAS) look up the same four
 * probes' bordered radiance with the same weights; their mean is taken in the x / (1 + Y) range and fades
 * into E / pi over LUMEN_ROUGH_SPECULAR_FADE_LENGTH. Pixels the traced reflections own fully (roughness
 * below LUMEN_MAX_ROUGHNESS_TO_TRACE - LUMEN_ROUGHNESS_FADE_LENGTH) keep E / pi.
 *
 * Temporal: last frame's result at the pixel's reprojection, from the 2x2 history taps whose stored
 * depth agrees with the reprojected depth (1% x U(0.5, 1.5) / lerp(0.1, 1, NoV) of it), blended with
 * weight 1 / (1 + N), N = the taps' frame count + 1, up to LUMEN_TEMPORAL_MAX_FRAMES; the rough specular
 * history shares the taps and the weight. No neighbourhood clamp: camera motion never shortens the
 * history.
 *
 * Writes the histories (rgb = E / pi and the rough specular, pre-exposed; a = the stored frame count), the
 * resolve the lighting consumes (rgb = E / pi x intensity, a = 1 on geometry, 0 on the sky) and the rough
 * specular (rgb = radiance x intensity, pre-exposed; a = the frame count + 1, 0 on the sky).
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "../pre_exposure.sh"
#include "lumen/lumen_common.sh"
#include "gi/gi_reflection_sampling.sh"

SAMPLER2D(s_lumen_depth, 0);
/// G-buffer target 1: octahedral normal, metalness, roughness.
SAMPLER2D(s_lumen_normal, 1);
SAMPLER2D(s_lumen_probe_records, 2);
SAMPLER2D(s_lumen_probe_sh, 3);
IMAGE2D_WO(s_lumen_rough_history_out, rgba16f, 4);
IMAGE2D_WO(s_lumen_rough_out, rgba16f, 5);
IMAGE2D_WO(s_lumen_history_out, rgba16f, 6);
IMAGE2D_WO(s_lumen_resolve_out, rgba16f, 7);
SAMPLER2D(s_lumen_history, 8);
/// Last frame's device depth.
SAMPLER2D(s_lumen_prev_depth, 9);
SAMPLER2D(s_lumen_rough_history, 10);
/// The filtered probe radiance with its octahedral border (cs_lumen_probe_border.sc), bilinear.
SAMPLER2D(s_lumen_probe_border, 11);
/// The adaptive probes' tile lists (lumen_adaptive_probes.sh).
BUFFER_RO(b_lumen_adaptive, uint, 12);

#include "lumen/lumen_history.sh"
#include "lumen/lumen_adaptive_probes.sh"

/// x > 0 when the histories and s_lumen_prev_depth hold last frame, y = GI intensity, z > 0 computes the rough
/// specular, w > 0 paints the pixels the uniform probes cannot interpolate (diagnostic: red = the fallback depth
/// weights served them, the pixels UE places adaptive probes for; magenta = not even those).
uniform vec4 u_lumen_temporal;

#define u_lumen_has_history (u_lumen_temporal.x > 0.0)
#define u_lumen_intensity   u_lumen_temporal.y
#define u_lumen_rough_specular (u_lumen_temporal.z > 0.0)
#define u_lumen_show_interpolation_fallback (u_lumen_temporal.w > 0.0)

#define LUMEN_PROBE_BORDER_RES (LUMEN_PROBE_TRACE_RES + 2 * LUMEN_PROBE_RADIANCE_BORDER)

/// E / pi of one probe's SH3 along @p normal: 4 pi x <SH / (4 pi), cosine transfer> / pi.
vec3 LumenProbeIrradianceOverPi(ivec2 tile, LumenSH3 transfer)
{
	int x = tile.x * LUMEN_SH_TEXELS_PER_PROBE;
	vec4 ambient = texelFetch(s_lumen_probe_sh, ivec2(x, tile.y), 0);
	vec3 result = vec3_splat(0.0);
	for(int channel = 0; channel < 3; ++channel)
	{
		vec4 low = texelFetch(s_lumen_probe_sh, ivec2(x + 1 + 2 * channel, tile.y), 0);
		vec4 high = texelFetch(s_lumen_probe_sh, ivec2(x + 2 + 2 * channel, tile.y), 0);
		LumenSH3 sh;
		sh.v0 = vec4(channel == 0 ? ambient.x : (channel == 1 ? ambient.y : ambient.z), low.x, low.y, low.z);
		sh.v1 = vec4(low.w, high.x, high.y, high.z);
		sh.v2 = high.w;
		float value = 4.0 * LumenDotSH3(sh, transfer);
		result.x = channel == 0 ? value : result.x;
		result.y = channel == 1 ? value : result.y;
		result.z = channel == 2 ? value : result.z;
	}
	return result;
}

/// The pixel's interpolation offset: the blue-noise jitter of up to one probe tile (half beyond
/// LUMEN_FULL_RES_JITTER_NEAR + RAMP), kept only when the jittered pixel lies on this pixel's plane.
vec2 LumenFullResJitter(ivec2 pixel, vec3 position, vec3 normal, float depth)
{
	vec2 noise = LumenSpatioTemporalNoise2D(vec2(pixel));
	float width = LUMEN_FULL_RES_JITTER_WIDTH *
	              mix(1.0, LUMEN_FULL_RES_JITTER_FAR_SCALE,
	                  saturate((depth - LUMEN_FULL_RES_JITTER_NEAR) / LUMEN_FULL_RES_JITTER_RAMP));
	vec2 jitter = (2.0 * noise - 1.0) * u_lumen_downsample * width;
	ivec2 jittered = clamp(pixel + ivec2(floor(jitter + 0.5)), ivec2(0, 0), ivec2(u_lumen_view_size) - ivec2(1, 1));
	float jittered_depth01 = texelFetch(s_lumen_depth, jittered, 0).x;
	if(jittered_depth01 >= 1.0)
	{
		return vec2_splat(0.0);
	}
	vec3 jittered_position = LumenWorldFromDepth(LumenPixelUv(jittered), jittered_depth01);
	float plane_distance = abs(dot(jittered_position - position, normal)) / depth;
	bool on_plane = exp2(-LUMEN_FULL_RES_JITTER_PLANE_WEIGHT * plane_distance * plane_distance) > LUMEN_INTERP_MIN_WEIGHT;
	return on_plane ? jitter : vec2_splat(0.0);
}

/// One interpolation corner: the atlas tile of the probe serving it and its weights (x = primary, y = fallback).
struct LumenCorner
{
	ivec2 tile;
	vec2 weights;
};

/// Uniform corner @p tile, or - when its probe weighs below LUMEN_INTERP_MIN_WEIGHT - the adaptive probe of that tile
/// with the largest primary weight above it (UE CalculateUpsampleInterpolationWeights, LumenScreenProbeGather.usf
/// :266-305). The adaptive weights see the pixel itself, without the full-resolution jitter.
LumenCorner LumenInterpolationCorner(ivec2 tile, float bilinear, ivec2 pixel, vec3 position, vec3 normal, float depth)
{
	LumenCorner corner;
	corner.tile = tile;
	corner.weights = LumenProbeCornerWeights(texelFetch(s_lumen_probe_records, tile, 0), bilinear, position, normal, depth);
	if(corner.weights.x >= LUMEN_INTERP_MIN_WEIGHT)
	{
		return corner;
	}
	int tile_index = LumenAdaptiveTileIndex(tile);
	int count = int(min(b_lumen_adaptive[LUMEN_ADAPTIVE_HEADER + tile_index], uint(LUMEN_ADAPTIVE_SAMPLES)));
	for(int k = 0; k < count; ++k)
	{
		int index = int(b_lumen_adaptive[LUMEN_ADAPTIVE_INDICES + tile_index * LUMEN_ADAPTIVE_SAMPLES + k]);
		ivec2 adaptive_tile = LumenAdaptiveAtlasTile(index);
		vec4 record = texelFetch(s_lumen_probe_records, adaptive_tile, 0);
		vec2 weights = LumenAdaptiveProbeWeights(pixel, position, normal, depth, LumenProbeRecordPixel(record), record.w);
		if(weights.x > corner.weights.x)
		{
			corner.tile = adaptive_tile;
			corner.weights = weights;
		}
	}
	return corner;
}

/// The pixel's four probes (from the corners base, +x, +y, +xy) and their normalized weights; valid false when no
/// corner passes even the fallback weights (UE FScreenProbeSample).
struct LumenProbeSample
{
	/// The probes' atlas tiles: tiles01.xy / .zw = corners base / +x, tiles23.xy / .zw = +y / +xy.
	ivec4 tiles01;
	ivec4 tiles23;
	vec4 weights;
	bool valid;
	/// The primary depth weights did not reach LUMEN_INTERP_MIN_WEIGHT (the fallback weights served).
	bool fallback;
};

LumenProbeSample LumenInterpolationProbes(ivec2 pixel, vec3 position, vec3 normal, float depth)
{
	vec2 offset = u_lumen_full_res_jitter ? LumenFullResJitter(pixel, position, normal, depth) : vec2_splat(0.0);
	vec4 interpolation = LumenUniformInterpolation(vec2(pixel) - u_lumen_placement_jitter + offset);
	ivec2 base = ivec2(interpolation.xy);
	vec2 f = interpolation.zw;
	LumenCorner c00 = LumenInterpolationCorner(base, (1.0 - f.x) * (1.0 - f.y), pixel, position, normal, depth);
	LumenCorner c10 = LumenInterpolationCorner(base + ivec2(1, 0), f.x * (1.0 - f.y), pixel, position, normal, depth);
	LumenCorner c01 = LumenInterpolationCorner(base + ivec2(0, 1), (1.0 - f.x) * f.y, pixel, position, normal, depth);
	LumenCorner c11 = LumenInterpolationCorner(base + ivec2(1, 1), f.x * f.y, pixel, position, normal, depth);
	LumenProbeSample probes;
	probes.tiles01 = ivec4(c00.tile, c10.tile);
	probes.tiles23 = ivec4(c01.tile, c11.tile);
	vec4 weights = vec4(c00.weights.x, c10.weights.x, c01.weights.x, c11.weights.x);
	float weight_sum = dot(weights, vec4_splat(1.0));
	probes.fallback = weight_sum < LUMEN_INTERP_MIN_WEIGHT;
	if(probes.fallback)
	{
		weights = vec4(c00.weights.y, c10.weights.y, c01.weights.y, c11.weights.y);
		weight_sum = dot(weights, vec4_splat(1.0));
	}
	probes.valid = weight_sum >= LUMEN_INTERP_MIN_WEIGHT;
	probes.weights = weights / max(weight_sum, LUMEN_INTERP_MIN_WEIGHT);
	return probes;
}

/// The interpolated E / pi along @p normal.
vec3 LumenInterpolateIrradianceOverPi(LumenProbeSample probes, vec3 normal)
{
	LumenSH3 transfer = LumenDiffuseTransferSH3(normal);
	return probes.weights.x * LumenProbeIrradianceOverPi(probes.tiles01.xy, transfer) +
	       probes.weights.y * LumenProbeIrradianceOverPi(probes.tiles01.zw, transfer) +
	       probes.weights.z * LumenProbeIrradianceOverPi(probes.tiles23.xy, transfer) +
	       probes.weights.w * LumenProbeIrradianceOverPi(probes.tiles23.zw, transfer);
}

/// One probe's bordered radiance along @p direction, bilinear (UE InterpolateFromScreenProbes, mip 0).
vec3 LumenProbeRadiance(ivec2 tile, vec2 probe_uv, vec2 inv_atlas_size)
{
	vec2 texel = vec2(tile * LUMEN_PROBE_BORDER_RES) + float(LUMEN_PROBE_RADIANCE_BORDER) +
	             probe_uv * float(LUMEN_PROBE_TRACE_RES);
	return texture2DLod(s_lumen_probe_border, texel * inv_atlas_size, 0.0).xyz;
}

vec3 LumenInterpolateRadiance(LumenProbeSample probes, vec3 direction)
{
	vec2 probe_uv = LumenInverseEquiAreaSphericalMapping(direction);
	vec2 inv_atlas_size = vec2_splat(1.0) / vec2(textureSize(s_lumen_probe_border, 0));
	return probes.weights.x * LumenProbeRadiance(probes.tiles01.xy, probe_uv, inv_atlas_size) +
	       probes.weights.y * LumenProbeRadiance(probes.tiles01.zw, probe_uv, inv_atlas_size) +
	       probes.weights.z * LumenProbeRadiance(probes.tiles23.xy, probe_uv, inv_atlas_size) +
	       probes.weights.w * LumenProbeRadiance(probes.tiles23.zw, probe_uv, inv_atlas_size);
}

/// UE RoughReflectionsDiffuseLerp: 1 = the rough specular is E / pi (the traced reflections own the pixel,
/// or the lobe is wide enough), 0 = all GGX samples.
float LumenRoughDiffuseLerp(float roughness)
{
	float traced = saturate((LUMEN_MAX_ROUGHNESS_TO_TRACE - roughness) / LUMEN_ROUGHNESS_FADE_LENGTH);
	if(traced >= 1.0)
	{
		return 1.0;
	}
	return saturate((roughness - LUMEN_MAX_ROUGHNESS_TO_EVALUATE_ROUGH_SPECULAR + LUMEN_ROUGH_SPECULAR_FADE_LENGTH) /
	                LUMEN_ROUGH_SPECULAR_FADE_LENGTH);
}

float LumenLuminance(vec3 color)
{
	return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

/// The lobe's mean radiance from the probes (pre-exposed): LUMEN_ROUGH_SPECULAR_SAMPLES visible-normal
/// samples (UE ComputeIndirectLightingSampleE seeds, BiasBSDFImportantSample), averaged in x / (1 + Y).
vec3 LumenRoughSpecular(ivec2 pixel, LumenProbeSample probes, vec3 position, vec3 normal, float roughness)
{
	float alpha = max(roughness, LUMEN_ROUGH_SPECULAR_MIN_ROUGHNESS);
	alpha *= alpha;
	vec3 camera = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	vec3 view = normalize(camera - position);
	GiReflectionBasis basis = GiReflectionMakeBasis(normal);
	vec3 view_ts = GiReflectionToTangent(basis, view);
	view_ts.z = max(view_ts.z, 1e-4);
	view_ts = normalize(view_ts);
	int frame_mod = int(mod(u_lumen_frame_index, 8.0));
	uvec2 seed = Rand3DPCG16(ivec3(pixel, frame_mod)).xy;
	vec3 sum = vec3_splat(0.0);
	for(int i = 0; i < LUMEN_ROUGH_SPECULAR_SAMPLES; ++i)
	{
		vec2 e = Hammersley16(uint(i), uint(LUMEN_ROUGH_SPECULAR_SAMPLES), seed);
		e.y = (e.y - 0.5) * (1.0 - LUMEN_SPECULAR_SAMPLE_BIAS) + 0.5;
		vec3 half_ts = GiReflectionSampleGgxVndf(view_ts, alpha, e.x, e.y);
		vec3 half_vector = GiReflectionToWorld(basis, half_ts);
		vec3 direction = 2.0 * dot(view, half_vector) * half_vector - view;
		vec3 radiance = LumenInterpolateRadiance(probes, direction);
		sum += radiance / (1.0 + LumenLuminance(radiance));
	}
	vec3 mean = sum / float(LUMEN_ROUGH_SPECULAR_SAMPLES);
	return mean / max(1.0 - LumenLuminance(mean), 1e-4);
}

/// Last frame's rough specular over the shared taps, in this frame's pre-exposure.
vec3 LumenReadRoughHistory(LumenHistoryTaps taps)
{
	float weight_sum = dot(taps.weights, vec4_splat(1.0));
	if(weight_sum <= 0.0)
	{
		return vec3_splat(0.0);
	}
	vec3 color = vec3_splat(0.0);
	for(int tap = 0; tap < 4; ++tap)
	{
		color += LumenHistoryTapWeight(taps, tap) * texelFetch(s_lumen_rough_history, LumenHistoryTapTexel(taps, tap), 0).xyz;
	}
	return color / max(weight_sum, 1e-5) * u_history_pre_exposure_correction;
}

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	if(pixel.x >= int(u_lumen_view_size.x) || pixel.y >= int(u_lumen_view_size.y))
	{
		return;
	}
	float depth01 = texelFetch(s_lumen_depth, pixel, 0).x;
	if(depth01 >= 1.0)
	{
		imageStore(s_lumen_history_out, pixel, vec4_splat(0.0));
		imageStore(s_lumen_resolve_out, pixel, vec4_splat(0.0));
		imageStore(s_lumen_rough_history_out, pixel, vec4_splat(0.0));
		imageStore(s_lumen_rough_out, pixel, vec4_splat(0.0));
		return;
	}
	vec3 position = LumenWorldFromDepth(LumenPixelUv(pixel), depth01);
	vec4 gbuffer1 = texelFetch(s_lumen_normal, pixel, 0);
	vec3 normal = decodeNormalOctahedron(gbuffer1.xy);
	float roughness = gbuffer1.w;
	float depth = LumenLinearDepth(depth01);
	LumenProbeSample probes = LumenInterpolationProbes(pixel, position, normal, depth);
	vec3 current = vec3_splat(0.0);
	vec3 rough_current = vec3_splat(0.0);
	BRANCH
	if(probes.valid)
	{
		current = LumenInterpolateIrradianceOverPi(probes, normal);
		rough_current = current;
		float diffuse_lerp = LumenRoughDiffuseLerp(roughness);
		BRANCH
		if(u_lumen_rough_specular && diffuse_lerp < 1.0)
		{
			vec3 lobe = LumenRoughSpecular(pixel, probes, position, normal, roughness);
			rough_current = mix(lobe, current, diffuse_lerp);
		}
	}
	LumenHistoryTaps taps;
	taps.origin = ivec2(0, 0);
	taps.weights = vec4_splat(0.0);
	BRANCH
	if(u_lumen_has_history)
	{
		taps = LumenHistoryReprojection(pixel, position, normal);
	}
	vec4 history_sample = LumenReadHistoryTaps(taps);
	vec3 rough_history = vec3_splat(0.0);
	BRANCH
	if(u_lumen_rough_specular)
	{
		rough_history = LumenReadRoughHistory(taps);
	}
	float frames = history_sample.w;
	float blend = 1.0 / (1.0 + frames);
	if(!probes.valid && frames >= 1.0)
	{
		blend = 1.0 / (1.0 + LUMEN_TEMPORAL_INVALID_CURRENT_WEIGHT * frames);
	}
	vec3 result = max(mix(history_sample.xyz, current, blend), vec3_splat(0.0));
	vec3 rough_result = max(mix(rough_history, rough_current, blend), vec3_splat(0.0));
	float stored_frames = LumenQuantizeFrames(frames);
	imageStore(s_lumen_history_out, pixel, vec4(result, stored_frames));
	vec3 resolve = result * u_lumen_intensity;
	BRANCH
	if(u_lumen_show_interpolation_fallback && probes.fallback)
	{
		resolve = probes.valid ? vec3(1.0, 0.0, 0.0) : vec3(1.0, 0.0, 1.0);
	}
	imageStore(s_lumen_resolve_out, pixel, vec4(resolve, 1.0));
	imageStore(s_lumen_rough_history_out, pixel, vec4(rough_result, stored_frames));
	imageStore(s_lumen_rough_out, pixel, vec4(rough_result * u_lumen_intensity, frames + 1.0));
}
