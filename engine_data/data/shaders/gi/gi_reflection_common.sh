#ifndef __GI_REFLECTION_COMMON_SH__
#define __GI_REFLECTION_COMMON_SH__

/*
 * Shared state of the GI reflection passes (gi_reflection_pass). The traces run at full resolution at the epic
 * tier, or for one pixel of each 2 x 2 block at the high tier and at the lowest reflection quality
 * (GiReflectionTracePixel); per trace texel:
 *  - the ray buffer: xyz = the traced direction, w = its cone angle 1 / pdf, 0 where the pixel traces no ray;
 *  - the trace hit: the distance the trace ended at, negative for a hit (GiEncodeRayDistance), +0 where the
 *    pixel traces no ray;
 *  - the trace radiance: rgb pre-exposed, clamped to GI_REFLECTION_MAX_RAY_INTENSITY.
 * The resolve and the denoisers run per pixel; they average in L / (1 + Y / GI_REFLECTION_DENOISER_TONEMAP_RANGE).
 */

#include "gi/gi_common.sh"

/// x = Hi-Z mip count, y = flags (1: the screen traces run, 2: the traces paint their type instead of radiance, 4: the
/// screen pass marks every tile as tracing, 8: the distance-field pass's misses trace the screen again from where the
/// distance field ends), z = the roughness the traced reflections end at (the gi_component's Max Roughness To Trace),
/// w > 0 when the denoiser histories hold last frame.
uniform vec4 u_gi_reflection;

#define u_gi_reflection_hiz_mip_count      int(u_gi_reflection.x)
#define u_gi_reflection_screen_traces      ((int(u_gi_reflection.y) & 1) != 0)
/// The trace type debug view: screen hits red, distance-field hits green (yellow when lit by last frame's
/// scene colour), distant screen hits magenta, misses blue.
#define u_gi_reflection_show_trace_types ((int(u_gi_reflection.y) & 2) != 0)
#define u_gi_reflection_all_tiles           ((int(u_gi_reflection.y) & 4) != 0)
#define u_gi_reflection_distant_traces      ((int(u_gi_reflection.y) & 8) != 0)
#define u_gi_reflection_max_roughness      u_gi_reflection.z
#define u_gi_reflection_has_history        (u_gi_reflection.w > 0.0)

/// The reflection quality's (gi_pass::get_reflection_downsample_factor, get_reflection_reconstruction_samples):
/// x = the trace downsample factor (1, or 2: one pixel of each 2 x 2 block traces), y = the neighbouring rays the
/// resolve reuses per pixel; and the composite's: z = the scale of the gather's rough specular history (the GI
/// intensity), w > 0 when that history is bound.
uniform vec4 u_gi_reflection_quality;

#define u_gi_reflection_downsample                int(u_gi_reflection_quality.x)
#define u_gi_reflection_reconstruction_samples int(u_gi_reflection_quality.y)
#define u_gi_reflection_rough_specular_scale      u_gi_reflection_quality.z
#define u_gi_reflection_has_rough_specular        (u_gi_reflection_quality.w > 0.0)

/// The trace buffers' size: the view over the downsample factor, rounded up.
ivec2 GiReflectionTraceSize()
{
	int factor = u_gi_reflection_downsample;
	return (ivec2(u_gi_view_size) + ivec2(factor - 1, factor - 1)) / factor;
}

/// The pixel of trace texel @p trace_coord's 2 x 2 block that traces, in a 4-rooks pattern that rotates every frame;
/// (0, 0) at full resolution.
ivec2 GiReflectionTraceJitter(ivec2 trace_coord)
{
	if(u_gi_reflection_downsample <= 1)
	{
		return ivec2(0, 0);
	}
	// The block's place among its 2 x 2 neighbours plus the frame, modulo 4 (every term is non-negative).
	ivec2 cell = trace_coord - (trace_coord / 2) * 2;
	int rotated = cell.x + cell.y * 2 + int(u_gi_frame_mod);
	int index = rotated - (rotated / 4) * 4;
	// x = index bit 1, y = the inverse of index bit 0.
	return ivec2(index >= 2 ? 1 : 0, (index == 1 || index == 3) ? 0 : 1);
}

/// The pixel trace texel @p trace_coord traces, inside the view.
ivec2 GiReflectionTracePixel(ivec2 trace_coord)
{
	ivec2 pixel = trace_coord * u_gi_reflection_downsample + GiReflectionTraceJitter(trace_coord);
	return min(pixel, ivec2(u_gi_view_size) - ivec2(1, 1));
}

