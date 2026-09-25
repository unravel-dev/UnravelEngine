#ifndef __GI_REFLECTION_TIERS_SH__
#define __GI_REFLECTION_TIERS_SH__

/*
 * ROUGHNESS TIERS of the GI reflections, shared by the two composites that split each pixel
 * between them. The traced tier (fs_gi_reflection_composite.sc) saw its occluders and goes
 * into RBUFFER with the traced layers; the rough tier - last frame's resolved gather, which
 * carries the probe lattice's visibility and not the pixel's - is untraced and goes into the
 * probe layer (fs_gi_reflection_rough.sc), which the indirect pass occludes. Their weights
 * reproduce the single blend they replace: over the probe layer P, the traced value T at
 * coverage c and the rough value R used to composite as c * mix(T, R, s) + (1 - c) * P.
 */

#include "gi/gi_constants.sh"

/// The rough tier's share of a pixel's GI reflection: 0 below GI_REFLECTION_GATHER_FADE_START,
/// 1 from GI_REFLECTION_ROUGH_CUTOFF on (the traced tier does not run there), a smoothstep
/// between for continuity at the cutoff. On the RAW authored roughness, like the tiering.
float GiReflectionRoughShare(float roughness)
{
	return smoothstep(GI_REFLECTION_GATHER_FADE_START, GI_REFLECTION_ROUGH_CUTOFF, roughness);
}

/// The traced tier's coverage of the probe layer: c * (1 - s).
float GiReflectionTracedCoverage(float coverage, float rough_share)
{
	return coverage * (1.0 - rough_share);
}

/// The rough tier's blend weight over what the traced tier left of the probe layer: it owns
/// c * s of the total, and 1 - c * (1 - s) is left, so the probe layer keeps exactly 1 - c.
float GiReflectionRoughWeight(float coverage, float rough_share)
{
	float uncovered = 1.0 - GiReflectionTracedCoverage(coverage, rough_share);
	return saturate(coverage * rough_share / max(uncovered, 1e-4));
}

#endif // __GI_REFLECTION_TIERS_SH__
