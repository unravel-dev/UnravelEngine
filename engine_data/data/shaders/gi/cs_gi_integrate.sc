/*
 * Screen probe gather, integrate + temporal, fused per full-resolution pixel: one pass interpolates the probes'
 * lighting at the pixel and accumulates it into the histories.
 *
 * Integrate: the pixel interpolates the SH3 irradiance of the 2x2 uniform probes around it with an
 * expanded bilinear (never fully on one probe), each corner weighted by how far its probe lies from the
 * pixel's tangent plane relative to the pixel's depth: exp2(-GI_INTERP_DEPTH_WEIGHT r^2). A corner whose
 * uniform probe weighs below GI_INTERP_MIN_WEIGHT takes the best adaptive probe of its tile instead
 * (gi_adaptive_probes.sh), and the looser GI_INTERP_FALLBACK_DEPTH_WEIGHT serves when nothing passes. The
 * interpolation position is first jittered by up to one probe tile (half from the final gather quality 4) when the
 * jittered pixel lies on the pixel's plane, which turns the probe lattice into noise the temporal averages away.
 *
 * Rough specular: below GI_MAX_ROUGHNESS_TO_EVALUATE_ROUGH_SPECULAR,
 * GI_ROUGH_SPECULAR_SAMPLES GGX visible-normal samples of the pixel's lobe (roughness floored at
 * GI_ROUGH_SPECULAR_MIN_ROUGHNESS, E.y squeezed by GI_SPECULAR_SAMPLE_BIAS) look up the same four
 * probes' bordered radiance with the same weights; their mean is taken in the x / (1 + Y) range and fades
 * into E / pi over GI_ROUGH_SPECULAR_FADE_LENGTH. Pixels the traced reflections own fully (roughness
 * below the traced roughness limit - GI_ROUGHNESS_FADE_LENGTH) keep E / pi.
 *
 * Temporal: last frame's result at the pixel's reprojection (a moving surface's from where it was, gi_motion.sh),
 * from the 2x2 history taps whose stored depth agrees with the reprojected depth (1% x U(0.5, 1.5) / lerp(0.1, 1,
 * NoV) of it), blended with weight 1 / (1 + N), N = the taps' frame count + 1, up to u_gi_temporal_max_frames;
 * the rough specular history shares the taps and the weight. No neighbourhood clamp: camera motion never shortens
 * the history. Moving lighting does (the fast update): the probes'
 * moving fractions interpolated like their lighting give the fast update amount saturate((moving /
 * GI_TEMPORAL_FAST_UPDATE_MOVING_FRACTION - 0.2) / 0.8), at most GI_TEMPORAL_FAST_UPDATE_MAX_AMOUNT, held at
 * least at last frame's amount where it was, and N is cut to (1 - amount) x the maximum.
 *
 * Short-range AO (GI_INTEGRATE_SHORT_RANGE_AO): the AO accumulates over the same taps with the same history length
 * (gi_short_range_ao_temporal.sh), beside the diffuse in the same pass.
 *
 * Writes the histories (rgb = E / pi and the rough specular, pre-exposed; a = GiEncodeHistoryAlpha for the
 * diffuse, the stored frame count for the rough specular; zero on the sky), which are also this frame's results: the
 * indirect lighting reads the diffuse and the reflections the rough specular, both times the GI intensity. With the
 * AO, its history (bent normal, visibility) and the composite's screen AO.
 */

#include "bgfx_compute.sh"
#include "../common.sh"
#include "../pre_exposure.sh"
#include "gi/gi_common.sh"
#include "gi/gi_reflection_sampling.sh"
#include "gi/gi_image_formats.sh"