/*
 * Reflection tile classification: one texel per
 * GI_REFLECTION_TILE_PIXELS x GI_REFLECTION_TILE_PIXELS pixels, 1 where any pixel of the tile traces (the screen
 * pass writes it; every tile at the 2 x 2 trace downsample, whose trace groups span several tiles). The trace,
 * resolve and temporal passes skip the other tiles and write nothing there; a reader takes such a tile's texels as
 * the empty values a traced-nothing pixel writes (no ray, no resolve, no history). An includer defining
 * GI_REFLECTION_TILES_STAGE gets this frame's tiles there.
 */
#define GI_REFLECTION_TILE_PIXELS 8

#ifdef GI_REFLECTION_TILES_STAGE
SAMPLER2D(s_gi_reflection_tiles, GI_REFLECTION_TILES_STAGE);

/// Whether the tile of @p pixel traces this frame.
bool GiReflectionTileTraces(ivec2 pixel)
{
	return texelFetch(s_gi_reflection_tiles, pixel / GI_REFLECTION_TILE_PIXELS, 0).x > 0.5;
}

/// The ray of trace texel @p trace_coord (xyz = direction, w = cone angle, 0 = no ray), no ray in a skipped tile.
vec4 GiReflectionTraceRay(sampler2D rays, ivec2 trace_coord)
{
	return GiReflectionTileTraces(GiReflectionTracePixel(trace_coord)) ? texelFetch(rays, trace_coord, 0)
	                                                                          : vec4_splat(0.0);
}
#endif

/// Luminance of @p color (weights 0.3, 0.59, 0.11).
float GiReflectionLuminance(vec3 color)
{
	return dot(color, vec3(0.3, 0.59, 0.11));
}

/// The traced reflections' weight at @p roughness: 1 below the trace
/// roughness - GI_ROUGHNESS_FADE_LENGTH, 0 from the trace roughness.
float GiReflectionFadeAlpha(float roughness)
{
	return saturate((u_gi_reflection_max_roughness - roughness) / GI_ROUGHNESS_FADE_LENGTH);
}

float GiEncodeRayDistance(float distance, bool hit)
{
	distance = max(distance, 0.0);
	return hit ? -distance : distance;
}

vec3 GiReflectionToDenoiserSpace(vec3 color)
{
	return color / (1.0 + GiReflectionLuminance(color) / GI_REFLECTION_DENOISER_TONEMAP_RANGE);
}

vec3 GiReflectionFromDenoiserSpace(vec3 color)
{
	return color / max(1.0 - GiReflectionLuminance(color) / GI_REFLECTION_DENOISER_TONEMAP_RANGE, 1e-4);
}

vec3 GiRGBToYCoCg(vec3 rgb)
{
	return vec3(dot(rgb, vec3(0.25, 0.5, 0.25)), dot(rgb, vec3(0.5, 0.0, -0.5)), dot(rgb, vec3(-0.25, 0.5, -0.25)));
}

vec3 GiYCoCgToRGB(vec3 ycocg)
{
	return vec3(ycocg.x + ycocg.y - ycocg.z, ycocg.x + ycocg.z, ycocg.x - ycocg.y - ycocg.z);
}

/// Concentric mapping of the unit square onto the unit disk, area preserving.
vec2 GiUniformSampleDiskConcentric(vec2 e)
{
	vec2 p = 2.0 * e - 0.99999994;
	vec2 a = abs(p);
	float lo = min(a.x, a.y);
	float hi = max(a.x, a.y);
	float phi = (GI_PI / 4.0) * (lo / (hi + GI_INVERSE_MAPPING_EPSILON) + 2.0 * (a.y >= a.x ? 1.0 : 0.0));
	vec2 disk = abs(vec2(cos(phi), sin(phi)));
	disk.x = p.x < 0.0 ? -disk.x : disk.x;
	disk.y = p.y < 0.0 ? -disk.y : disk.y;
	return disk * hi;
}

/// The half angle of the specular lobe at @p roughness that holds 75% of its energy.
float GiSpecularLobeHalfAngle(float roughness)
{
	return atan((roughness * roughness * 0.75) / 0.25);
}

/// The lerp(N, R) factor of the specular lobe's dominant direction at @p roughness.
float GiSpecularDominantDirFactor(float roughness)
{
	float s = saturate(1.0 - roughness);
	return s * (sqrt(s) + roughness);
}

/// The camera's world position (the pass view).
vec3 GiReflectionCamera()
{
	return mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
}

#endif // __GI_REFLECTION_COMMON_SH__
