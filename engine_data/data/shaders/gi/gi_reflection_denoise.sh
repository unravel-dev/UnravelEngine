#ifndef __GI_REFLECTION_DENOISE_SH__
#define __GI_REFLECTION_DENOISE_SH__

/*
 * DENOISER SPACE for the reflection tier - the colour space every average, statistic and
 * clamp in the temporal and the composite runs in.
 *
 * Ported from Lumen (LumenReflectionDenoiserCommon.ush:12-39,
 * r.Lumen.Reflections.DenoiserTonemapRange). Two properties, both of which this chain used
 * to lack:
 *
 *  - BOUNDED RANGE. L / (1 + Lum / range) is near-linear up to scene-referred whites and
 *    compresses only spikes, so a single firefly tap can no longer carry a weighted mean
 *    and a single bright NEIGHBOUR can no longer stretch the history clamp box until it
 *    rejects nothing. Expanded back exactly at the store, so the accumulated mean stays a
 *    linear, pre-exposed radiance that the composite and RBUFFER can consume unchanged -
 *    and so the history's pre-exposure correction, which is a linear scale, stays valid.
 *
 *  - YCoCg. Neighbourhood statistics separate luma from chroma, so the clamp bounds a
 *    stale history's brightness and its colour independently instead of per RGB channel.
 *
 * Order matters and is fixed: compress in RGB, then rotate to YCoCg. The inverse runs the
 * other way.
 */

#include "gi/gi_constants.sh"

/// Rec.709 luminance (common.sh carries no Luminance helper).
float GiReflLuma(vec3 color)
{
	return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

vec3 GiReflToBounded(vec3 color)
{
	return color / (1.0 + GiReflLuma(color) / GI_REFLECTION_DENOISER_RANGE);
}

vec3 GiReflFromBounded(vec3 bounded)
{
	// The forward map keeps Lum(bounded) strictly below the range, so the denominator is
	// positive by construction; the floor only bounds the expansion of fp16 round-off at
	// the very top of the range.
	return bounded / max(1.0 - GiReflLuma(bounded) / GI_REFLECTION_DENOISER_RANGE, 1e-4);
}

vec3 GiReflRgbToYCoCg(vec3 rgb)
{
	return vec3(dot(rgb, vec3(0.25, 0.5, 0.25)),
	            dot(rgb, vec3(0.5, 0.0, -0.5)),
	            dot(rgb, vec3(-0.25, 0.5, -0.25)));
}

vec3 GiReflYCoCgToRgb(vec3 ycocg)
{
	return vec3(ycocg.x + ycocg.y - ycocg.z, ycocg.x + ycocg.z, ycocg.x - ycocg.y - ycocg.z);
}

/// Denoiser-space YCoCg of a linear, pre-exposed radiance - the statistic space of the
/// temporal's neighbourhood and of its history clamp.
vec3 GiReflToDenoiser(vec3 color)
{
	return GiReflRgbToYCoCg(GiReflToBounded(color));
}

vec3 GiReflFromDenoiser(vec3 denoiser)
{
	return GiReflFromBounded(GiReflYCoCgToRgb(denoiser));
}

#endif // __GI_REFLECTION_DENOISE_SH__