SAMPLER2D(s_gi_depth, 0);
/// G-buffer target 1: octahedral normal, metalness, roughness.
SAMPLER2D(s_gi_normal, 1);
SAMPLER2D(s_gi_probe_records, 2);
/// The probes' SH3 (cs_gi_probe_sh.sc; texel 0's alpha = the probe's moving fraction).
SAMPLER2D(s_gi_probe_sh, 3);
/// The rough specular history: R11G11B10 float (no reader takes an alpha).
IMAGE2D_WO(s_gi_rough_history_out, rg11b10f, 4);
IMAGE2D_WO(s_gi_history_out, rgba16f, 6);
#ifdef GI_INTEGRATE_SHORT_RANGE_AO
/// The accumulated AO, packed (GiPackShortRangeAO).
UIMAGE2D_WO(s_gi_short_range_ao_history_out, r32ui, 5);
IMAGE2D_WO(s_gi_short_range_ao_screen_out, rgba8, 7);
/// This frame's AO search (cs_gi_short_range_ao.sc) and last frame's accumulation.
USAMPLER2D(s_gi_short_range_ao, 14);
USAMPLER2D(s_gi_short_range_ao_history, 15);
#endif
SAMPLER2D(s_gi_history, 8);
/// Last frame's device depth.
SAMPLER2D(s_gi_prev_depth, 9);
SAMPLER2D(s_gi_rough_history, 10);
/// The filtered probe radiance with its octahedral border (cs_gi_probe_border.sc), bilinear.
SAMPLER2D(s_gi_probe_border, 11);
/// The adaptive probes' tile lists (gi_adaptive_probes.sh).
BUFFER_RO(b_gi_adaptive, uint, 12);

/// This frame's velocity buffer (where moving surfaces were last frame).
#define GI_VELOCITY_STAGE 13
#include "gi/gi_history.sh"
#include "gi/gi_adaptive_probes.sh"
#ifdef GI_INTEGRATE_SHORT_RANGE_AO
#include "gi/gi_short_range_ao_temporal.sh"
#endif

/// x > 0 when the histories and s_gi_prev_depth hold last frame, y > 0 draws one probe per pixel instead of blending
/// the four (stochastic interpolation, on at High), z > 0 computes the rough specular, w > 0 paints the pixels the
/// uniform probes cannot interpolate into the diffuse history (diagnostic, stored as a fresh history: red = the fallback
/// depth weights served them, the pixels adaptive probes are placed for; magenta = not even those).
uniform vec4 u_gi_temporal;

#define u_gi_has_history (u_gi_temporal.x > 0.0)
#define u_gi_stochastic_interpolation (u_gi_temporal.y > 0.0)
#define u_gi_rough_specular (u_gi_temporal.z > 0.0)
#define u_gi_show_interpolation_fallback (u_gi_temporal.w > 0.0)

#define GI_PROBE_BORDER_RES (GI_PROBE_TRACE_RES + 2 * GI_PROBE_RADIANCE_BORDER)
/// The probe draw's rotation over frames: the golden ratio (a rank-1 lattice, any window of frames stratified).
#define GI_PROBE_DRAW_FRAME_INCREMENT 0.61803398875

/// E / pi of one probe's SH3 along @p normal: 4 pi x <SH / (4 pi), cosine transfer> / pi.
vec3 GiProbeIrradianceOverPi(ivec2 tile, GiSH3 transfer)
{
	int x = tile.x * GI_SH_TEXELS_PER_PROBE;
	vec4 ambient = texelFetch(s_gi_probe_sh, ivec2(x, tile.y), 0);
	vec3 result = vec3_splat(0.0);
	for(int channel = 0; channel < 3; ++channel)
	{
		vec4 low = texelFetch(s_gi_probe_sh, ivec2(x + 1 + 2 * channel, tile.y), 0);
		vec4 high = texelFetch(s_gi_probe_sh, ivec2(x + 2 + 2 * channel, tile.y), 0);
		GiSH3 sh;
		sh.v0 = vec4(channel == 0 ? ambient.x : (channel == 1 ? ambient.y : ambient.z), low.x, low.y, low.z);
		sh.v1 = vec4(low.w, high.x, high.y, high.z);
		sh.v2 = high.w;
		float value = 4.0 * GiDotSH3(sh, transfer);
		result.x = channel == 0 ? value : result.x;
		result.y = channel == 1 ? value : result.y;
		result.z = channel == 2 ? value : result.z;
	}
	return result;
}

