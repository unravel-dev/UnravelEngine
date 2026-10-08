#ifndef __LUMEN_REFLECTION_COMMON_SH__
#define __LUMEN_REFLECTION_COMMON_SH__

/*
 * Shared state of the Lumen reflection passes (lumen_reflection_pass; UE 5.8 LumenReflectionCommon.ush,
 * LumenReflectionDenoiserCommon.ush, LumenReflectionsCombine.ush). The traces run at full resolution as at Epic, or
 * at the lowest reflection quality for one pixel of each 2 x 2 block (LumenReflectionTracePixel); per trace texel:
 *  - the ray buffer: xyz = the traced direction, w = its cone angle 1 / pdf, 0 where the pixel traces no ray;
 *  - the trace hit: the distance the trace ended at, negative for a hit (UE EncodeRayDistance), +0 where the
 *    pixel traces no ray;
 *  - the trace radiance: rgb pre-exposed, clamped to LUMEN_REFLECTION_MAX_RAY_INTENSITY.
 * The resolve and the denoisers run per pixel; they average in L / (1 + Y / LUMEN_REFLECTION_DENOISER_TONEMAP_RANGE).
 */

#include "lumen/lumen_common.sh"

/// x = Hi-Z mip count, y = flags (1: the screen traces run, 2: the traces paint their type instead of radiance, 4: the
/// screen pass marks every tile as tracing, 8: the distance-field pass's misses trace the screen again from where the
/// distance field ends), z = the roughness the traced reflections end at (UE LumenMaxRoughnessToTraceReflections, the
/// gi_component's Max Roughness To Trace), w > 0 when the denoiser histories hold last frame.
uniform vec4 u_lumen_reflection;

#define u_lumen_reflection_hiz_mip_count   int(u_lumen_reflection.x)
#define u_lumen_reflection_screen_traces   ((int(u_lumen_reflection.y) & 1) != 0)
/// UE DEBUG_VISUALIZE_TRACE_TYPES: screen hits red, distance-field hits green (yellow when lit by last frame's
/// scene colour), distant screen hits magenta, misses blue.
#define u_lumen_reflection_show_trace_types ((int(u_lumen_reflection.y) & 2) != 0)
#define u_lumen_reflection_all_tiles        ((int(u_lumen_reflection.y) & 4) != 0)
#define u_lumen_reflection_distant_traces   ((int(u_lumen_reflection.y) & 8) != 0)
#define u_lumen_reflection_max_roughness   u_lumen_reflection.z
#define u_lumen_reflection_has_history     (u_lumen_reflection.w > 0.0)

/// The reflection quality's (lumen_pass::get_reflection_downsample_factor, get_reflection_reconstruction_samples):
/// x = the trace downsample factor (1, or 2: one pixel of each 2 x 2 block traces), y = the neighbouring rays the
/// resolve reuses per pixel; and the composite's: z = the scale of the gather's rough specular history (the GI
/// intensity), w > 0 when that history is bound.
uniform vec4 u_lumen_reflection_quality;

#define u_lumen_reflection_downsample             int(u_lumen_reflection_quality.x)
#define u_lumen_reflection_reconstruction_samples int(u_lumen_reflection_quality.y)
#define u_lumen_reflection_rough_specular_scale   u_lumen_reflection_quality.z
#define u_lumen_reflection_has_rough_specular     (u_lumen_reflection_quality.w > 0.0)

/// The trace buffers' size: the view over the downsample factor, rounded up.
ivec2 LumenReflectionTraceSize()
{
	int factor = u_lumen_reflection_downsample;
	return (ivec2(u_lumen_view_size) + ivec2(factor - 1, factor - 1)) / factor;
}

/// UE GetScreenTileJitter (LumenReflectionCommon.ush:48-69): the pixel of trace texel @p trace_coord's 2 x 2 block that
/// traces, in a 4-rooks pattern that rotates every frame; (0, 0) at full resolution.
ivec2 LumenReflectionTraceJitter(ivec2 trace_coord)
{
	if(u_lumen_reflection_downsample <= 1)
	{
		return ivec2(0, 0);
	}
	// The block's place among its 2 x 2 neighbours plus the frame, modulo 4 (every term is non-negative).
	ivec2 cell = trace_coord - (trace_coord / 2) * 2;
	int rotated = cell.x + cell.y * 2 + int(u_lumen_frame_mod);
	int index = rotated - (rotated / 4) * 4;
	// UE: x = index bit 1, y = the inverse of index bit 0.
	return ivec2(index >= 2 ? 1 : 0, (index == 1 || index == 3) ? 0 : 1);
}

/// The pixel trace texel @p trace_coord traces (UE GetScreenUVFromReflectionTracingCoord), inside the view.
ivec2 LumenReflectionTracePixel(ivec2 trace_coord)
{
	ivec2 pixel = trace_coord * u_lumen_reflection_downsample + LumenReflectionTraceJitter(trace_coord);
	return min(pixel, ivec2(u_lumen_view_size) - ivec2(1, 1));
}

