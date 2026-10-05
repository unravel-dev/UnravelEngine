#ifndef __LUMEN_COMMON_SH__
#define __LUMEN_COMMON_SH__

/*
 * Shared math of the Lumen-style screen probe gather (lumen_gather_pass): the equal-area octahedral
 * direction mapping, SH3, the probe atlas layout and the per-frame jitter sequences, as UE 5.8 Lumen
 * defines them (tasks/lumen_transform/analysis d_ and e_ chapters).
 *
 * Probe atlas layout: probe tile (x, y) owns the LUMEN_PROBE_TRACE_RES^2 texel tile at (x, y) *
 * LUMEN_PROBE_TRACE_RES of every per-probe texture. Rows below the uniform probes' hold the adaptive probes
 * (lumen_adaptive_probes.sh); a slot no probe was placed in has a record without probe.
 */

#include "lumen/lumen_constants.sh"
#include "sampling.sh"

#define LUMEN_PI 3.14159265359
/// The probe trace's far-field pass (cs_lumen_probe_trace.sc): threads per group, and groups per row of its indirect
/// dispatch (below the 65535 groups an axis takes).
#define LUMEN_TRACE_FAR_FIELD_GROUP 64
#define LUMEN_TRACE_FAR_FIELD_ROW_GROUPS 32768
/// 2^-64: avoids 0 / 0 in the inverse mapping without changing it (UE MonteCarlo.ush).
#define LUMEN_INVERSE_MAPPING_EPSILON 5.42101086243e-20

/// x = frame index, y = frame % LUMEN_PROBE_JITTER_PERIOD, zw = this frame's placement jitter in pixels.
uniform vec4 u_lumen_frame;
/// x, y = uniform probe counts, z = downsample factor in pixels, w = rows of the probe atlas (the uniform probes'
/// and the adaptive probes' capacity).
uniform vec4 u_lumen_probes;
/// x, y = full-resolution view size, z, w = 1 / size.
uniform vec4 u_lumen_view;
/// x > 0 when the probes trace importance-sampled ray slots (cs_lumen_probe_generate_rays.sc); y > 0 turns
/// the integrate's full-resolution jitter off, z > 0 rejects screen hits within a few pixels of the probe,
/// w = experiment bits: 1 shades screen hits from the distance-field hits' store (the cards) instead of last frame's
/// colour, 2 re-bins the composite with a second slice of the ray jitter (experiment toggles).
uniform vec4 u_lumen_options;
/// The view's GI settings (lumen_pass::make_settings_uniform): x = the farthest a ray travels in metres, y = the
/// frames the gather's temporal accumulates at most, z = the roughness below which pixels trace reflection rays,
/// w = the integrate's full-resolution jitter in probe tiles.
uniform vec4 u_lumen_settings;

#define u_lumen_frame_index       u_lumen_frame.x
#define u_lumen_frame_mod         u_lumen_frame.y
#define u_lumen_placement_jitter  u_lumen_frame.zw
#define u_lumen_probe_count       ivec2(u_lumen_probes.xy)
#define u_lumen_downsample        u_lumen_probes.z
#define u_lumen_atlas_rows        int(u_lumen_probes.w)
#define u_lumen_view_size         u_lumen_view.xy
#define u_lumen_view_texel        u_lumen_view.zw
#define u_lumen_importance_sampling (u_lumen_options.x > 0.0)
#define u_lumen_full_res_jitter     (u_lumen_options.y <= 0.0)
#define u_lumen_reject_near_hits    (u_lumen_options.z > 0.0)
#define u_lumen_voxel_screen_hits   ((int(u_lumen_options.w) & 1) != 0)
#define u_lumen_jitter_slice_dither ((int(u_lumen_options.w) & 2) != 0)
#define u_lumen_max_trace_distance     u_lumen_settings.x
#define u_lumen_temporal_max_frames    u_lumen_settings.y
#define u_lumen_max_roughness_to_trace u_lumen_settings.z
#define u_lumen_full_res_jitter_width  u_lumen_settings.w

/// Two noise values per pixel and frame in the role of UE's spatiotemporal blue noise (BlueNoiseVec2):
/// SpatioTemporalNoise2D over the frame index, so any window of frames stratifies a pixel's samples.
vec2 LumenSpatioTemporalNoise2D(vec2 coord)
{
	return SpatioTemporalNoise2D(coord, u_lumen_frame_index);
}