/// The probe draw's random number per pixel and frame: interleaved gradient noise along an axis
/// independent of the full-resolution jitter's two (GiSpatioTemporalNoise2D), rotated over frames.
float GiProbeDrawNoise(ivec2 pixel)
{
	float spatial = InterleavedGradientNoise(vec2(float(pixel.x), -float(pixel.y)) + vec2(37.0, 11.0));
	return fract(spatial + u_gi_frame_index * GI_PROBE_DRAW_FRAME_INCREMENT);
}

/// The pixel's interpolation offset: the blue-noise jitter of up to u_gi_full_res_jitter_width probe tiles (half
/// beyond GI_FULL_RES_JITTER_NEAR + RAMP), kept only when the jittered pixel lies on this pixel's plane.
vec2 GiFullResJitter(ivec2 pixel, vec3 position, vec3 normal, float depth)
{
	vec2 noise = GiSpatioTemporalNoise2D(vec2(pixel));
	float width = u_gi_full_res_jitter_width *
	              mix(1.0, GI_FULL_RES_JITTER_FAR_SCALE,
	                  saturate((depth - GI_FULL_RES_JITTER_NEAR) / GI_FULL_RES_JITTER_RAMP));
	vec2 jitter = (2.0 * noise - 1.0) * u_gi_downsample * width;
	ivec2 jittered = clamp(pixel + ivec2(floor(jitter + 0.5)), ivec2(0, 0), ivec2(u_gi_view_size) - ivec2(1, 1));
	float jittered_depth01 = texelFetch(s_gi_depth, jittered, 0).x;
	if(jittered_depth01 >= 1.0)
	{
		return vec2_splat(0.0);
	}
	vec3 jittered_position = GiWorldFromDepth(GiPixelUv(jittered), jittered_depth01);
	float plane_distance = abs(dot(jittered_position - position, normal)) / depth;
	bool on_plane = exp2(-GI_FULL_RES_JITTER_PLANE_WEIGHT * plane_distance * plane_distance) > GI_INTERP_MIN_WEIGHT;
	return on_plane ? jitter : vec2_splat(0.0);
}

/// One interpolation corner: the atlas tile of the probe serving it and its weights (x = primary, y = fallback).
struct GiCorner
{
	ivec2 tile;
	vec2 weights;
};

/// Uniform corner @p tile, or - when its probe weighs below GI_INTERP_MIN_WEIGHT - the adaptive probe of that tile
/// with the largest primary weight above it. The adaptive weights see the pixel itself, without the full-resolution
/// jitter.
GiCorner GiInterpolationCorner(ivec2 tile, float bilinear, ivec2 pixel, vec3 position, vec3 normal, float depth)
{
	GiCorner corner;
	corner.tile = tile;
	corner.weights = GiProbeCornerWeights(texelFetch(s_gi_probe_records, tile, 0), bilinear, position, normal, depth);
	if(corner.weights.x >= GI_INTERP_MIN_WEIGHT)
	{
		return corner;
	}
	int tile_index = GiAdaptiveTileIndex(tile);
	int count = int(min(b_gi_adaptive[GiAdaptiveCountEntry(tile_index)], uint(GI_ADAPTIVE_MAX_SAMPLES)));
	for(int k = 0; k < count; ++k)
	{
		int index = int(b_gi_adaptive[GiAdaptiveProbeEntry(tile_index, k)]);
		ivec2 adaptive_tile = GiAdaptiveAtlasTile(index);
		vec4 record = texelFetch(s_gi_probe_records, adaptive_tile, 0);
		vec2 weights = GiAdaptiveProbeWeights(pixel, position, normal, depth, GiProbeRecordPixel(record), record.w);
		if(weights.x > corner.weights.x)
		{
			corner.tile = adaptive_tile;
			corner.weights = weights;
		}
	}
	return corner;
}

