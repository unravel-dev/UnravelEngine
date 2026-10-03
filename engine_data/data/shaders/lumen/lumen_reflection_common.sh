#ifndef __LUMEN_REFLECTION_COMMON_SH__
#define __LUMEN_REFLECTION_COMMON_SH__

/*
 * Shared state of the Lumen reflection passes (lumen_reflection_pass; UE 5.8 LumenReflectionCommon.ush,
 * LumenReflectionDenoiserCommon.ush, LumenReflectionsCombine.ush), full resolution as at Epic:
 *  - the ray buffer: xyz = the traced direction, w = its cone angle 1 / pdf, 0 where the pixel traces no ray;
 *  - the trace hit: the distance the trace ended at, negative for a hit (UE EncodeRayDistance), +0 where the
 *    pixel traces no ray;
 *  - the trace radiance: rgb pre-exposed, clamped to LUMEN_REFLECTION_MAX_RAY_INTENSITY.
 * The denoisers average in L / (1 + Y / LUMEN_REFLECTION_DENOISER_TONEMAP_RANGE).
 */

#include "lumen/lumen_common.sh"

/// x = Hi-Z mip count, y = flags (1: the screen traces run, 2: the traces paint their type instead of radiance, 4: the
/// rays rotate their noise by a per-frame hash instead of the R2 sequence),
/// z = the roughness the traced reflections end at (LUMEN_MAX_ROUGHNESS_TO_TRACE; UE
/// r.Lumen.Reflections.MaxRoughnessToTrace overrides it), w > 0 when the denoiser histories hold last frame.
uniform vec4 u_lumen_reflection;

#define u_lumen_reflection_hiz_mip_count   int(u_lumen_reflection.x)
#define u_lumen_reflection_screen_traces   ((int(u_lumen_reflection.y) & 1) != 0)
/// UE DEBUG_VISUALIZE_TRACE_TYPES: screen hits red, distance-field hits green (yellow when lit by last frame's
/// scene colour), misses blue.
#define u_lumen_reflection_show_trace_types ((int(u_lumen_reflection.y) & 2) != 0)
#define u_lumen_reflection_hash_noise ((int(u_lumen_reflection.y) & 4) != 0)
#define u_lumen_reflection_max_roughness   u_lumen_reflection.z
#define u_lumen_reflection_has_history     (u_lumen_reflection.w > 0.0)

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