/// Equal-area octahedral mapping of the unit square onto the sphere [Clarberg 2008]
/// (UE MonteCarlo.ush EquiAreaSphericalMapping). Every texel of an N x N map covers 4 pi / N^2
/// steradians. The mapping's pole is world up: UE's z is Unravel's y.
vec3 LumenEquiAreaSphericalMapping(vec2 uv)
{
	uv = 2.0 * uv - 1.0;
	float d = 1.0 - (abs(uv.x) + abs(uv.y));
	float r = 1.0 - abs(d);
	float phi = 0.0;
	if(r > 0.0)
	{
		phi = (LUMEN_PI / 4.0) * ((abs(uv.y) - abs(uv.x)) / r + 1.0);
	}
	float f = r * sqrt(2.0 - r * r);
	vec3 pole_z = vec3(f * sign(uv.x) * abs(cos(phi)), f * sign(uv.y) * abs(sin(phi)), sign(d) * (1.0 - r * r));
	return vec3(pole_z.x, pole_z.z, pole_z.y);
}

/// Inverse of LumenEquiAreaSphericalMapping (UE InverseEquiAreaSphericalMapping, with its minimax atan).
vec2 LumenInverseEquiAreaSphericalMapping(vec3 direction)
{
	vec3 d = normalize(vec3(direction.x, direction.z, direction.y));
	vec3 a = abs(d);
	float r = sqrt(max(1.0 - a.z, 0.0));
	float x = min(a.x, a.y) / (max(a.x, a.y) + LUMEN_INVERSE_MAPPING_EPSILON);
	float phi = 0.419038818029165735901852432784e-1 + (-0.251390972343483509333252996350e-1) * x;
	phi = 0.881770664775316294736387951347e-1 + phi * x;
	phi = -0.247333733281268944196501420480 + phi * x;
	phi = 0.61572017898280213493197203466e-2 + phi * x;
	phi = 0.636226545274016134946890922156 + phi * x;
	phi = 0.406758566246788489601959989e-5 + phi * x;
	if(a.x < a.y)
	{
		phi = 1.0 - phi;
	}
	vec2 uv = vec2(r - phi * r, phi * r);
	if(d.z < 0.0)
	{
		uv = vec2_splat(1.0) - uv.yx;
	}
	uv.x = d.x < 0.0 ? -uv.x : uv.x;
	uv.y = d.y < 0.0 ? -uv.y : uv.y;
	return uv * 0.5 + 0.5;
}

/// Wraps a texel coordinate of a (resolution)^2 map carrying a border of @p border texels across the
/// octahedron's folds, so bilinear reads and stochastic re-binning stay continuous (UE
/// OctahedralCommon.ush OctahedralMapWrapBorder). Returns the interior coordinate.
ivec2 LumenOctahedralMapWrapBorder(ivec2 texel, int resolution, int border)
{
	if(texel.x < border)
	{
		texel.x = border - 1 + border - texel.x;
		texel.y = resolution - 1 - texel.y;
	}
	if(texel.x >= resolution - border)
	{
		texel.x = (resolution - border) - (texel.x - (resolution - border - 1));
		texel.y = resolution - 1 - texel.y;
	}
	if(texel.y < border)
	{
		texel.y = border - 1 + border - texel.y;
		texel.x = resolution - 1 - texel.x;
	}
	if(texel.y >= resolution - border)
	{
		texel.y = (resolution - border) - (texel.y - (resolution - border - 1));
		texel.x = resolution - 1 - texel.x;
	}
	return texel - ivec2(border, border);
}

/// Third-order SH basis (UE SHCommon.ush SHBasisFunction3): c[0] = v0.x .. c[8] = v2.
struct LumenSH3
{
	vec4 v0;
	vec4 v1;
	float v2;
};

LumenSH3 LumenSHBasis3(vec3 n)
{
	LumenSH3 sh;
	sh.v0 = vec4(0.282095, -0.488603 * n.y, 0.488603 * n.z, -0.488603 * n.x);
	vec3 n2 = n * n;
	sh.v1 = vec4(1.092548 * n.x * n.y, -1.092548 * n.y * n.z, 0.315392 * (3.0 * n2.z - 1.0), -1.092548 * n.x * n.z);
	sh.v2 = 0.546274 * (n2.x - n2.y);
	return sh;
}