/// The pixel's four probes (from the corners base, +x, +y, +xy) and their normalized weights; valid false when no
/// corner passes even the fallback weights.
struct GiProbeSample
{
	/// The probes' atlas tiles: tiles01.xy / .zw = corners base / +x, tiles23.xy / .zw = +y / +xy.
	ivec4 tiles01;
	ivec4 tiles23;
	vec4 weights;
	bool valid;
	/// The primary depth weights did not reach GI_INTERP_MIN_WEIGHT (the fallback weights served).
	bool fallback;
};

GiProbeSample GiInterpolationProbes(ivec2 pixel, vec3 position, vec3 normal, float depth)
{
	vec2 offset = u_gi_full_res_jitter ? GiFullResJitter(pixel, position, normal, depth) : vec2_splat(0.0);
	vec4 interpolation = GiUniformInterpolation(vec2(pixel) - u_gi_placement_jitter + offset);
	ivec2 base = ivec2(interpolation.xy);
	vec2 f = interpolation.zw;
	GiCorner c00 = GiInterpolationCorner(base, (1.0 - f.x) * (1.0 - f.y), pixel, position, normal, depth);
	GiCorner c10 = GiInterpolationCorner(base + ivec2(1, 0), f.x * (1.0 - f.y), pixel, position, normal, depth);
	GiCorner c01 = GiInterpolationCorner(base + ivec2(0, 1), (1.0 - f.x) * f.y, pixel, position, normal, depth);
	GiCorner c11 = GiInterpolationCorner(base + ivec2(1, 1), f.x * f.y, pixel, position, normal, depth);
	GiProbeSample probes;
	probes.tiles01 = ivec4(c00.tile, c10.tile);
	probes.tiles23 = ivec4(c01.tile, c11.tile);
	vec4 weights = vec4(c00.weights.x, c10.weights.x, c01.weights.x, c11.weights.x);
	float weight_sum = dot(weights, vec4_splat(1.0));
	probes.fallback = weight_sum < GI_INTERP_MIN_WEIGHT;
	if(probes.fallback)
	{
		weights = vec4(c00.weights.y, c10.weights.y, c01.weights.y, c11.weights.y);
		weight_sum = dot(weights, vec4_splat(1.0));
	}
	probes.valid = weight_sum >= GI_INTERP_MIN_WEIGHT;
	probes.weights = weights / max(weight_sum, GI_INTERP_MIN_WEIGHT);
	return probes;
}

/// One of @p probes' four probes drawn in proportion to its weight (stochastic interpolation): every tile becomes the
/// drawn probe's, its weight 1 (0 when none is valid).
GiProbeSample GiDrawProbe(GiProbeSample probes, ivec2 pixel)
{
	vec4 weights = probes.weights;
	float random = min(GiProbeDrawNoise(pixel), 0.99) * dot(weights, vec4_splat(1.0));
	ivec2 tile = probes.tiles01.xy;
	if(random >= weights.x + weights.y + weights.z)
	{
		tile = probes.tiles23.zw;
	}
	else if(random >= weights.x + weights.y)
	{
		tile = probes.tiles23.xy;
	}
	else if(random >= weights.x)
	{
		tile = probes.tiles01.zw;
	}
	probes.tiles01 = ivec4(tile, tile);
	probes.tiles23 = ivec4(tile, tile);
	probes.weights = vec4(probes.valid ? 1.0 : 0.0, 0.0, 0.0, 0.0);
	return probes;
}

