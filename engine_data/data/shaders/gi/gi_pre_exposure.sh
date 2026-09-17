#ifndef __GI_PRE_EXPOSURE_SH__
#define __GI_PRE_EXPOSURE_SH__

/*
 * PRE-EXPOSURE FOR THE GI (UE's Lumen scheme, tasks/auto_exposure_plan.md phase 4).
 *
 * Two spaces meet in the GI, and every read across the boundary converts:
 *
 *   - PERSISTENT STORES (light voxels, world-probe radiance and irradiance, the attribute
 *     emissive volume) hold CACHED LIGHTING at a fixed scale,
 *     GI_CACHED_LIGHTING_PRE_EXPOSURE, exactly as Lumen's surface cache and radiance cache
 *     hold theirs at r.EyeAdaptation.CachedLightingPreExposure. The scale must be fixed,
 *     because the stores outlive any one frame's exposure - changing it invalidates them
 *     (UE resets its caches on a change). Ours is 1: this engine's light units already sit
 *     well under UE's physical scale, so float16 holds them without an offset.
 *
 *   - PER-FRAME BUFFERS (screen probes, the reflection trace, the resolve and its history)
 *     are in the VIEW's pre-exposed space, like Lumen's screen probe gather: every value is
 *     multiplied by the view pre-exposure (pre_exposure.sh), which tracks the adapted
 *     exposure, so a dark scene's radiance lands near 1 instead of near the bottom of
 *     float16. The per-ray clamps (GI_MAX_RAY_RADIANCE) then act on pre-exposed values, as
 *     Lumen's MaxRayIntensity does.
 *
 * Last frame's per-frame buffers were written under the PREVIOUS pre-exposure, so history
 * reads multiply by u_history_pre_exposure_correction (P / Pprev) instead.
 */

#include "../pre_exposure.sh"
#include "gi/gi_constants.sh"

/// Cached lighting (a persistent store) into this frame's pre-exposed space.
vec3 GiCachedToView(vec3 cached)
{
	return cached * (u_pre_exposure_value * (1.0 / GI_CACHED_LIGHTING_PRE_EXPOSURE));
}

/// A threshold expressed on PRE-EXPOSED values into the stores' cached-lighting space, so a
/// clamp tuned against the displayed image bounds a stored value the same way (Lumen applies
/// MaxRayIntensity x View.OneOverPreExposure to the radiosity feeding its cache).
float GiViewToCached(float view_value)
{
	return view_value * (GI_CACHED_LIGHTING_PRE_EXPOSURE * u_pre_exposure_inverse);
}

#endif // __GI_PRE_EXPOSURE_SH__