/// The clamped-cosine lobe around @p n in SH3 (UE CalcDiffuseTransferSH3 with exponent 1): band
/// factors pi, 2 pi / 3, pi / 4.
LumenSH3 LumenDiffuseTransferSH3(vec3 n)
{
	LumenSH3 sh = LumenSHBasis3(n);
	sh.v0.x *= LUMEN_PI;
	sh.v0.yzw *= 2.0 * LUMEN_PI / 3.0;
	sh.v1 *= LUMEN_PI / 4.0;
	sh.v2 *= LUMEN_PI / 4.0;
	return sh;
}

float LumenDotSH3(LumenSH3 a, LumenSH3 b)
{
	return dot(a.v0, b.v0) + dot(a.v1, b.v1) + a.v2 * b.v2;
}

/// The per-probe-tile ray jitter inside each octahedral texel: one 2D offset per probe tile per frame,
/// blue noise over tiles, period LUMEN_PROBE_JITTER_PERIOD (UE BlueNoiseVec2(tile, frame % 8)).
vec2 LumenProbeRayJitter(ivec2 tile, float slice)
{
	return SpatioTemporalNoise2D(vec2(tile), slice);
}

/// The composite's re-binning dither of probe tile @p tile, period LUMEN_PROBE_JITTER_PERIOD (UE reads a blue noise
/// slice half a period from the ray jitter's, S/LumenScreenProbeFiltering.usf:96-100). It must not be a function of
/// the ray jitter: another slice of LumenProbeRayJitter is the jitter plus a constant, which rounds every ray of a
/// texel the same way and moves the directions each texel averages off its centre. Interleaved gradient noise on the
/// two mirrored lattices (independent of the jitter's direct and transposed ones) keeps it blue over tiles, and the
/// swapped R2 increments make each axis's (jitter, dither) pairs over a period follow the R2 sequence.
vec2 LumenProbeRebinDither(ivec2 tile, float frame_mod)
{
	vec2 coord = vec2(tile);
	vec2 spatial = vec2(InterleavedGradientNoise(vec2(coord.x, -coord.y)), InterleavedGradientNoise(vec2(-coord.y, coord.x)));
	return fract(spatial + mod(frame_mod, SAMPLING_R2_PERIOD) * SAMPLING_R2_INCREMENT.yx);
}

/// Full-resolution pixel of uniform probe @p tile: the tile origin plus this frame's screen-wide
/// placement jitter, clamped into the view (UE LumenScreenProbeCommon.ush:140-145).
ivec2 LumenProbePixel(ivec2 tile)
{
	ivec2 p = tile * int(u_lumen_downsample) + ivec2(u_lumen_placement_jitter);
	return min(p, ivec2(u_lumen_view_size) - ivec2(1, 1));
}

/// Linear view depth of a device depth (any backend's depth range).
float LumenLinearDepth(float depth01)
{
	return screenSpaceToViewSpaceDepth(depth01);
}

/// The gather history's alpha (UE PackFastUpdateModeAmountAndNumFramesAccumulated, LumenScreenProbeGatherTemporal.ush:
/// 72-91): the frame count in LUMEN_TEMPORAL_COUNT_LEVELS steps of @p max_frames, plus (levels + 1) x the fast update
/// amount in as many steps rounded up, so only "not moving" stores 0. An integer below 256, exact in float16.
float LumenEncodeHistoryAlpha(float frames, float fast_update, float max_frames)
{
	float levels = float(LUMEN_TEMPORAL_COUNT_LEVELS);
	float count = floor(saturate(frames / max(max_frames, 1e-5)) * levels + 0.5);
	return count + (levels + 1.0) * ceil(saturate(fast_update) * levels);
}

/// The frame count a gather history alpha stores.
float LumenHistoryAlphaFrames(float alpha, float max_frames)
{
	float levels = float(LUMEN_TEMPORAL_COUNT_LEVELS);
	float encoded = floor(alpha + 0.5);
	float count = encoded - (levels + 1.0) * floor(encoded / (levels + 1.0));
	return count / levels * max_frames;
}