/// The interpolated E / pi along @p normal; the drawn probe alone when the interpolation is stochastic.
vec3 GiInterpolateIrradianceOverPi(GiProbeSample probes, vec3 normal)
{
	GiSH3 transfer = GiDiffuseTransferSH3(normal);
	vec3 irradiance = probes.weights.x * GiProbeIrradianceOverPi(probes.tiles01.xy, transfer);
	BRANCH
	if(!u_gi_stochastic_interpolation)
	{
		irradiance += probes.weights.y * GiProbeIrradianceOverPi(probes.tiles01.zw, transfer) +
		              probes.weights.z * GiProbeIrradianceOverPi(probes.tiles23.xy, transfer) +
		              probes.weights.w * GiProbeIrradianceOverPi(probes.tiles23.zw, transfer);
	}
	return irradiance;
}

/// One probe's moving fraction (the alpha of its SH texel 0).
float GiProbeMoving(ivec2 tile)
{
	return texelFetch(s_gi_probe_sh, ivec2(tile.x * GI_SH_TEXELS_PER_PROBE, tile.y), 0).w;
}

/// The interpolated moving fraction of the probes' lighting.
float GiInterpolateMoving(GiProbeSample probes)
{
	BRANCH
	if(u_gi_stochastic_interpolation)
	{
		return probes.weights.x * GiProbeMoving(probes.tiles01.xy);
	}
	vec4 moving = vec4(GiProbeMoving(probes.tiles01.xy),
	                   GiProbeMoving(probes.tiles01.zw),
	                   GiProbeMoving(probes.tiles23.xy),
	                   GiProbeMoving(probes.tiles23.zw));
	return dot(probes.weights, moving);
}

/// This frame's fast update amount from the moving fraction of the pixel's lighting.
float GiFastUpdateAmount(float moving)
{
	float amount = saturate(moving / GI_TEMPORAL_FAST_UPDATE_MOVING_FRACTION);
	return saturate(min((amount - GI_TEMPORAL_FAST_UPDATE_THRESHOLD) / (1.0 - GI_TEMPORAL_FAST_UPDATE_THRESHOLD),
	                    GI_TEMPORAL_FAST_UPDATE_MAX_AMOUNT));
}

/// One probe's bordered radiance along @p direction, bilinear (mip 0).
vec3 GiProbeRadiance(ivec2 tile, vec2 probe_uv, vec2 inv_atlas_size)
{
	vec2 texel = vec2(tile * GI_PROBE_BORDER_RES) + float(GI_PROBE_RADIANCE_BORDER) +
	             probe_uv * float(GI_PROBE_TRACE_RES);
	return texture2DLod(s_gi_probe_border, texel * inv_atlas_size, 0.0).xyz;
}

vec3 GiInterpolateRadiance(GiProbeSample probes, vec3 direction)
{
	vec2 probe_uv = GiInverseEquiAreaSphericalMapping(direction);
	vec2 inv_atlas_size = vec2_splat(1.0) / vec2(textureSize(s_gi_probe_border, 0));
	vec3 radiance = probes.weights.x * GiProbeRadiance(probes.tiles01.xy, probe_uv, inv_atlas_size);
	BRANCH
	if(!u_gi_stochastic_interpolation)
	{
		radiance += probes.weights.y * GiProbeRadiance(probes.tiles01.zw, probe_uv, inv_atlas_size) +
		            probes.weights.z * GiProbeRadiance(probes.tiles23.xy, probe_uv, inv_atlas_size) +
		            probes.weights.w * GiProbeRadiance(probes.tiles23.zw, probe_uv, inv_atlas_size);
	}
	return radiance;
}

/// The rough specular's lerp to the diffuse: 1 = the rough specular is E / pi (the traced reflections own the pixel,
/// or the lobe is wide enough), 0 = all GGX samples.
float GiRoughDiffuseLerp(float roughness)
{
	float traced = saturate((u_gi_max_roughness_to_trace - roughness) / GI_ROUGHNESS_FADE_LENGTH);
	if(traced >= 1.0)
	{
		return 1.0;
	}
	return saturate((roughness - GI_MAX_ROUGHNESS_TO_EVALUATE_ROUGH_SPECULAR + GI_ROUGH_SPECULAR_FADE_LENGTH) /
	                GI_ROUGH_SPECULAR_FADE_LENGTH);
}