/*
 * Reflection tiles (UE ReflectionTileClassificationMarkCS, LumenReflections.usf): one texel per
 * LUMEN_REFLECTION_TILE_PIXELS x LUMEN_REFLECTION_TILE_PIXELS pixels, 1 where any pixel of the tile traces (the screen
 * pass writes it; every tile at the 2 x 2 trace downsample, whose trace groups span several tiles). The trace,
 * resolve and temporal passes skip the other tiles and write nothing there; a reader takes such a tile's texels as
 * the empty values a traced-nothing pixel writes (no ray, no resolve, no history). An includer defining
 * LUMEN_REFLECTION_TILES_STAGE gets this frame's tiles there.
 */
#define LUMEN_REFLECTION_TILE_PIXELS 8

#ifdef LUMEN_REFLECTION_TILES_STAGE
SAMPLER2D(s_lumen_reflection_tiles, LUMEN_REFLECTION_TILES_STAGE);

/// Whether the tile of @p pixel traces this frame.
bool LumenReflectionTileTraces(ivec2 pixel)
{
	return texelFetch(s_lumen_reflection_tiles, pixel / LUMEN_REFLECTION_TILE_PIXELS, 0).x > 0.5;
}

/// The ray of trace texel @p trace_coord (xyz = direction, w = cone angle, 0 = no ray), no ray in a skipped tile.
vec4 LumenReflectionTraceRay(sampler2D rays, ivec2 trace_coord)
{
	return LumenReflectionTileTraces(LumenReflectionTracePixel(trace_coord)) ? texelFetch(rays, trace_coord, 0)
	                                                                          : vec4_splat(0.0);
}
#endif

/// UE Luminance (Common.ush).
float LumenReflectionLuminance(vec3 color)
{
	return dot(color, vec3(0.3, 0.59, 0.11));
}

/// The traced reflections' weight at @p roughness (UE LumenCombineReflectionsAlpha): 1 below the trace
/// roughness - LUMEN_ROUGHNESS_FADE_LENGTH, 0 from the trace roughness.
float LumenReflectionFadeAlpha(float roughness)
{
	return saturate((u_lumen_reflection_max_roughness - roughness) / LUMEN_ROUGHNESS_FADE_LENGTH);
}

float LumenEncodeRayDistance(float distance, bool hit)
{
	distance = max(distance, 0.0);
	return hit ? -distance : distance;
}

vec3 LumenReflectionToDenoiserSpace(vec3 color)
{
	return color / (1.0 + LumenReflectionLuminance(color) / LUMEN_REFLECTION_DENOISER_TONEMAP_RANGE);
}

vec3 LumenReflectionFromDenoiserSpace(vec3 color)
{
	return color / max(1.0 - LumenReflectionLuminance(color) / LUMEN_REFLECTION_DENOISER_TONEMAP_RANGE, 1e-4);
}

vec3 LumenRGBToYCoCg(vec3 rgb)
{
	return vec3(dot(rgb, vec3(0.25, 0.5, 0.25)), dot(rgb, vec3(0.5, 0.0, -0.5)), dot(rgb, vec3(-0.25, 0.5, -0.25)));
}

vec3 LumenYCoCgToRGB(vec3 ycocg)
{
	return vec3(ycocg.x + ycocg.y - ycocg.z, ycocg.x + ycocg.z, ycocg.x - ycocg.y - ycocg.z);
}

/// UE UniformSampleDiskConcentric (MonteCarlo.ush): the unit square onto the unit disk, area preserving.
vec2 LumenUniformSampleDiskConcentric(vec2 e)
{
	vec2 p = 2.0 * e - 0.99999994;
	vec2 a = abs(p);
	float lo = min(a.x, a.y);
	float hi = max(a.x, a.y);
	float phi = (LUMEN_PI / 4.0) * (lo / (hi + LUMEN_INVERSE_MAPPING_EPSILON) + 2.0 * (a.y >= a.x ? 1.0 : 0.0));
	vec2 disk = abs(vec2(cos(phi), sin(phi)));
	disk.x = p.x < 0.0 ? -disk.x : disk.x;
	disk.y = p.y < 0.0 ? -disk.y : disk.y;
	return disk * hi;
}

/// UE GetSpecularLobeHalfAngle (StochasticLightingCommon.ush, Frostbite): the half angle holding 75% of the
/// lobe's energy.
float LumenSpecularLobeHalfAngle(float roughness)
{
	return atan((roughness * roughness * 0.75) / 0.25);
}

/// UE GetSpecularDominantDirFactor (Frostbite): the lerp(N, R) factor of the lobe's dominant direction.
float LumenSpecularDominantDirFactor(float roughness)
{
	float s = saturate(1.0 - roughness);
	return s * (sqrt(s) + roughness);
}

/// The camera's world position (the pass view).
vec3 LumenReflectionCamera()
{
	return mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
}

#endif // __LUMEN_REFLECTION_COMMON_SH__