/// The fast update amount a gather history alpha stores.
float LumenHistoryAlphaFastUpdate(float alpha)
{
	float levels = float(LUMEN_TEMPORAL_COUNT_LEVELS);
	return floor(floor(alpha + 0.5) / (levels + 1.0)) / levels;
}

/// A probe trace record's alpha (UE EncodeProbeRayDistance's moving bit): the filter distance, or -(distance + 1)
/// when the ray's screen hit moves against the probe.
float LumenEncodeTraceDistance(float distance, bool is_moving)
{
	return is_moving ? -(distance + 1.0) : distance;
}

/// The filter distance of a probe trace record's alpha.
float LumenTraceDistance(float alpha)
{
	return alpha < 0.0 ? -alpha - 1.0 : alpha;
}

/// Screen uv of a full-resolution pixel centre.
vec2 LumenPixelUv(ivec2 pixel)
{
	return (vec2(pixel) + vec2_splat(0.5)) * u_lumen_view_texel;
}

/// World position of screen @p uv at device depth @p depth01 (u_invViewProj of the pass view).
vec3 LumenWorldFromDepth(vec2 uv, float depth01)
{
	return clipToWorld(u_invViewProj, clipTransform(vec3(uv * 2.0 - 1.0, toClipSpaceDepth(depth01))));
}

/// One probe ray slot: the octahedral texel it traces and its level (1 = a texel of the probe's N x N map,
/// N = LUMEN_PROBE_TRACE_RES; 0 = a texel of the 2N x 2N map, a quarter of one), packed as
/// x | y << 6 | level << 12 (UE LumenScreenProbeTracingCommon.ush:67-78).
uint LumenPackRay(ivec2 texel, int level)
{
	return uint(texel.x & 63) | (uint(texel.y & 63) << 6u) | (uint(level & 15) << 12u);
}

/// xy = texel, z = level.
ivec3 LumenUnpackRay(uint packed)
{
	return ivec3(int(packed & 63u), int((packed >> 6u) & 63u), int((packed >> 12u) & 15u));
}

/// Texels per axis of the octahedral map a ray slot's level indexes.
int LumenRayResolution(int level)
{
	return LUMEN_PROBE_TRACE_RES << (1 - level);
}

/// The ray slot @p slot of a probe traces: its importance-sampled assignment when the ray generator ran,
/// else the slot's own texel. @p ray_info is the generator's output value.
ivec3 LumenProbeRaySlot(ivec2 slot, float ray_info)
{
	return u_lumen_importance_sampling ? LumenUnpackRay(uint(ray_info)) : ivec3(slot, 1);
}

/// Levels per axis of a record's octahedral normal (12 bits each: the pair packs below 2^24).
#define LUMEN_PROBE_NORMAL_LEVELS 4096.0

/// Bits per axis of a record's pixel, and the bit that keeps the packed pattern a normal float.
#define LUMEN_PROBE_PIXEL_BITS 15u
#define LUMEN_PROBE_PIXEL_MASK 0x7FFFu
#define LUMEN_PROBE_PIXEL_EXPONENT_BIT 0x40000000u

/// Per-probe packed record (RGBA32F texture, one texel per probe): x = linear view depth (<= 0: no probe),
/// y = octahedral world normal (u + v x LUMEN_PROBE_NORMAL_LEVELS, an integer below 2^24, exact in a float),
/// z = the probe's full-resolution pixel as the bits x | y << 15 | 1 << 30 (the set bit keeps the exponent between
/// 128 and 254 for y below 32512, so the pattern is a normal float the texture stores bit-exact;
/// LUMEN_PROBE_MAX_VIEW_EXTENT), w = device depth.
vec4 LumenPackProbe(float view_depth, vec3 normal, ivec2 pixel, float depth01)
{
	vec2 octahedral = floor(saturate(encodeNormalOctahedron(normal)) * (LUMEN_PROBE_NORMAL_LEVELS - 1.0) + 0.5);
	uint packed_pixel = LUMEN_PROBE_PIXEL_EXPONENT_BIT | (uint(pixel.x) & LUMEN_PROBE_PIXEL_MASK) |
	                    ((uint(pixel.y) & LUMEN_PROBE_PIXEL_MASK) << LUMEN_PROBE_PIXEL_BITS);
	return vec4(view_depth,
	            octahedral.x + octahedral.y * LUMEN_PROBE_NORMAL_LEVELS,
	            uintBitsToFloat(packed_pixel),
	            depth01);
}