float GiLuminance(vec3 color)
{
	return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

/// The lobe's mean radiance from the probes (pre-exposed): GI_ROUGH_SPECULAR_SAMPLES visible-normal
/// samples (Hammersley points with a per-pixel PCG seed, E.y biased), averaged in x / (1 + Y).
vec3 GiRoughSpecular(ivec2 pixel, GiProbeSample probes, vec3 position, vec3 normal, float roughness)
{
	float alpha = max(roughness, GI_ROUGH_SPECULAR_MIN_ROUGHNESS);
	alpha *= alpha;
	vec3 camera = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	vec3 view = normalize(camera - position);
	GiReflectionBasis basis = GiReflectionMakeBasis(normal);
	vec3 view_ts = GiReflectionToTangent(basis, view);
	view_ts.z = max(view_ts.z, 1e-4);
	view_ts = normalize(view_ts);
	int frame_mod = int(mod(u_gi_frame_index, 8.0));
	uvec2 seed = Rand3DPCG16(ivec3(pixel, frame_mod)).xy;
	vec3 sum = vec3_splat(0.0);
	for(int i = 0; i < GI_ROUGH_SPECULAR_SAMPLES; ++i)
	{
		vec2 e = Hammersley16(uint(i), uint(GI_ROUGH_SPECULAR_SAMPLES), seed);
		e.y = (e.y - 0.5) * (1.0 - GI_SPECULAR_SAMPLE_BIAS) + 0.5;
		vec3 half_ts = GiReflectionSampleGgxVndf(view_ts, alpha, e.x, e.y);
		vec3 half_vector = GiReflectionToWorld(basis, half_ts);
		vec3 direction = 2.0 * dot(view, half_vector) * half_vector - view;
		vec3 radiance = GiInterpolateRadiance(probes, direction);
		sum += radiance / (1.0 + GiLuminance(radiance));
	}
	vec3 mean = sum / float(GI_ROUGH_SPECULAR_SAMPLES);
	return mean / max(1.0 - GiLuminance(mean), 1e-4);
}

/// Last frame's rough specular over the shared taps, in this frame's pre-exposure.
vec3 GiReadRoughHistory(GiHistoryTaps taps)
{
	float weight_sum = dot(taps.weights, vec4_splat(1.0));
	if(weight_sum <= 0.0)
	{
		return vec3_splat(0.0);
	}
	vec3 color = vec3_splat(0.0);
	for(int tap = 0; tap < 4; ++tap)
	{
		color += GiWeightedHistoryTap(GiHistoryTapWeight(taps, tap),
		                                 texelFetch(s_gi_rough_history, GiHistoryTapTexel(taps, tap), 0)).xyz;
	}
	return color / max(weight_sum, 1e-5) * u_history_pre_exposure_correction;
}

NUM_THREADS(8, 8, 1)
void main()
{
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	ivec2 group_origin = ivec2(gl_WorkGroupID.xy) * 8;
#if defined(GI_INTEGRATE_SHORT_RANGE_AO)
	// The AO clamp's neighbourhoods of a full-resolution search, before any thread returns.
	GiLoadShortRangeAOTile(group_origin, int(gl_LocalInvocationIndex));
	barrier();
#endif
	if(pixel.x >= int(u_gi_view_size.x) || pixel.y >= int(u_gi_view_size.y))
	{
		return;
	}
	float depth01 = texelFetch(s_gi_depth, pixel, 0).x;
	// Sky: nothing is written. Every reader masks the sky by depth (the composite, the debug views) or by the history
	// taps' depth test, whose weight-0 taps are selected out (GiWeightedHistoryTap).
	if(depth01 >= 1.0)
	{
		return;
	}
	vec3 position = GiWorldFromDepth(GiPixelUv(pixel), depth01);
	vec4 gbuffer1 = texelFetch(s_gi_normal, pixel, 0);
	vec3 normal = decodeNormalOctahedron(gbuffer1.xy);
	float roughness = gbuffer1.w;
	float depth = GiLinearDepth(depth01);
	GiProbeSample probes = GiInterpolationProbes(pixel, position, normal, depth);
	BRANCH
	if(u_gi_stochastic_interpolation)
	{
		probes = GiDrawProbe(probes, pixel);
	}
	vec3 current = vec3_splat(0.0);
	vec3 rough_current = vec3_splat(0.0);
	BRANCH
	if(probes.valid)
	{
		// The interpolated SH's irradiance is clamped at zero: where it rings negative the temporal would otherwise
		// average the dip in.
		current = max(GiInterpolateIrradianceOverPi(probes, normal), vec3_splat(0.0));
		rough_current = current;
		float diffuse_lerp = GiRoughDiffuseLerp(roughness);
		BRANCH
		if(u_gi_rough_specular && diffuse_lerp < 1.0)
		{
			vec3 lobe = GiRoughSpecular(pixel, probes, position, normal, roughness);
			rough_current = mix(lobe, current, diffuse_lerp);
		}
	}
	GiHistoryTaps taps;
	taps.origin = ivec2(0, 0);
	taps.weights = vec4_splat(0.0);
	BRANCH
	if(u_gi_has_history)
	{
		taps = GiHistoryReprojection(pixel, position, normal);
	}
	GiHistorySample history_read = GiReadHistorySample(taps);
	vec4 history_sample = history_read.color_frames;
	vec3 rough_history = vec3_splat(0.0);
	BRANCH
	if(u_gi_rough_specular)
	{
		rough_history = GiReadRoughHistory(taps);
	}
	float frames = history_sample.w;
	float fast_update = 0.0;
	BRANCH
	if(u_gi_fast_update)
	{
		fast_update = probes.valid ? GiFastUpdateAmount(GiInterpolateMoving(probes)) : 0.0;
		float held_fast_update =
		    max(fast_update, min(history_read.fast_update, GI_TEMPORAL_FAST_UPDATE_MAX_AMOUNT));
		frames = min(frames, (1.0 - held_fast_update) * u_gi_temporal_max_frames);
	}
	float blend = 1.0 / (1.0 + frames);
	if(!probes.valid && frames >= 1.0)
	{
		blend = 1.0 / (1.0 + GI_TEMPORAL_INVALID_CURRENT_WEIGHT * frames);
	}
	vec3 result = max(mix(history_sample.xyz, current, blend), vec3_splat(0.0));
	vec3 rough_result = max(mix(rough_history, rough_current, blend), vec3_splat(0.0));
	float history_alpha = GiEncodeHistoryAlpha(frames, fast_update, u_gi_temporal_max_frames);
	BRANCH
	if(u_gi_show_interpolation_fallback && probes.fallback)
	{
		result = probes.valid ? vec3(1.0, 0.0, 0.0) : vec3(1.0, 0.0, 1.0);
		history_alpha = 0.0;
	}
	imageStore(s_gi_history_out, pixel, vec4(result, history_alpha));
	// The rough specular history's rg11b10f store is dithered by a per-pixel noise scalar (GiQuantizeForRg11b10f).
	float quantize_noise = InterleavedGradientNoise(vec2(pixel), mod(u_gi_frame_index, 8.0));
	imageStore(s_gi_rough_history_out, pixel, vec4(GiQuantizeForRg11b10f(rough_result, quantize_noise), 1.0));
#ifdef GI_INTEGRATE_SHORT_RANGE_AO
	vec4 ao = GiAccumulateShortRangeAO(pixel, position, normal, depth, taps, frames, group_origin);
	imageStore(s_gi_short_range_ao_history_out, pixel, uvec4(GiPackShortRangeAO(ao), 0u, 0u, 0u));
	imageStore(s_gi_short_range_ao_screen_out, pixel, GiShortRangeAOScreen(ao, normal));
#endif
}