/// @p v, or 0 when it is NaN or infinite (UE MakeFinite: an exponent test, which no float optimisation removes).
float LumenMakeFinite(float v)
{
	return (floatBitsToUint(v) & 0x7F800000u) == 0x7F800000u ? 0.0 : v;
}

vec3 LumenMakeFinite3(vec3 v)
{
	return vec3(LumenMakeFinite(v.x), LumenMakeFinite(v.y), LumenMakeFinite(v.z));
}

/// @p v made finite and clamped to what a float16 store keeps finite (LUMEN_FLOAT16_MAX).
vec3 LumenToFloat16Range(vec3 v)
{
	return min(LumenMakeFinite3(v), vec3_splat(LUMEN_FLOAT16_MAX));
}

/// (low, high) of a value packed as low + high x @p stride (a power of two).
vec2 LumenUnpackPair(float packed, float stride)
{
	float high = floor(packed / stride);
	return vec2(packed - high * stride, high);
}

vec3 LumenProbeNormal(vec4 record)
{
	return decodeNormalOctahedron(LumenUnpackPair(record.y, LUMEN_PROBE_NORMAL_LEVELS) / (LUMEN_PROBE_NORMAL_LEVELS - 1.0));
}

/// The full-resolution pixel the probe was placed at.
ivec2 LumenProbeRecordPixel(vec4 record)
{
	uint packed_pixel = floatBitsToUint(record.z);
	return ivec2(int(packed_pixel & LUMEN_PROBE_PIXEL_MASK),
	             int((packed_pixel >> LUMEN_PROBE_PIXEL_BITS) & LUMEN_PROBE_PIXEL_MASK));
}

/// World position of a probe from its record, under this frame's camera.
vec3 LumenProbePosition(vec4 record)
{
	return LumenWorldFromDepth(LumenPixelUv(LumenProbeRecordPixel(record)), record.w);
}

/// The uniform tile of the probe at atlas tile @p tile (UE GetScreenTileCoord): its own for a uniform probe, the
/// tile its pixel lies in for an adaptive one, which shares that tile's ray jitter and filter neighbours.
ivec2 LumenProbeScreenTile(ivec2 tile, vec4 record)
{
	if(tile.y < u_lumen_probe_count.y)
	{
		return tile;
	}
	return (LumenProbeRecordPixel(record) - ivec2(u_lumen_placement_jitter)) / int(u_lumen_downsample);
}

/// The 2x2 uniform probes a full-resolution position interpolates (UE CalculateUniformUpsampleInterpolationWeights):
/// xy = the base tile, zw = the expanded bilinear fractions towards +x / +y (never fully on one probe). @p coord is
/// the pixel minus this frame's placement jitter (plus the integrate's jitter), truncated to a whole pixel as UE's
/// uint conversion does.
vec4 LumenUniformInterpolation(vec2 coord)
{
	float downsample = u_lumen_downsample;
	vec2 clamped = floor(clamp(coord, vec2_splat(0.0), u_lumen_view_size - 1.0));
	vec2 base = min(floor(clamped / downsample), vec2(u_lumen_probe_count - ivec2(2, 2)));
	return vec4(base, (clamped - base * downsample + 1.0) / (downsample + 2.0));
}

/// One probe's interpolation weights for a pixel on the plane (@p position, @p normal) at view depth @p depth, the
/// bilinear weight @p bilinear included: x = primary, y = fallback (0 for a record without probe).
vec2 LumenProbeCornerWeights(vec4 record, float bilinear, vec3 position, vec3 normal, float depth)
{
	if(record.x <= 0.0)
	{
		return vec2_splat(0.0);
	}
	float plane_distance = abs(dot(LumenProbePosition(record) - position, normal)) / depth;
	float r2 = plane_distance * plane_distance;
	return bilinear * vec2(exp2(-LUMEN_INTERP_DEPTH_WEIGHT * r2), exp2(-LUMEN_INTERP_FALLBACK_DEPTH_WEIGHT * r2));
}

#endif // __LUMEN_COMMON_SH__
